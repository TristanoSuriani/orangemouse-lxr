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

#define QUEUE 1024
#define TICKS 96             // the tempo: a line through the last 4 beats' ticks
#define MIN_TICKS 24         // one beat before the first reading
#define TEMPO_EVERY 6        // read it every 16th
#define TEMPO_MOVE 0.3f      // BPM: a smaller change is jitter, and would make the delay flutter
#define MIN_BPM 20.f
#define MAX_BPM 400.f

enum { NOTE_OFF = 0x80, NOTE_ON = 0x90, CC = 0xB0, CC_ALL_SOUND_OFF = 120, CC_ALL_NOTES_OFF = 123 };
enum { MIDI_TICK = 0xF8, MIDI_START = 0xFA, MIDI_CONTINUE = 0xFB, MIDI_STOP = 0xFC };

typedef struct
{
	uint64_t frame;
	uint8_t b[3];
} Message;

typedef struct
{
	uint8_t engine, part;
	uint8_t held[128];
} Route;

struct OmMidiIn
{
	OmStudio *st;
	float rate;
	void (*raw)(uint8_t);
	Route route[OM_MIDI_CHANNELS];
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
		m->route[c].engine = OM_MIDI_OFF;
	return m;
}

void om_midi_in_destroy(OmMidiIn *m)
{
	free(m);
}

static void endNotes(OmMidiIn *m, uint64_t frame, int channel)
{
	Route *r = &m->route[channel];
	for (int n = 0; n < 128; n++)
		if (r->held[n])
		{
			r->held[n] = 0;
			if (r->engine != OM_MIDI_OFF && r->engine != OM_MIDI_RAW)
				om_studio_note_off(m->st, frame, r->engine, r->part, (uint8_t)n);
		}
}

static void endAllNotes(OmMidiIn *m, uint64_t frame)
{
	for (int c = 0; c < OM_MIDI_CHANNELS; c++)
		endNotes(m, frame, c);
}

void om_midi_in_route(OmMidiIn *m, uint8_t channel, uint8_t engine, uint8_t part)
{
	if (channel >= OM_MIDI_CHANNELS)
		return;
	endNotes(m, om_studio_frame(m->st), channel);
	m->route[channel].engine = engine;
	m->route[channel].part = part;
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
	if (r->engine == OM_MIDI_OFF)
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

static void apply(OmMidiIn *m, const Message *q)
{
	const uint8_t status = q->b[0];
	if (status >= MIDI_TICK)
		clockMessage(m, q->frame, status);
	else if (status >= 0x80 && status < 0xA0)
		note(m, q->frame, q->b);
	else
	{
		if ((status & 0xf0) == CC && (q->b[1] == CC_ALL_NOTES_OFF || q->b[1] == CC_ALL_SOUND_OFF))
			endNotes(m, q->frame, status & 0x0f);
		toRaw(m, q->b);
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
	m->everTicked = 0;
	m->tickCount = m->tickPos = m->sinceTempo = 0;
	m->applied = 0.f;
	om_studio_set_tempo(m->st, m->tempo);
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
