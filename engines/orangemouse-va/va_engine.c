/*
 * va_engine.c
 *
 * Orange Mouse VA in the studio (engines/engine.h): four parts (the slots),
 * one stem per slot. Notes from the studio are held until their note off;
 * the slot's GATE time applies only to va_noteOn() callers.
 *
 * VaEngine.c keeps its state in globals, so there is one instance.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <string.h>
#include "engine.h"
#include "VaEngine.h"
#include "om_registry_va.h"

static uint8_t va_created;

static void *vaCreate(float sampleRate)
{
	if (va_created)
		return NULL;
	va_created = 1;
	va_init(sampleRate);
	return &va_created;
}

static void vaDestroy(void *self)
{
	(void)self;
	va_allNotesOff(0xff);
	va_created = 0;
}

static void vaNoteOn(void *self, uint8_t part, uint8_t note, uint8_t velocity)
{
	(void)self;
	va_noteOnHeld(part, note, velocity);
}

static void vaNoteOff(void *self, uint8_t part, uint8_t note)
{
	(void)self;
	va_noteOff(part, note);
}

static void vaSetParam(void *self, uint8_t part, uint16_t param, uint8_t value)
{
	(void)self;
	if (param >= OM_VA_NUM_PARAMS)
		return;
	const OmParamInfo *info = &om_va_params[param];
	va_setParam(part, (uint8_t)param, value < info->min ? info->min : value > info->max ? info->max : value);
}

static void vaSetTempo(void *self, float bpm)
{
	(void)self;
	va_setTempo(bpm);
}

static void vaRender(void *self, float *const *stems, uint32_t frames)
{
	(void)self;
	for (uint8_t s = 0; s < VA_NUM_SLOTS; s++)
		if (!va_render(s, stems[2 * s], stems[2 * s + 1], frames))
		{
			memset(stems[2 * s], 0, frames * sizeof(float));
			memset(stems[2 * s + 1], 0, frames * sizeof(float));
		}
}

const OmEngineClass om_va_engine = {
	.name = "Orange Mouse VA",
	.parts = VA_NUM_SLOTS,
	.stems = VA_NUM_SLOTS,
	.block = 0,
	.params = om_va_params,
	.numParams = OM_VA_NUM_PARAMS,
	.create = vaCreate,
	.destroy = vaDestroy,
	.note_on = vaNoteOn,
	.note_off = vaNoteOff,
	.set_param = vaSetParam,
	.set_tempo = vaSetTempo,
	.render = vaRender,
};
