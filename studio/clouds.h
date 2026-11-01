/*
 * clouds.h
 *
 * Mutable Instruments' Clouds (third_party/eurorack, MIT) as a send effect
 * (PLAN.md "Later", added 2026-11-01; fx.h OM_FX_CLOUDS): granular,
 * time-stretch, looping-delay and spectral textures from what the send bus
 * feeds it. All wet, as a send effect is. It runs at its module's 32 kHz,
 * resampled to and from the studio's rate (resampler.h): 3 ms late.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef OM_CLOUDS_H_
#define OM_CLOUDS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// its settings, in registry/fx.json's order from OM_FX_CLOUDS_MODE
enum
{
	OM_CLOUDS_MODE, OM_CLOUDS_POSITION, OM_CLOUDS_SIZE, OM_CLOUDS_PITCH, OM_CLOUDS_DENSITY, OM_CLOUDS_TEXTURE,
	OM_CLOUDS_FEEDBACK, OM_CLOUDS_SPREAD, OM_CLOUDS_REVERB, OM_CLOUDS_FREEZE, OM_CLOUDS_SETTINGS
};

typedef struct OmClouds OmClouds;

OmClouds *om_clouds_create(float sampleRate);
void om_clouds_destroy(OmClouds *c);
// a setting's byte (registry/fx.json)
void om_clouds_set(OmClouds *c, int setting, uint8_t value);
// empties the buffers and the recording
void om_clouds_reset(OmClouds *c);
// Replaces left/right with the effect's output; any number of frames.
void om_clouds_process(OmClouds *c, float *left, float *right, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* OM_CLOUDS_H_ */
