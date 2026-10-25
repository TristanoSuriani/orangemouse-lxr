/*
 * engine.h
 *
 * The interface every sound engine of the Orange Mouse studio implements
 * (PLAN.md Phase 0, step 5): the LXR, Orange Mouse VA, and later Plaits and
 * Dexed. The studio (studio/studio.c) creates engines, sends them notes and
 * parameter changes at exact times, and mixes their stems.
 *
 *  - A part is what a tracker track plays: an LXR voice, a VA slot, a Plaits
 *    or Dexed instance.
 *  - A stem is a stereo output. Engines render all their stems at once.
 *  - block: 0 if the engine renders any number of frames, so events land on
 *    their exact sample; otherwise the engine renders in blocks of that size
 *    and the studio applies events at the start of the block they fall in
 *    (the LXR: 64 frames, as on the hardware).
 *  - Parameters are bytes, as in the LXR's and VA's kits. The parameter
 *    registry (registry/<engine>.json) describes them: params[i] is
 *    parameter i, with its names, range and default (param_info.h).
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef OM_ENGINE_H_
#define OM_ENGINE_H_

#include <stdint.h>
#include "param_info.h"

#define OM_MAX_BLOCK 64   // the most frames the studio asks an engine for at once

typedef struct OmEngineClass
{
	const char *name;
	uint8_t parts;
	uint8_t stems;
	uint16_t block;      // 0: any length up to OM_MAX_BLOCK; else exactly this many frames
	const OmParamInfo *params;
	uint16_t numParams;

	// returns the engine's state, or NULL (e.g. a single-instance engine created twice)
	void *(*create)(float sampleRate);
	void (*destroy)(void *self);

	void (*note_on)(void *self, uint8_t part, uint8_t note, uint8_t velocity);
	void (*note_off)(void *self, uint8_t part, uint8_t note);
	void (*set_param)(void *self, uint8_t part, uint16_t param, uint8_t value);
	void (*set_tempo)(void *self, float bpm);

	// renders `frames` frames of every stem: stems[2 * s] left, stems[2 * s + 1]
	// right, written (not added); full scale = 1.0
	void (*render)(void *self, float *const *stems, uint32_t frames);
} OmEngineClass;

#endif /* OM_ENGINE_H_ */
