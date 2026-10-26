/*
 * studio.h
 *
 * The Orange Mouse studio's audio core (PLAN.md Phase 0, step 5; see
 * docs/architecture-v2.svg): the engines, the router that delivers notes and
 * parameter changes to them at exact frames, and the mixer (Phase 3).
 *
 * The mixer: every engine stem is a channel (a Surge filter, level, pan, two
 * sends, mute, solo); two send buses each run an effect (fx.h) and return it to the mix;
 * the master has a level and a limiter that keeps the output under -0.2 dBFS
 * (1.5 ms lookahead). Gains move smoothly over a block when they change.
 *
 * It runs on the audio thread: nothing here allocates after creation, except
 * a send effect's engine the first time a bus is set to its type.
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
#define OM_MAX_CHANNELS (OM_MAX_ENGINES * OM_MAX_STEMS)

// the mixer's controls (registry/mix.json)
enum
{
	OM_MIX_LEVEL, OM_MIX_PAN, OM_MIX_SEND_A, OM_MIX_SEND_B, OM_MIX_MUTE, OM_MIX_SOLO, OM_MIX_FX_TYPE, OM_MIX_RETURN,
	OM_MIX_FILTER, OM_MIX_CUTOFF, OM_MIX_RESONANCE
};
// mixer targets besides the channels (0 .. channels - 1)
#define OM_MIX_BUS_A  0xF0
#define OM_MIX_BUS_B  0xF1
#define OM_MIX_MASTER 0xFF
// the limiter's lookahead: the output is this many frames late
#define OM_STUDIO_LATENCY 64

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
// A clock event (OM_CLOCK_*) to every engine that follows the clock.
void om_studio_clock(OmStudio *st, uint64_t frame, uint8_t event);

// A parameter's current value (events still queued are not applied yet), and
// an engine's class (NULL past the last engine).
uint8_t om_studio_get_param(OmStudio *st, uint8_t engine, uint8_t part, uint16_t param);
const OmEngineClass *om_studio_engine(const OmStudio *st, uint8_t engine);

// ---- the mixer ----
// Channels are the engines' stems, in the order the engines were added.
uint8_t om_studio_channels(const OmStudio *st);
int om_studio_channel(const OmStudio *st, uint8_t engine, uint8_t stem);   // -1: none
// A control of a channel (LEVEL..SOLO, FILTER..RESONANCE), a bus (FX_TYPE, RETURN) or the master
// (LEVEL); bytes as in registry/mix.json. Bus A starts as a hall, B a delay.
void om_studio_mix(OmStudio *st, uint8_t target, uint8_t param, uint8_t value);
uint8_t om_studio_mix_value(const OmStudio *st, uint8_t target, uint8_t param);
// A send effect's parameter (fx.h, registry/fx.json); bus 0 is A, 1 is B.
void om_studio_fx(OmStudio *st, uint8_t bus, uint16_t param, uint8_t value);
uint8_t om_studio_fx_value(const OmStudio *st, uint8_t bus, uint16_t param);
// The peaks since the last call: each channel's (after its fader), the two
// buses' returns, the master's left and right (before the limiter), and the
// limiter's lowest gain (1: it did nothing). Returns how many floats it wrote.
uint32_t om_studio_meters(OmStudio *st, float *out, uint32_t max);

// Renders `frames` frames (a multiple of OM_MAX_BLOCK) of the stereo mix.
void om_studio_render(OmStudio *st, float *left, float *right, uint32_t frames);

uint64_t om_studio_frame(const OmStudio *st);

#endif /* OM_STUDIO_H_ */
