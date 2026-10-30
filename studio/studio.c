/*
 * studio.c
 *
 * See studio.h. Rendering goes in chunks of OM_MAX_BLOCK frames. Within a
 * chunk, an engine with block == 0 is rendered up to each of its events and
 * the event applied on its exact frame; an engine with a fixed block gets the
 * chunk's events at the chunk's start.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "studio.h"
#include "fx.h"
#include "filter.h"

enum { EV_NOTE_ON, EV_NOTE_OFF, EV_PARAM, EV_CLOCK };

typedef struct
{
	uint64_t frame;
	uint16_t param;
	uint8_t type, part, note, value;
} Event;

typedef struct
{
	const OmEngineClass *cls;
	void *self;
	Event queue[OM_MAX_EVENTS];   // sorted by frame, oldest first, from head
	uint32_t head, count;
	float buf[2 * OM_MAX_STEMS][OM_MAX_BLOCK];
} Engine;

// gains: where they are, and where they go during the next block
typedef struct
{
	float now, to;
} Gain;

typedef struct
{
	uint8_t engine, stem;
	uint8_t value[OM_MIX_LAST + 1];
	OmFilter *filter;
	Gain left, right, sendA, sendB;
	float peak;
	float duck;       // the key's envelope, 0..1
	float duckRel;    // its release, per frame
	float gate;       // the gate's gain now
	float gateCoef;   // its smoothing, per frame
} Channel;

// the clock the gates follow: the tracker's (24 ticks a beat, a gate step
// every 6), or the tempo when nothing plays
#define CLOCK_QUEUE 256
typedef struct { uint64_t frame; uint8_t event; } ClockEvent;

// ducking: the key's level that dips a channel by the whole DUCK amount
#define DUCK_FULL   0.25f   // -12 dBFS
#define DUCK_ATTACK 0.011f  // per frame: about 2 ms

#define LOOKAHEAD OM_STUDIO_LATENCY   // frames: 1.5 ms at 44.1 kHz
#define LIMIT     0.977f         // -0.2 dBFS

typedef struct
{
	OmFx *fx;
	uint8_t type, ret;
	Gain gain;
	float buf[2][OM_MAX_BLOCK];
	float peak;
} Bus;

struct OmStudio
{
	float rate, bpm;
	uint64_t frame;
	uint8_t engines;
	Engine engine[OM_MAX_ENGINES];

	uint8_t channels;
	Channel channel[OM_MAX_CHANNELS];
	Bus bus[2];
	uint8_t masterLevel;
	Gain master;
	// the limiter
	float delay[2][LOOKAHEAD];
	uint32_t delayPos, holdLeft;
	float hold, env, release;
	float dcIn[2], dcOut[2], dcPole;   // the DC blocker before it
	int dcOn;
	float peakL, peakR, lowestGain;
	ClockEvent clock[CLOCK_QUEUE];   // sorted, from clockHead
	uint32_t clockHead, clockCount;
	double ticks;                    // where the clock is, in ticks
	uint32_t tickCount;              // ticks since the last start
	uint8_t step[OM_MAX_BLOCK];      // the gate step of each frame of this block
};

// a fader's byte as a gain: 100 is 0 dB (registry/mix.json "fader")
static float fader(uint8_t v)
{
	const float x = v / 100.f;
	return x * x * x;
}

OmStudio *om_studio_create(float sampleRate)
{
	OmStudio *st = calloc(1, sizeof(OmStudio));
	if (!st)
		return NULL;
	st->rate = sampleRate;
	st->bpm = 120.f;
	const uint8_t types[2] = {OM_FX_HALL, OM_FX_DELAY};
	for (int b = 0; b < 2; b++)
	{
		st->bus[b].fx = om_fx_create(sampleRate);
		if (!st->bus[b].fx)
		{
			om_studio_destroy(st);
			return NULL;
		}
		om_studio_mix(st, OM_MIX_BUS_A + b, OM_MIX_FX_TYPE, types[b]);
		om_studio_mix(st, OM_MIX_BUS_A + b, OM_MIX_RETURN, 100);
		st->bus[b].gain.now = st->bus[b].gain.to;
	}
	om_studio_mix(st, OM_MIX_MASTER, OM_MIX_LEVEL, 100);
	st->master.now = st->master.to;
	st->hold = st->env = st->lowestGain = 1.f;
	st->release = 1.f - expf(-1.f / (0.15f * sampleRate));   // 150 ms
	st->dcPole = 1.f - 6.2831853f * 5.f / sampleRate;        // 5 Hz
	st->dcOn = 1;
	return st;
}

void om_studio_destroy(OmStudio *st)
{
	if (!st)
		return;
	for (int e = 0; e < st->engines; e++)
		st->engine[e].cls->destroy(st->engine[e].self);
	for (int b = 0; b < 2; b++)
		om_fx_destroy(st->bus[b].fx);
	for (int c = 0; c < st->channels; c++)
		om_filter_destroy(st->channel[c].filter);
	free(st);
}

int om_studio_add_engine(OmStudio *st, const OmEngineClass *cls)
{
	if (st->engines >= OM_MAX_ENGINES || cls->stems > OM_MAX_STEMS ||
	    (cls->block && cls->block != OM_MAX_BLOCK) || st->channels + cls->stems > OM_MAX_CHANNELS)
		return -1;
	void *self = cls->create(st->rate);
	if (!self)
		return -1;
	Engine *en = &st->engine[st->engines];
	memset(en, 0, sizeof(*en));
	en->cls = cls;
	en->self = self;
	for (uint8_t s = 0; s < cls->stems; s++)
	{
		const uint8_t c = st->channels++;
		Channel *ch = &st->channel[c];
		memset(ch, 0, sizeof(*ch));
		ch->engine = st->engines;
		ch->stem = s;
		ch->filter = om_filter_create(st->rate);
		if (!ch->filter)
		{
			st->channels--;
			cls->destroy(self);
			return -1;
		}
		ch->value[OM_MIX_CUTOFF] = 127;
		om_studio_mix(st, c, OM_MIX_DUCK_RELEASE, 62);   // 150 ms (registry/mix.json)
		om_studio_mix(st, c, OM_MIX_GATE_SMOOTH, 47);    // 3 ms
		ch->value[OM_MIX_GATE_STEPS1] = ch->value[OM_MIX_GATE_STEPS2] = 21;   // the 8ths: x.x.x.x.x.x.x.x.
		ch->value[OM_MIX_GATE_STEPS3] = 5;
		ch->gate = 1.f;
		om_studio_mix(st, c, OM_MIX_LEVEL, 100);
		om_studio_mix(st, c, OM_MIX_PAN, 64);
		ch->left.now = ch->left.to;
		ch->right.now = ch->right.to;
	}
	cls->set_tempo(self, st->bpm);
	return st->engines++;
}

void om_studio_set_tempo(OmStudio *st, float bpm)
{
	st->bpm = bpm;
	for (int e = 0; e < st->engines; e++)
		st->engine[e].cls->set_tempo(st->engine[e].self, bpm);
	for (int b = 0; b < 2; b++)
		om_fx_set_tempo(st->bus[b].fx, bpm);
}

float om_studio_tempo(const OmStudio *st)
{
	return st->bpm;
}

uint64_t om_studio_frame(const OmStudio *st)
{
	return st->frame;
}

static int push(OmStudio *st, uint8_t engine, Event ev)
{
	if (engine >= st->engines)
		return -1;
	Engine *en = &st->engine[engine];
	if (ev.part >= en->cls->parts || en->count >= OM_MAX_EVENTS)
		return -1;
	// insertion sort from the back: events usually arrive in time order
	uint32_t i = en->count;
	while (i > 0 && en->queue[(en->head + i - 1) % OM_MAX_EVENTS].frame > ev.frame)
	{
		en->queue[(en->head + i) % OM_MAX_EVENTS] = en->queue[(en->head + i - 1) % OM_MAX_EVENTS];
		i--;
	}
	en->queue[(en->head + i) % OM_MAX_EVENTS] = ev;
	en->count++;
	return 0;
}

// Notes are MIDI notes: one above 127 could never be ended by a panic (the
// fuzz found VA holding one), so it is refused, as a velocity above 127 is.
int om_studio_note_on(OmStudio *st, uint64_t frame, uint8_t engine, uint8_t part, uint8_t note, uint8_t velocity)
{
	if (note > 127 || velocity > 127)
		return -1;
	return push(st, engine, (Event){.frame = frame, .type = EV_NOTE_ON, .part = part, .note = note, .value = velocity});
}

int om_studio_note_off(OmStudio *st, uint64_t frame, uint8_t engine, uint8_t part, uint8_t note)
{
	if (note > 127)
		return -1;
	return push(st, engine, (Event){.frame = frame, .type = EV_NOTE_OFF, .part = part, .note = note});
}

int om_studio_set_param(OmStudio *st, uint64_t frame, uint8_t engine, uint8_t part, uint16_t param, uint8_t value)
{
	return push(st, engine, (Event){.frame = frame, .type = EV_PARAM, .part = part, .param = param, .value = value});
}

void om_studio_cancel_after(OmStudio *st, uint64_t frame)
{
	for (uint32_t e = 0; e < st->engines; e++)
	{
		Engine *en = &st->engine[e];
		uint32_t kept = 0;
		for (uint32_t i = 0; i < en->count; i++)
		{
			const Event ev = en->queue[(en->head + i) % OM_MAX_EVENTS];
			const int later = ev.frame > frame;
			if (later && (ev.type == EV_NOTE_ON || (ev.type == EV_CLOCK && ev.note == OM_CLOCK_TICK)))
				continue;
			en->queue[(en->head + kept++) % OM_MAX_EVENTS] = ev;
		}
		en->count = kept;
	}
	uint32_t kept = 0;   // the gates' clock too
	for (uint32_t i = 0; i < st->clockCount; i++)
	{
		const ClockEvent ev = st->clock[(st->clockHead + i) % CLOCK_QUEUE];
		if (ev.frame > frame && ev.event == OM_CLOCK_TICK)
			continue;
		st->clock[(st->clockHead + kept++) % CLOCK_QUEUE] = ev;
	}
	st->clockCount = kept;
}

uint8_t om_studio_get_param(OmStudio *st, uint8_t engine, uint8_t part, uint16_t param)
{
	if (engine >= st->engines || part >= st->engine[engine].cls->parts)
		return 0;
	return st->engine[engine].cls->get_param(st->engine[engine].self, part, param);
}

const OmEngineClass *om_studio_engine(const OmStudio *st, uint8_t engine)
{
	return engine < st->engines ? st->engine[engine].cls : NULL;
}

void om_studio_clock(OmStudio *st, uint64_t frame, uint8_t event)
{
	if (st->clockCount < CLOCK_QUEUE)   // the gates' copy, sorted as the engines' queues are
	{
		uint32_t i = st->clockCount++;
		while (i > 0 && st->clock[(st->clockHead + i - 1) % CLOCK_QUEUE].frame > frame)
		{
			st->clock[(st->clockHead + i) % CLOCK_QUEUE] = st->clock[(st->clockHead + i - 1) % CLOCK_QUEUE];
			i--;
		}
		st->clock[(st->clockHead + i) % CLOCK_QUEUE] = (ClockEvent){frame, event};
	}
	for (uint8_t e = 0; e < st->engines; e++)
		if (st->engine[e].cls->clock)
			push(st, e, (Event){.frame = frame, .type = EV_CLOCK, .note = event});
}

// ---- the mixer ----

uint8_t om_studio_channels(const OmStudio *st)
{
	return st->channels;
}

int om_studio_channel(const OmStudio *st, uint8_t engine, uint8_t stem)
{
	for (int c = 0; c < st->channels; c++)
		if (st->channel[c].engine == engine && st->channel[c].stem == stem)
			return c;
	return -1;
}

// Where every channel's gains go, after a change (solo affects them all).
static void retarget(OmStudio *st)
{
	uint8_t anySolo = 0;
	for (int c = 0; c < st->channels; c++)
		anySolo |= st->channel[c].value[OM_MIX_SOLO];
	for (int c = 0; c < st->channels; c++)
	{
		Channel *ch = &st->channel[c];
		const uint8_t *v = ch->value;
		const float level = v[OM_MIX_MUTE] || (anySolo && !v[OM_MIX_SOLO]) ? 0.f : fader(v[OM_MIX_LEVEL]);
		// balance: the far side is turned down, the near side stays
		float b = (v[OM_MIX_PAN] - 64) / 63.f;
		b = b < -1.f ? -1.f : b > 1.f ? 1.f : b;
		ch->left.to = level * (b > 0.f ? 1.f - b : 1.f);
		ch->right.to = level * (b < 0.f ? 1.f + b : 1.f);
		ch->sendA.to = level * fader(v[OM_MIX_SEND_A]);   // after the fader, before the pan
		ch->sendB.to = level * fader(v[OM_MIX_SEND_B]);
	}
}

void om_studio_mix(OmStudio *st, uint8_t target, uint8_t param, uint8_t value)
{
	if (target == OM_MIX_MASTER)
	{
		if (param == OM_MIX_LEVEL)
		{
			st->masterLevel = value > 127 ? 127 : value;
			st->master.to = fader(st->masterLevel);
		}
		return;
	}
	if (target == OM_MIX_BUS_A || target == OM_MIX_BUS_B)
	{
		Bus *bus = &st->bus[target - OM_MIX_BUS_A];
		if (param == OM_MIX_FX_TYPE && value < OM_FX_TYPES)
		{
			om_fx_set_tempo(bus->fx, st->bpm);
			bus->type = om_fx_set_type(bus->fx, value) == 0 ? value : OM_FX_OFF;
		}
		else if (param == OM_MIX_RETURN)
		{
			bus->ret = value > 127 ? 127 : value;
			bus->gain.to = fader(bus->ret);
		}
		return;
	}
	const int isChannelParam = param <= OM_MIX_SOLO || (param >= OM_MIX_FILTER && param <= OM_MIX_LAST);
	if (target >= st->channels || !isChannelParam)
		return;
	Channel *ch = &st->channel[target];
	const uint8_t max = param == OM_MIX_MUTE || param == OM_MIX_SOLO ? 1 : param == OM_MIX_FILTER ? OM_FILTER_TYPES - 1
		: param == OM_MIX_DUCK_KEY ? OM_MAX_CHANNELS - 1 : param == OM_MIX_GATE_STEPS3 ? 15
		: param == OM_MIX_GATE_STEPS1 || param == OM_MIX_GATE_STEPS2 ? 63 : 127;
	ch->value[param] = value > max ? max : value;
	if (param == OM_MIX_DUCK_RELEASE)   // 20 ms .. 1.28 s (registry "exp")
		ch->duckRel = expf(-1.f / (0.02f * exp2f(ch->value[param] * 6.f / 127.f) * st->rate));
	else if (param == OM_MIX_GATE_SMOOTH)   // 0.5 .. 64 ms
		ch->gateCoef = 1.f - expf(-1.f / (0.0005f * exp2f(ch->value[param] * 7.f / 127.f) * st->rate));
	else if (param >= OM_MIX_DUCK)
		return;
	else if (param >= OM_MIX_FILTER)   // cutoff: semitones from A4, -60 .. 70 (registry "exp", 13.75 Hz up)
		om_filter_set(ch->filter, ch->value[OM_MIX_FILTER], -60.f + ch->value[OM_MIX_CUTOFF] * (130.f / 127.f),
		              ch->value[OM_MIX_RESONANCE] / 127.f);
	else
		retarget(st);
}

uint8_t om_studio_mix_value(const OmStudio *st, uint8_t target, uint8_t param)
{
	if (target == OM_MIX_MASTER)
		return param == OM_MIX_LEVEL ? st->masterLevel : 0;
	if (target == OM_MIX_BUS_A || target == OM_MIX_BUS_B)
	{
		const Bus *bus = &st->bus[target - OM_MIX_BUS_A];
		return param == OM_MIX_FX_TYPE ? bus->type : param == OM_MIX_RETURN ? bus->ret : 0;
	}
	return target < st->channels && param <= OM_MIX_LAST && (param <= OM_MIX_SOLO || param >= OM_MIX_FILTER)
		? st->channel[target].value[param] : 0;
}

void om_studio_fx(OmStudio *st, uint8_t bus, uint16_t param, uint8_t value)
{
	if (bus < 2)
		om_fx_set_param(st->bus[bus].fx, param, value);
}

uint8_t om_studio_fx_value(const OmStudio *st, uint8_t bus, uint16_t param)
{
	return bus < 2 ? om_fx_param(st->bus[bus].fx, param) : 0;
}

uint32_t om_studio_meters(OmStudio *st, float *out, uint32_t max)
{
	uint32_t n = 0;
	for (int c = 0; c < st->channels && n < max; c++, n++)
	{
		out[n] = st->channel[c].peak;
		st->channel[c].peak = 0.f;
	}
	for (int b = 0; b < 2 && n < max; b++, n++)
	{
		out[n] = st->bus[b].peak;
		st->bus[b].peak = 0.f;
	}
	const float master[3] = {st->peakL, st->peakR, st->lowestGain};
	for (int i = 0; i < 3 && n < max; i++, n++)
		out[n] = master[i];
	st->peakL = st->peakR = 0.f;
	st->lowestGain = 1.f;
	return n;
}

void om_studio_dc_blocker(OmStudio *st, int on)
{
	st->dcOn = on != 0;
	st->dcIn[0] = st->dcIn[1] = st->dcOut[0] = st->dcOut[1] = 0.f;
}

// Adds src * gain (moving from gain->now to gain->to over the block) to dst.
static float addRamped(float *dst, const float *src, Gain *g)
{
	float peak = 0.f;
	const float step = (g->to - g->now) / OM_MAX_BLOCK;
	if (g->now == 0.f && step == 0.f)
		return 0.f;
	for (int i = 0; i < OM_MAX_BLOCK; i++)
	{
		const float x = src[i] * (g->now + step * (i + 1));
		dst[i] += x;
		peak = fmaxf(peak, fabsf(x));
	}
	g->now = g->to;
	return peak;
}

// The master: its level, a DC blocker (5 Hz), then the limiter (LOOKAHEAD frames late).
static void master(OmStudio *st, float *l, float *r)
{
	const float step = (st->master.to - st->master.now) / OM_MAX_BLOCK;
	const float attack = 1.f - expf(-5.f / LOOKAHEAD);   // within the lookahead
	for (int i = 0; i < OM_MAX_BLOCK; i++)
	{
		const float g = st->master.now + step * (i + 1);
		float x[2] = {l[i] * g, r[i] * g};
		for (int c = 0; c < 2 && st->dcOn; c++)
		{
			const float y = x[c] - st->dcIn[c] + st->dcPole * st->dcOut[c];
			st->dcIn[c] = x[c];
			st->dcOut[c] = y;
			x[c] = y;
		}
		const float xl = x[0], xr = x[1];
		st->peakL = fmaxf(st->peakL, fabsf(xl));
		st->peakR = fmaxf(st->peakR, fabsf(xr));
		// the gain this frame needs, held for as long as it is in the delay
		const float peak = fmaxf(fabsf(xl), fabsf(xr));
		const float need = peak > LIMIT ? LIMIT / peak : 1.f;
		if (need <= st->hold)
		{
			st->hold = need;
			st->holdLeft = LOOKAHEAD;
		}
		else if (st->holdLeft)
			st->holdLeft--;
		else
			st->hold = need;
		st->env += (st->hold - st->env) * (st->hold < st->env ? attack : st->release);
		st->lowestGain = fminf(st->lowestGain, st->env);
		// out: the frame from LOOKAHEAD frames ago
		const float dl = st->delay[0][st->delayPos], dr = st->delay[1][st->delayPos];
		st->delay[0][st->delayPos] = xl;
		st->delay[1][st->delayPos] = xr;
		st->delayPos = (st->delayPos + 1) % LOOKAHEAD;
		l[i] = fmaxf(-LIMIT, fminf(LIMIT, dl * st->env));
		r[i] = fmaxf(-LIMIT, fminf(LIMIT, dr * st->env));
	}
	st->master.now = st->master.to;
}

// Applies the engine's queued events up to (not including) frame `until`.
static void dispatch(Engine *en, uint64_t until)
{
	while (en->count && en->queue[en->head].frame < until)
	{
		const Event *ev = &en->queue[en->head];
		switch (ev->type)
		{
		case EV_NOTE_ON:  en->cls->note_on(en->self, ev->part, ev->note, ev->value); break;
		case EV_NOTE_OFF: en->cls->note_off(en->self, ev->part, ev->note); break;
		case EV_PARAM:    en->cls->set_param(en->self, ev->part, ev->param, ev->value); break;
		case EV_CLOCK:    en->cls->clock(en->self, ev->note); break;
		}
		en->head = (en->head + 1) % OM_MAX_EVENTS;
		en->count--;
	}
}

static void renderEngine(Engine *en, uint64_t start)
{
	const uint64_t end = start + OM_MAX_BLOCK;
	float *stems[2 * OM_MAX_STEMS];
	if (en->cls->block)
	{
		dispatch(en, end);
		for (int c = 0; c < 2 * en->cls->stems; c++)
			stems[c] = en->buf[c];
		en->cls->render(en->self, stems, OM_MAX_BLOCK);
		return;
	}
	uint64_t pos = start;
	while (pos < end)
	{
		dispatch(en, pos + 1);   // everything due at or before this frame
		uint64_t next = end;
		if (en->count && en->queue[en->head].frame < end)
			next = en->queue[en->head].frame;
		const uint32_t n = (uint32_t)(next - pos);
		for (int c = 0; c < 2 * en->cls->stems; c++)
			stems[c] = en->buf[c] + (pos - start);
		en->cls->render(en->self, stems, n);
		pos = next;
	}
}

// The clock for this block's frames: each frame's gate step (a 16th: 6
// ticks, 16 to the bar). The tracker's ticks set it; between them, and when
// nothing plays, it runs on at the tempo.
static void clockBlock(OmStudio *st)
{
	const double perFrame = st->bpm * 24.0 / 60.0 / st->rate;
	for (int i = 0; i < OM_MAX_BLOCK; i++)
	{
		const uint64_t frame = st->frame + (uint64_t)i;
		int ticked = 0;
		while (st->clockCount && st->clock[st->clockHead].frame <= frame)
		{
			const ClockEvent ev = st->clock[st->clockHead];
			st->clockHead = (st->clockHead + 1) % CLOCK_QUEUE;
			st->clockCount--;
			if (ev.event == OM_CLOCK_START) { st->tickCount = 0; st->ticks = 0.0; }
			else if (ev.event == OM_CLOCK_TICK) { st->ticks = st->tickCount++; ticked = 1; }
		}
		if (!ticked)
			st->ticks += perFrame;
		st->step[i] = (uint8_t)((uint64_t)(st->ticks / 6.0) & 15);
	}
}

// The trance gate: the steps that are off cut the channel by GATE, smoothed.
static void gate(OmStudio *st, Channel *ch, float *sl, float *sr)
{
	const uint8_t *v = ch->value;
	if (!v[OM_MIX_GATE])
	{
		ch->gate = 1.f;
		return;
	}
	const uint32_t steps = (uint32_t)v[OM_MIX_GATE_STEPS1] | (uint32_t)v[OM_MIX_GATE_STEPS2] << 6 | (uint32_t)v[OM_MIX_GATE_STEPS3] << 12;
	const float closed = 1.f - v[OM_MIX_GATE] / 127.f;
	float g = ch->gate;
	for (int i = 0; i < OM_MAX_BLOCK; i++)
	{
		const float to = (steps >> st->step[i]) & 1 ? 1.f : closed;
		g += (to - g) * ch->gateCoef;
		sl[i] *= g;
		sr[i] *= g;
	}
	ch->gate = g;
}

// Sidechain ducking: the channel dips as its key channel (after its filter,
// before its fader) gets loud, by up to the DUCK amount, and comes back with
// the RELEASE. A channel keys from its own key, never itself. Skipped at DUCK 0.
static void duck(OmStudio *st, int c, float *sl, float *sr)
{
	Channel *ch = &st->channel[c];
	const int key = ch->value[OM_MIX_DUCK_KEY];
	if (!ch->value[OM_MIX_DUCK] || key == c || key >= st->channels)
	{
		ch->duck = 0.f;
		return;
	}
	const Channel *k = &st->channel[key];
	const Engine *ke = &st->engine[k->engine];
	const float *kl = ke->buf[2 * k->stem], *kr = ke->buf[2 * k->stem + 1];
	const float depth = ch->value[OM_MIX_DUCK] / 127.f;
	float env = ch->duck;
	for (int i = 0; i < OM_MAX_BLOCK; i++)
	{
		float x = fmaxf(fabsf(kl[i]), fabsf(kr[i])) * (1.f / DUCK_FULL);
		x = x > 1.f ? 1.f : x;
		env = x > env ? env + (x - env) * DUCK_ATTACK : env * ch->duckRel;
		const float g = 1.f - depth * env;
		sl[i] *= g;
		sr[i] *= g;
	}
	ch->duck = env;
}

void om_studio_render(OmStudio *st, float *left, float *right, uint32_t frames)
{
	for (uint32_t done = 0; done < frames; done += OM_MAX_BLOCK)
	{
		float *l = left + done, *r = right + done;
		memset(l, 0, OM_MAX_BLOCK * sizeof(float));
		memset(r, 0, OM_MAX_BLOCK * sizeof(float));
		for (int b = 0; b < 2; b++)
			memset(st->bus[b].buf, 0, sizeof(st->bus[b].buf));
		for (int e = 0; e < st->engines; e++)
			renderEngine(&st->engine[e], st->frame);
		// every channel's inserts first, the filter then the gate: a key is heard after them
		clockBlock(st);
		for (int c = 0; c < st->channels; c++)
		{
			Channel *ch = &st->channel[c];
			Engine *en = &st->engine[ch->engine];
			om_filter_process(ch->filter, en->buf[2 * ch->stem], en->buf[2 * ch->stem + 1], OM_MAX_BLOCK);
			gate(st, ch, en->buf[2 * ch->stem], en->buf[2 * ch->stem + 1]);
		}
		for (int c = 0; c < st->channels; c++)
		{
			Channel *ch = &st->channel[c];
			Engine *en = &st->engine[ch->engine];
			float *sl = en->buf[2 * ch->stem], *sr = en->buf[2 * ch->stem + 1];
			duck(st, c, sl, sr);
			Gain a = ch->sendA, b = ch->sendB;   // both sides of a send ramp alike
			addRamped(st->bus[0].buf[0], sl, &ch->sendA);
			addRamped(st->bus[0].buf[1], sr, &a);
			addRamped(st->bus[1].buf[0], sl, &ch->sendB);
			addRamped(st->bus[1].buf[1], sr, &b);
			ch->peak = fmaxf(ch->peak, fmaxf(addRamped(l, sl, &ch->left), addRamped(r, sr, &ch->right)));
		}
		for (int b = 0; b < 2; b++)
		{
			Bus *bus = &st->bus[b];
			if (bus->type == OM_FX_OFF)
				continue;
			om_fx_process(bus->fx, bus->buf[0], bus->buf[1], OM_MAX_BLOCK);
			Gain g = bus->gain;
			bus->peak = fmaxf(bus->peak, fmaxf(addRamped(l, bus->buf[0], &bus->gain), addRamped(r, bus->buf[1], &g)));
		}
		master(st, l, r);
		st->frame += OM_MAX_BLOCK;
	}
}
