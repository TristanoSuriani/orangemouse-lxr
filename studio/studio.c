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
#include <stdlib.h>
#include <string.h>
#include "studio.h"

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
	float level[OM_MAX_STEMS], balance[OM_MAX_STEMS];
	float buf[2 * OM_MAX_STEMS][OM_MAX_BLOCK];
} Engine;

struct OmStudio
{
	float rate, bpm;
	uint64_t frame;
	uint8_t engines;
	Engine engine[OM_MAX_ENGINES];
};

OmStudio *om_studio_create(float sampleRate)
{
	OmStudio *st = calloc(1, sizeof(OmStudio));
	if (st)
	{
		st->rate = sampleRate;
		st->bpm = 120.f;
	}
	return st;
}

void om_studio_destroy(OmStudio *st)
{
	if (!st)
		return;
	for (int e = 0; e < st->engines; e++)
		st->engine[e].cls->destroy(st->engine[e].self);
	free(st);
}

int om_studio_add_engine(OmStudio *st, const OmEngineClass *cls)
{
	if (st->engines >= OM_MAX_ENGINES || cls->stems > OM_MAX_STEMS ||
	    (cls->block && cls->block != OM_MAX_BLOCK))
		return -1;
	void *self = cls->create(st->rate);
	if (!self)
		return -1;
	Engine *en = &st->engine[st->engines];
	memset(en, 0, sizeof(*en));
	en->cls = cls;
	en->self = self;
	for (int s = 0; s < OM_MAX_STEMS; s++)
		en->level[s] = 1.f;
	cls->set_tempo(self, st->bpm);
	return st->engines++;
}

void om_studio_set_tempo(OmStudio *st, float bpm)
{
	st->bpm = bpm;
	for (int e = 0; e < st->engines; e++)
		st->engine[e].cls->set_tempo(st->engine[e].self, bpm);
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

void om_studio_set_stem(OmStudio *st, uint8_t engine, uint8_t stem, float level, float balance)
{
	if (engine >= st->engines || stem >= OM_MAX_STEMS)
		return;
	st->engine[engine].level[stem] = level;
	st->engine[engine].balance[stem] = balance < -1.f ? -1.f : balance > 1.f ? 1.f : balance;
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
		for (int e = 0; e < st->engines; e++)
		{
			Engine *en = &st->engine[e];
			renderEngine(en, st->frame);
			for (int s = 0; s < en->cls->stems; s++)
			{
				// balance: the far side is turned down, the near side stays
				const float b = en->balance[s];
				const float gl = en->level[s] * (b > 0.f ? 1.f - b : 1.f);
				const float gr = en->level[s] * (b < 0.f ? 1.f + b : 1.f);
				const float *sl = en->buf[2 * s], *sr = en->buf[2 * s + 1];
				for (int i = 0; i < OM_MAX_BLOCK; i++)
				{
					l[i] += sl[i] * gl;
					r[i] += sr[i] * gr;
				}
			}
		}
		st->frame += OM_MAX_BLOCK;
	}
}
