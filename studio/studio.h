/*
 * studio.h
 *
 * The Orange Mouse studio's audio core (PLAN.md Phase 0, step 5; see
 * docs/architecture-v2.svg): the engines, the router that delivers notes and
 * parameter changes to them at exact frames, and the mixer that sums their
 * stems. It runs on the audio thread: nothing here allocates after creation.
 *
 * Times are frames since the studio was created. Events for a frame already
 * rendered take effect at the start of the next render.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef OM_STUDIO_H_
#define OM_STUDIO_H_

#include <stdint.h>
#include "engine.h"

#define OM_MAX_ENGINES 8
#define OM_MAX_STEMS   8     // per engine
#define OM_MAX_EVENTS  1024  // queued per engine

typedef struct OmStudio OmStudio;

OmStudio *om_studio_create(float sampleRate);
void om_studio_destroy(OmStudio *st);

// Adds an engine; returns its index, or -1 (too many, or it cannot be created).
int om_studio_add_engine(OmStudio *st, const OmEngineClass *cls);

void om_studio_set_tempo(OmStudio *st, float bpm);
float om_studio_tempo(const OmStudio *st);

// The router. Each returns 0, or -1 if the engine's queue is full or the
// engine or part does not exist.
int om_studio_note_on(OmStudio *st, uint64_t frame, uint8_t engine, uint8_t part, uint8_t note, uint8_t velocity);
int om_studio_note_off(OmStudio *st, uint64_t frame, uint8_t engine, uint8_t part, uint8_t note);
int om_studio_set_param(OmStudio *st, uint64_t frame, uint8_t engine, uint8_t part, uint16_t param, uint8_t value);

// The mixer: a stem's level (linear) and balance (-1 left .. 1 right).
void om_studio_set_stem(OmStudio *st, uint8_t engine, uint8_t stem, float level, float balance);

// Renders `frames` frames (a multiple of OM_MAX_BLOCK) of the stereo mix.
void om_studio_render(OmStudio *st, float *left, float *right, uint32_t frames);

uint64_t om_studio_frame(const OmStudio *st);

#endif /* OM_STUDIO_H_ */
