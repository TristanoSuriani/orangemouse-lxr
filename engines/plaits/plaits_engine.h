/*
 * plaits_engine.h
 *
 * Plaits in the studio (PLAN.md Phase 5): Mutable Instruments' macro
 * oscillator (third_party/eurorack, MIT), with the engines the four genres
 * use (om_plaits_voice.h). Two parts (PL1, PL2) of 4 voices each, so the
 * tracker's chords play; one stereo stem per part. The gate drives Plaits'
 * trigger and, through an attack envelope and the velocity, its low-pass
 * gate: DECAY sets how notes fade after their note off.
 *
 * Plaits tunes itself for its hardware's 47872 Hz; the studio runs at
 * 44.1 kHz, so every note is corrected (PLAN.md: only Plaits needs it).
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef OM_PLAITS_ENGINE_H_
#define OM_PLAITS_ENGINE_H_

#include <stdint.h>
#include "engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLAITS_PARTS  2
#define PLAITS_VOICES 4   // per part

// a part's parameters (registry/plaits.json)
enum
{
	PL_P_ENGINE, PL_P_HARMONICS, PL_P_TIMBRE, PL_P_MORPH, PL_P_DECAY, PL_P_COLOUR,
	PL_P_ATTACK, PL_P_TIMBRE_ENV, PL_P_MORPH_ENV, PL_P_VOLUME, PL_P_TRANSPOSE, PL_P_PRESET,
	PLAITS_NUM_PARAMS
};

extern const OmEngineClass om_plaits_engine;

// A preset (registry/plaits.json "presets", in om_registry_plaits.h): a name,
// its genre (0 trance, 1 electro, 2 synthwave, 3 dub techno) and the
// parameters PL_P_ENGINE .. PL_P_TRANSPOSE.
#define PLAITS_PRESET_PARAMS PL_P_PRESET
typedef struct
{
	const char *name;
	uint8_t genre;
	uint8_t p[PLAITS_PRESET_PARAMS];
} PlaitsPreset;

#ifdef __cplusplus
}
#endif

#endif /* OM_PLAITS_ENGINE_H_ */
