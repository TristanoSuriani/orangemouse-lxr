/*
 * filter.h
 *
 * The mixer's channel filters (PLAN.md Phase 3, added 2026-10-26): Surge
 * XT's filters (sst-filters) as an insert on every channel, before its
 * fader. Stereo; cutoff and resonance glide over each 16-frame block.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef OM_FILTER_H_
#define OM_FILTER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// registry/mix.json OM_MIX_FILTER
enum
{
	OM_FILTER_OFF, OM_FILTER_LP12, OM_FILTER_LP24, OM_FILTER_LADDER, OM_FILTER_VINTAGE, OM_FILTER_OBXD,
	OM_FILTER_K35, OM_FILTER_DIODE, OM_FILTER_HP12, OM_FILTER_HP24, OM_FILTER_K35_HP, OM_FILTER_BP,
	OM_FILTER_NOTCH, OM_FILTER_OBXD_BP, OM_FILTER_TYPES
};

typedef struct OmFilter OmFilter;

OmFilter *om_filter_create(float sampleRate);
void om_filter_destroy(OmFilter *f);
// type: OM_FILTER_*; cutoff: semitones from A4 (-60 .. 70); resonance 0..1
void om_filter_set(OmFilter *f, uint8_t type, float cutoff, float resonance);
// in place; frames: a multiple of 16. Off: the signal passes untouched.
void om_filter_process(OmFilter *f, float *left, float *right, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* OM_FILTER_H_ */
