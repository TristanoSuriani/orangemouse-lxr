/*
 * fx.h
 *
 * The studio's send effects (PLAN.md Phase 3): Surge XT's hall and room
 * reverbs, delay, phaser and flanger (sst-effects), and a chorus written here
 * (sst-effects has none yet). An OmFx is one send bus's effect slot: it can
 * be any type, and keeps every type's settings, so switching back restores
 * them. Output is the effect alone (wet): the dry sound reaches the mix
 * through its channel.
 *
 * Parameters are bytes, described by registry/fx.json: each belongs to one
 * type. A type's engine is allocated the first time the slot is set to it
 * (Surge's hall reverb needs 14 MB), then kept.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef OM_FX_H_
#define OM_FX_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { OM_FX_OFF, OM_FX_HALL, OM_FX_ROOM, OM_FX_DELAY, OM_FX_CHORUS, OM_FX_PHASER, OM_FX_FLANGER, OM_FX_TYPES };

// the parameters, in registry/fx.json's order
enum
{
	OM_FX_HALL_PRE,
	OM_FX_HALL_DECAY,
	OM_FX_HALL_SIZE,
	OM_FX_HALL_DIFF,
	OM_FX_HALL_MOD,
	OM_FX_HALL_LFD,
	OM_FX_HALL_HFD,
	OM_FX_HALL_WIDTH,
	OM_FX_ROOM_PRE,
	OM_FX_ROOM_SHAPE,
	OM_FX_ROOM_SIZE,
	OM_FX_ROOM_DECAY,
	OM_FX_ROOM_HFD,
	OM_FX_ROOM_LOCUT,
	OM_FX_ROOM_HICUT,
	OM_FX_ROOM_WIDTH,
	OM_FX_DELAY_LEFT,
	OM_FX_DELAY_RIGHT,
	OM_FX_DELAY_FEEDBACK,
	OM_FX_DELAY_CROSS,
	OM_FX_DELAY_LOCUT,
	OM_FX_DELAY_HICUT,
	OM_FX_DELAY_RATE,
	OM_FX_DELAY_DEPTH,
	OM_FX_DELAY_WIDTH,
	OM_FX_CHORUS_RATE,
	OM_FX_CHORUS_DEPTH,
	OM_FX_CHORUS_DELAY,
	OM_FX_CHORUS_FEEDBACK,
	OM_FX_CHORUS_WIDTH,
	OM_FX_PHASER_CENTER,
	OM_FX_PHASER_FEEDBACK,
	OM_FX_PHASER_SHARP,
	OM_FX_PHASER_RATE,
	OM_FX_PHASER_DEPTH,
	OM_FX_PHASER_STAGES,
	OM_FX_PHASER_STEREO,
	OM_FX_PHASER_WAVE,
	OM_FX_PHASER_TONE,
	OM_FX_FLANGER_MODE,
	OM_FX_FLANGER_WAVE,
	OM_FX_FLANGER_RATE,
	OM_FX_FLANGER_DEPTH,
	OM_FX_FLANGER_VOICES,
	OM_FX_FLANGER_PITCH,
	OM_FX_FLANGER_SPACING,
	OM_FX_FLANGER_FEEDBACK,
	OM_FX_FLANGER_HFD,
	OM_FX_PARAMS
};

#define OM_FX_BLOCK 16   // process() takes a multiple of this

typedef struct OmFx OmFx;

OmFx *om_fx_create(float sampleRate);
void om_fx_destroy(OmFx *fx);
// Returns 0, or -1 if the type's engine cannot be allocated (the slot is then off).
int om_fx_set_type(OmFx *fx, uint8_t type);
uint8_t om_fx_type(const OmFx *fx);
void om_fx_set_param(OmFx *fx, uint16_t param, uint8_t value);
uint8_t om_fx_param(const OmFx *fx, uint16_t param);
void om_fx_set_tempo(OmFx *fx, float bpm);
// Replaces left/right (frames: a multiple of OM_FX_BLOCK) with the effect's output.
void om_fx_process(OmFx *fx, float *left, float *right, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* OM_FX_H_ */
