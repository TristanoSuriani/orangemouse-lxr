/*
 * shaper.h
 *
 * The mixer's channel waveshapers (PLAN.md "Later", added 2026-11-01):
 * Surge XT's waveshapers (sst-waveshapers) as an insert on every channel,
 * after its filter, as in Surge's voice (filter, then shaper). A few of
 * Surge's 40-odd shapes, one of each kind: saturators, folders, rectifiers,
 * fuzz, harmonics. Stereo; the drive glides over each block and the output
 * is turned down as the drive goes up, so a shaper changes the colour more
 * than the level.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef OM_SHAPER_H_
#define OM_SHAPER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// registry/mix.json OM_MIX_SHAPE
enum
{
	OM_SHAPE_OFF, OM_SHAPE_SOFT, OM_SHAPE_WARM, OM_SHAPE_TUBE, OM_SHAPE_HARD, OM_SHAPE_ASYM, OM_SHAPE_FOLD,
	OM_SHAPE_DOUBLE_FOLD, OM_SHAPE_WEST, OM_SHAPE_RECTIFY, OM_SHAPE_FUZZ, OM_SHAPE_HEAVY_FUZZ,
	OM_SHAPE_OCTAVE, OM_SHAPE_FIFTH, OM_SHAPE_SINE, OM_SHAPE_DIGITAL, OM_SHAPE_TYPES
};

typedef struct OmShaper OmShaper;

OmShaper *om_shaper_create(void);
void om_shaper_destroy(OmShaper *s);
// type: OM_SHAPE_*; drive: the gain into the shape, 1 (0 dB) .. 31.6 (+30 dB), as
// studio.c works it out (with musl's powf, so native and WebAssembly agree)
void om_shaper_set(OmShaper *s, uint8_t type, float drive);
// in place. Off: the signal passes untouched.
void om_shaper_process(OmShaper *s, float *left, float *right, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* OM_SHAPER_H_ */
