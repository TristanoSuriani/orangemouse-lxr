/*
 * rings_engine.h
 *
 * Rings in the studio (PLAN.md "Later", added 2026-11-01): Mutable
 * Instruments' resonator (third_party/eurorack, MIT). One part (RN1) of 1, 2
 * or 4 voices and one stereo stem (odd and even voices, as on the module's
 * two outputs). Every note strums the next voice, which rings as long as its
 * DAMPING lets it: notes have no end, as on the module. A chord's notes strum
 * one block (0.5 ms) apart. Velocity brightens the strike.
 *
 * Rings is excited by its own pulse or noise burst, or, with INPUT set, by a
 * mixer channel's sound (that channel's engine's stem before its inserts):
 * the notes then tune the resonator the sound rings through.
 *
 * Like Plaits, Rings runs at its module's rate, 48 kHz, resampled to and
 * from the studio's (resampler.h).
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef OM_RINGS_ENGINE_H_
#define OM_RINGS_ENGINE_H_

#include <stdint.h>
#include "engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RINGS_PARTS 1

// the part's parameters (registry/rings.json)
enum
{
	RN_P_MODEL, RN_P_POLYPHONY, RN_P_STRUCTURE, RN_P_BRIGHTNESS, RN_P_DAMPING, RN_P_POSITION,
	RN_P_VOLUME, RN_P_TRANSPOSE, RN_P_PRESET, RN_P_INPUT,
	RINGS_NUM_PARAMS
};

extern const OmEngineClass om_rings_engine;

// A preset (registry/rings.json "presets", in om_registry_rings.h): a name,
// its genre (0 trance, 1 electro, 2 synthwave, 3 dub techno) and the
// parameters RN_P_MODEL .. RN_P_TRANSPOSE (INPUT is routing, not sound).
#define RINGS_PRESET_PARAMS RN_P_PRESET
typedef struct
{
	const char *name;
	uint8_t genre;
	uint8_t p[RINGS_PRESET_PARAMS];
} RingsPreset;

#ifdef __cplusplus
}
#endif

#endif /* OM_RINGS_ENGINE_H_ */
