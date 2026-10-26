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
	uint8_t value[OM_MIX_RESONANCE + 1];
	OmFilter *filter;
	Gain left, right, sendA, sendB;
	float peak;
} Channel;

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
	float peakL, peakR, lowestGain;
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

int om_studio_note_on(OmStudio *st, uint64_t frame, uint8_t engine, uint8_t part, uint8_t note, uint8_t velocity)
{
	return push(st, engine, (Event){.frame = frame, .type = EV_NOTE_ON, .part = part, .note = note, .value = velocity});
}

int om_studio_note_off(OmStudio *st, uint64_t frame, uint8_t engine, uint8_t part, uint8_t note)
{
	return push(st, engine, (Event){.frame = frame, .type = EV_NOTE_OFF, .part = part, .note = note});
}

int om_studio_set_param(OmStudio *st, uint64_t frame, uint8_t engine, uint8_t part, uint16_t param, uint8_t value)
{
	return push(st, engine, (Event){.frame = frame, .type = EV_PARAM, .part = part, .param = param, .value = value});
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
	const int isChannelParam = param <= OM_MIX_SOLO || (param >= OM_MIX_FILTER && param <= OM_MIX_RESONANCE);
	if (target >= st->channels || !isChannelParam)
		return;
	Channel *ch = &st->channel[target];
	const uint8_t max = param == OM_MIX_MUTE || param == OM_MIX_SOLO ? 1 : param == OM_MIX_FILTER ? OM_FILTER_TYPES - 1 : 127;
	ch->value[param] = value > max ? max : value;
	if (param >= OM_MIX_FILTER)   // cutoff: semitones from A4, -60 .. 70 (registry "exp", 13.75 Hz up)
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
	return target < st->channels && param <= OM_MIX_RESONANCE && (param <= OM_MIX_SOLO || param >= OM_MIX_FILTER)
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

// The master: its level, then the limiter (LOOKAHEAD frames late).
static void master(OmStudio *st, float *l, float *r)
{
	const float step = (st->master.to - st->master.now) / OM_MAX_BLOCK;
	const float attack = 1.f - expf(-5.f / LOOKAHEAD);   // within the lookahead
	for (int i = 0; i < OM_MAX_BLOCK; i++)
	{
		const float g = st->master.now + step * (i + 1);
		const float xl = l[i] * g, xr = r[i] * g;
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
		for (int c = 0; c < st->channels; c++)
		{
			Channel *ch = &st->channel[c];
			Engine *en = &st->engine[ch->engine];
			float *sl = en->buf[2 * ch->stem], *sr = en->buf[2 * ch->stem + 1];
			om_filter_process(ch->filter, sl, sr, OM_MAX_BLOCK);   // the insert, before the fader
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
