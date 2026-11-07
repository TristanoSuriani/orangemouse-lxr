/*
 * midi_in.c
 *
 * See midi_in.h.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "midi_in.h"
#include "om_registry_mix.h"

#define QUEUE 1024
#define TICKS 96             // the tempo: a line through the last 4 beats' ticks
#define MIN_TICKS 24         // one beat before the first reading
#define TEMPO_EVERY 6        // read it every 16th
#define TEMPO_MOVE 0.3f      // BPM: a smaller change is jitter, and would make the delay flutter
#define MIN_BPM 20.f
#define MAX_BPM 400.f
#define CHANGES 512          // more than the CC maps' parameters on all parts (226) and the mixer's (16 channels x 20 + 3)

enum { NOTE_OFF = 0x80, NOTE_ON = 0x90, CC = 0xB0, PROGRAM = 0xC0, BEND = 0xE0 };
enum { CC_DATA = 6, CC_DATA_LSB = 38, CC_RPN_LSB = 100, CC_RPN_MSB = 101,
       CC_ALL_SOUND_OFF = 120, CC_RESET_CONTROLLERS = 121, CC_ALL_NOTES_OFF = 123, FIRST_MODE_CC = 120 };
#define BEND_RANGE 2           // semitones, until an RPN 0 says otherwise
#define MAX_BEND_RANGE 24
#define NO_RPN 0x7F
#define WHOLE_PART 0xFFFF      // a change: every parameter of the part (a program)
enum { MIDI_SONG_POSITION = 0xF2, MIDI_TICK = 0xF8, MIDI_START = 0xFA, MIDI_CONTINUE = 0xFB, MIDI_STOP = 0xFC };

typedef struct
{
	uint64_t frame;
	uint8_t b[3];
} Message;

typedef struct
{
	uint8_t engine, part;
	uint8_t ownCcs;      // the engine has a CC map: its CCs stop here
	uint8_t bends;       // the engine takes pitch bend
	uint8_t programs;    // the engine takes program changes
	uint8_t held[128];
} Route;

typedef struct
{
	float bend;          // semitones, as last sent
	uint8_t range;       // the bend's semitones at the wheel's end (RPN 0)
	uint8_t rpnMsb, rpnLsb;
} Controllers;

typedef struct
{
	uint8_t engine, part, value;
	uint16_t param;
} Change;

struct OmMidiIn
{
	OmStudio *st;
	float rate;
	void (*raw)(uint8_t);
	Route route[OM_MIDI_CHANNELS];
	Controllers ctl[OM_MIDI_CHANNELS];
	Message queue[QUEUE];
	uint32_t head, count;
	uint64_t lastFrame;      // the order: no message goes before this

	int running;
	float tempo;             // without a clock
	float applied;           // what the clock last set; 0: nothing yet
	uint64_t ticks[TICKS];
	uint32_t tickCount;      // ticks in the window (up to TICKS)
	uint32_t tickPos;        // the next to write
	uint32_t sinceTempo;
	int everTicked;
	uint64_t lastTick;

	uint32_t late, dropped;
	float lateMs;
	uint16_t activity;

	Change changes[CHANGES]; // what the CCs moved since the panels last asked
	uint32_t changeCount;
};

OmMidiIn *om_midi_in_create(OmStudio *st, float sampleRate, void (*raw)(uint8_t byte))
{
	OmMidiIn *m = calloc(1, sizeof(OmMidiIn));
	if (!m)
		return NULL;
	m->st = st;
	m->rate = sampleRate;
	m->raw = raw;
	m->tempo = om_studio_tempo(st);
	for (int c = 0; c < OM_MIDI_CHANNELS; c++)
	{
		m->route[c].engine = OM_MIDI_OFF;
		m->ctl[c] = (Controllers){0.f, BEND_RANGE, NO_RPN, NO_RPN};
	}
	return m;
}

void om_midi_in_destroy(OmMidiIn *m)
{
	free(m);
}

static int playsNotes(const Route *r)
{
	return r->engine != OM_MIDI_OFF && r->engine != OM_MIDI_RAW && r->engine != OM_MIDI_MIXER;
}

static void endNotes(OmMidiIn *m, uint64_t frame, int channel)
{
	Route *r = &m->route[channel];
	for (int n = 0; n < 128; n++)
		if (r->held[n])
		{
			r->held[n] = 0;
			if (playsNotes(r))
				om_studio_note_off(m->st, frame, r->engine, r->part, (uint8_t)n);
		}
}

static void endAllNotes(OmMidiIn *m, uint64_t frame)
{
	for (int c = 0; c < OM_MIDI_CHANNELS; c++)
		endNotes(m, frame, c);
}

// The part plays in tune again.
static void unbend(OmMidiIn *m, uint64_t frame, int channel)
{
	const Route *r = &m->route[channel];
	if (m->ctl[channel].bend != 0.f && r->bends)
		om_studio_pitch_bend(m->st, frame, r->engine, r->part, 0.f);
	m->ctl[channel].bend = 0.f;
}

void om_midi_in_route(OmMidiIn *m, uint8_t channel, uint8_t engine, uint8_t part)
{
	if (channel >= OM_MIDI_CHANNELS)
		return;
	endNotes(m, om_studio_frame(m->st), channel);
	unbend(m, om_studio_frame(m->st), channel);
	m->route[channel].engine = engine;
	m->route[channel].part = part;
	m->route[channel].ownCcs = 0;
	const OmEngineClass *cls = om_studio_engine(m->st, engine);
	m->route[channel].bends = cls && cls->pitch_bend;
	m->route[channel].programs = cls && cls->program;
	for (uint16_t p = 0; cls && p < cls->numParams; p++)
		if (cls->params[p].cc)
			m->route[channel].ownCcs = 1;
}

static int ticking(const OmMidiIn *m, uint64_t frame)
{
	return m->everTicked && frame < m->lastTick + (uint64_t)m->rate;
}

void om_midi_in_tempo(OmMidiIn *m, float bpm)
{
	if (!(bpm >= MIN_BPM && bpm <= MAX_BPM))
		return;
	m->tempo = bpm;
	if (!ticking(m, om_studio_frame(m->st)))
		om_studio_set_tempo(m->st, bpm);
}

void om_midi_in_event(OmMidiIn *m, uint64_t frame, uint8_t b0, uint8_t b1, uint8_t b2)
{
	if (m->count >= QUEUE)
	{
		m->dropped++;
		return;
	}
	const uint64_t now = om_studio_frame(m->st);
	if (frame < now)
	{
		const float ms = (float)(now - frame) * 1000.f / m->rate;
		m->late++;
		if (ms > m->lateMs)
			m->lateMs = ms;
		frame = now;
	}
	else if (frame > now + (uint64_t)m->rate)   // not a time this input can mean: at once
		frame = now;
	if (frame < m->lastFrame)
		frame = m->lastFrame;
	m->lastFrame = frame;
	Message *q = &m->queue[(m->head + m->count++) % QUEUE];
	q->frame = frame;
	q->b[0] = b0;
	q->b[1] = b1 & 0x7f;
	q->b[2] = b2 & 0x7f;
}

// How many bytes the message has, for the raw sink; 0: not for the firmware.
static int rawLength(uint8_t status)
{
	if (status < 0x80)
		return 0;
	if (status < 0xF0)
		return (status & 0xE0) == 0xC0 ? 2 : 3;   // program change, channel pressure: 2
	switch (status)
	{
	case 0xF1:
	case 0xF3:
		return 2;
	case 0xF2:
		return 3;
	case 0xF6:
		return 1;
	default:
		return 0;   // system exclusive, active sensing, reset: nothing it uses
	}
}

static void toRaw(const OmMidiIn *m, const uint8_t *b)
{
	const int n = rawLength(b[0]);
	if (m->raw)
		for (int i = 0; i < n; i++)
			m->raw(b[i]);
}

// The tempo: the slope of a line through the window's ticks, which a jittery
// clock moves far less than the time between its first and last tick would.
static void measureTempo(OmMidiIn *m, uint64_t frame)
{
	if (m->everTicked && frame > m->lastTick + (uint64_t)(m->rate / 2))   // under 5 BPM: a gap, not a tempo
		m->tickCount = m->tickPos = m->sinceTempo = 0;
	m->ticks[m->tickPos] = frame;
	m->tickPos = (m->tickPos + 1) % TICKS;
	if (m->tickCount < TICKS)
		m->tickCount++;
	m->everTicked = 1;
	m->lastTick = frame;
	if (m->tickCount < MIN_TICKS || ++m->sinceTempo < TEMPO_EVERY)
		return;
	m->sinceTempo = 0;

	const uint32_t oldest = (m->tickPos + TICKS - m->tickCount) % TICKS;
	const uint64_t origin = m->ticks[oldest];
	const double n = m->tickCount, meanX = (n - 1.0) / 2.0;
	double meanY = 0.0;
	for (uint32_t i = 0; i < m->tickCount; i++)
		meanY += (double)(m->ticks[(oldest + i) % TICKS] - origin);
	meanY /= n;
	double top = 0.0, bottom = 0.0;
	for (uint32_t i = 0; i < m->tickCount; i++)
	{
		const double x = (double)i - meanX;
		top += x * ((double)(m->ticks[(oldest + i) % TICKS] - origin) - meanY);
		bottom += x * x;
	}
	if (top <= 0.0)
		return;
	const float bpm = (float)((double)m->rate * 60.0 / 24.0 * bottom / top);
	if (bpm < MIN_BPM || bpm > MAX_BPM || (m->applied > 0.f && fabsf(bpm - m->applied) < TEMPO_MOVE))
		return;
	m->applied = roundf(bpm * 10.f) / 10.f;
	om_studio_set_tempo(m->st, m->applied);
}

static void clockMessage(OmMidiIn *m, uint64_t frame, uint8_t status)
{
	switch (status)
	{
	case MIDI_TICK:
		measureTempo(m, frame);
		if (m->running)
			om_studio_clock(m->st, frame, OM_CLOCK_TICK);
		break;
	case MIDI_START:
		if (m->running)
			om_studio_clock(m->st, frame, OM_CLOCK_STOP);
		om_studio_clock(m->st, frame, OM_CLOCK_START);
		m->running = 1;
		break;
	case MIDI_CONTINUE:
		if (!m->running)
			om_studio_clock(m->st, frame, OM_CLOCK_CONTINUE);
		m->running = 1;
		break;
	case MIDI_STOP:
		if (m->running)
			om_studio_clock(m->st, frame, OM_CLOCK_STOP);
		m->running = 0;
		endAllNotes(m, frame);
		break;
	}
}

static void note(OmMidiIn *m, uint64_t frame, const uint8_t *b)
{
	const int channel = b[0] & 0x0f;
	const int on = (b[0] & 0xf0) == NOTE_ON && b[2] > 0;
	Route *r = &m->route[channel];
	if (on)
		m->activity |= (uint16_t)(1u << channel);
	if (r->engine == OM_MIDI_OFF || r->engine == OM_MIDI_MIXER)
		return;
	if (r->engine == OM_MIDI_RAW)
	{
		toRaw(m, b);
		return;
	}
	if (on)
	{
		if (om_studio_note_on(m->st, frame, r->engine, r->part, b[1], b[2]) == 0)
			r->held[b[1]] = 1;
	}
	else if (r->held[b[1]])
	{
		r->held[b[1]] = 0;
		om_studio_note_off(m->st, frame, r->engine, r->part, b[1]);
	}
}

// A CC's 128 values shared evenly over the parameter's range, so 0 is its
// minimum, 127 its maximum, and a parameter of a few values takes equal slices.
static uint8_t scaled(const OmParamInfo *p, uint8_t cc)
{
	const unsigned range = (unsigned)p->max - p->min + 1u;
	return (uint8_t)(p->min + cc * range / 128u);
}

static void moveParam(OmMidiIn *m, uint64_t frame, const Route *r, uint8_t cc, uint8_t value)
{
	const OmEngineClass *cls = om_studio_engine(m->st, r->engine);
	for (uint16_t p = 0; p < cls->numParams; p++)
	{
		if (cls->params[p].cc != cc)
			continue;
		const uint8_t v = scaled(&cls->params[p], value);
		// A repeated value does nothing, so a CC that moves within one slice of a
		// parameter of a few values costs nothing. What it was last set to may
		// still be queued: the engine does not have it yet.
		uint32_t i = 0, whole = 0;
		for (uint32_t k = 0; k < m->changeCount; k++)
			if (m->changes[k].engine == r->engine && m->changes[k].part == r->part)
			{
				if (m->changes[k].param == p)
					i = k + 1;
				whole |= m->changes[k].param == WHOLE_PART;   // a program is queued: the engine's value is not known yet
			}
		const int known = i > 0 || !whole;
		const uint8_t now = i > 0 ? m->changes[i - 1].value : om_studio_get_param(m->st, r->engine, r->part, p);
		i = i > 0 ? i - 1 : m->changeCount;
		if ((known && v == now) || om_studio_set_param(m->st, frame, r->engine, r->part, p, v) != 0)
			return;
		if (i < m->changeCount)
			m->changes[i].value = v;
		else if (m->changeCount < CHANGES)
			m->changes[m->changeCount++] = (Change){r->engine, r->part, v, p};
		return;
	}
}

// A mixer control, from a CC: where it is now, as a parameter's is (moveParam).
static void moveMix(OmMidiIn *m, uint8_t target, uint8_t param, uint8_t value)
{
	const uint8_t v = scaled(&om_mix_params[param], value);
	uint32_t i = 0;
	while (i < m->changeCount && !(m->changes[i].engine == OM_MIDI_MIXER && m->changes[i].part == target && m->changes[i].param == param))
		i++;
	const uint8_t now = i < m->changeCount ? m->changes[i].value : om_studio_mix_value(m->st, target, param);
	if (v == now)
		return;
	om_studio_mix(m->st, target, param, v);
	if (i < m->changeCount)
		m->changes[i].value = v;
	else if (m->changeCount < CHANGES)
		m->changes[m->changeCount++] = (Change){OM_MIDI_MIXER, target, v, param};
}

// On a part's channel: its mixer channel's strip (registry/mix.json "cc"). 0: not one of them.
static int stripCc(OmMidiIn *m, const Route *r, uint8_t cc, uint8_t value)
{
	for (uint8_t p = 0; p < OM_MIX_NUM_PARAMS; p++)
		if (om_mix_params[p].cc == cc)
		{
			const int target = om_studio_channel(m->st, r->engine, r->part);   // these engines have a stem per part
			if (target >= 0)
				moveMix(m, (uint8_t)target, p, value);
			return 1;
		}
	return 0;
}

// On a channel set to MIXER: the grid of the first mixer channels (the LXR's),
// the returns, the master (registry/mix.json "mixer_channel").
static void mixerChannelCc(OmMidiIn *m, uint8_t cc, uint8_t value)
{
	if (cc == OM_MIX_CC_MASTER)
		moveMix(m, OM_MIX_MASTER, OM_MIX_LEVEL, value);
	else if (cc == OM_MIX_CC_RETURN_A || cc == OM_MIX_CC_RETURN_B)
		moveMix(m, cc == OM_MIX_CC_RETURN_A ? OM_MIX_BUS_A : OM_MIX_BUS_B, OM_MIX_RETURN, value);
	else
		for (uint8_t p = 0; p < OM_MIX_NUM_PARAMS; p++)
			if (om_mix_grid_first[p] && cc >= om_mix_grid_first[p] && cc < om_mix_grid_first[p] + OM_MIX_GRID_CHANNELS)
			{
				moveMix(m, (uint8_t)(cc - om_mix_grid_first[p]), p, value);
				return;
			}
}

// A program change: the part's whole sound changes; the panels read all of it.
static void program(OmMidiIn *m, uint64_t frame, const Route *r, uint8_t number)
{
	if (om_studio_program(m->st, frame, r->engine, r->part, number) != 0)
		return;
	uint32_t kept = 0;   // the part's single changes are part of the whole one now
	for (uint32_t k = 0; k < m->changeCount; k++)
		if (!(m->changes[k].engine == r->engine && m->changes[k].part == r->part))   // mixer entries are engine OM_MIDI_MIXER: kept
			m->changes[kept++] = m->changes[k];
	m->changeCount = kept;
	if (m->changeCount < CHANGES)
		m->changes[m->changeCount++] = (Change){r->engine, r->part, 0, WHOLE_PART};
}

// 14 bits, 0x2000 in the middle; both ends reach the range
static void bend(OmMidiIn *m, uint64_t frame, int channel, uint8_t lsb, uint8_t msb)
{
	const int v = ((int)msb << 7 | lsb) - 0x2000;
	Controllers *c = &m->ctl[channel];
	const float s = (float)v / (v > 0 ? 8191.f : 8192.f) * (float)c->range;
	const Route *r = &m->route[channel];
	if (om_studio_pitch_bend(m->st, frame, r->engine, r->part, s) == 0)
		c->bend = s;
}

// The registered parameters a sequencer sets with CC 101, 100 and 6: RPN 0, the
// bend's range in semitones, is the one used.
static int rpn(OmMidiIn *m, int channel, uint8_t cc, uint8_t value)
{
	Controllers *c = &m->ctl[channel];
	switch (cc)
	{
	case CC_RPN_MSB: c->rpnMsb = value; return 1;
	case CC_RPN_LSB: c->rpnLsb = value; return 1;
	case CC_DATA:
		if (c->rpnMsb == 0 && c->rpnLsb == 0)
			c->range = value > MAX_BEND_RANGE ? MAX_BEND_RANGE : value;
		return 1;
	case CC_DATA_LSB: return 1;   // cents: too fine to matter here
	default: return 0;
	}
}

static void apply(OmMidiIn *m, const Message *q)
{
	const uint8_t status = q->b[0];
	if (status >= MIDI_TICK)
		clockMessage(m, q->frame, status);
	else if (status == MIDI_SONG_POSITION)
		om_studio_song_position(m->st, q->frame, (uint16_t)(q->b[1] | q->b[2] << 7));
	else if (status >= 0x80 && status < 0xA0)
		note(m, q->frame, q->b);
	else
	{
		const int channel = status & 0x0f, type = status & 0xf0;
		const Route *r = &m->route[channel];
		if (type == BEND && r->bends)
			bend(m, q->frame, channel, q->b[1], q->b[2]);
		else if (type == PROGRAM && r->programs)
			program(m, q->frame, r, q->b[1]);
		else if (type == CC && r->engine == OM_MIDI_MIXER && q->b[1] < FIRST_MODE_CC)
			mixerChannelCc(m, q->b[1], q->b[2]);
		else if (type == CC && r->ownCcs && q->b[1] < FIRST_MODE_CC)
		{
			if (!rpn(m, channel, q->b[1], q->b[2]) && !stripCc(m, r, q->b[1], q->b[2]))
				moveParam(m, q->frame, r, q->b[1], q->b[2]);
		}
		else
		{
			if (type == CC && (q->b[1] == CC_ALL_NOTES_OFF || q->b[1] == CC_ALL_SOUND_OFF))
				endNotes(m, q->frame, channel);
			if (type == CC && q->b[1] == CC_RESET_CONTROLLERS)
				unbend(m, q->frame, channel);
			toRaw(m, q->b);
		}
	}
}

void om_midi_in_advance(OmMidiIn *m, uint32_t frames)
{
	const uint64_t end = om_studio_frame(m->st) + frames;
	while (m->count && m->queue[m->head].frame < end)
	{
		apply(m, &m->queue[m->head]);
		m->head = (m->head + 1) % QUEUE;
		m->count--;
	}
}

void om_midi_in_reset(OmMidiIn *m)
{
	const uint64_t now = om_studio_frame(m->st);
	m->head = m->count = 0;
	m->lastFrame = now;
	if (m->running)
		om_studio_clock(m->st, now, OM_CLOCK_STOP);
	m->running = 0;
	endAllNotes(m, now);
	for (int c = 0; c < OM_MIDI_CHANNELS; c++)
		unbend(m, now, c);
	m->everTicked = 0;
	m->tickCount = m->tickPos = m->sinceTempo = 0;
	m->applied = 0.f;
	om_studio_set_tempo(m->st, m->tempo);
}

int om_midi_in_change(OmMidiIn *m, OmMidiInChange *out)
{
	if (!m->changeCount)
		return 0;
	const Change *c = &m->changes[--m->changeCount];
	// the engine's own value: it may have held it closer (Dexed's banks)
	const uint8_t value = c->engine == OM_MIDI_MIXER ? om_studio_mix_value(m->st, c->part, (uint8_t)c->param)
		: c->param == WHOLE_PART ? 0 : om_studio_get_param(m->st, c->engine, c->part, c->param);
	*out = (OmMidiInChange){c->engine, c->part, c->param, value};
	return 1;
}

OmMidiInState om_midi_in_state(OmMidiIn *m)
{
	const OmMidiInState s = {
		.running = (uint8_t)m->running,
		.locked = (uint8_t)ticking(m, om_studio_frame(m->st)),
		.bpm = om_studio_tempo(m->st),
		.late = m->late,
		.lateMs = m->lateMs,
		.dropped = m->dropped,
		.activity = m->activity,
	};
	m->activity = 0;
	return s;
}
