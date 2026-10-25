/*
 * VaEngine.h
 *
 * The virtual analogue engine behind the four VA tracks (VA1 mono bass, VA2
 * poly, VA3 mono lead, VA4 noises). Each slot is a complete synthesizer: two
 * oscillators, a sub-oscillator and noise, a nonlinear zero-delay filter,
 * two envelopes, an LFO, unison and glide. Every note runs at VA_OS times
 * the sample rate and each slot is brought down with a cascade of halfband
 * FIR decimators.
 *
 * A slot's parameters are VA_NUM_PARAMS consecutive entries of
 * activeParameters, starting at PAR_VA1_FIRST + slot * VA_NUM_PARAMS
 * (Parameters.h); the engine reads them every block.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */
#ifndef VAENGINE_H_
#define VAENGINE_H_

#include <stdint.h>
#include "Soundengine/config.h"

#define VA_NUM_SLOTS    4
#define VA_FIRST_TRACK  7     // the VA slots are tracks 7..10, after the seven drum voices

// The parameters of one slot, in menu order (8 sub-pages, see menuPages.h).
enum VaParam
{
	// OSC
	VA_P_WAVE1, VA_P_WAVE2, VA_P_SEMI2, VA_P_FINE2, VA_P_PW, VA_P_MIX, VA_P_SYNC, VA_P_OCT,
	// MIXER: oscillators, sub-oscillator, noise, drive into the filter, analogue variance
	VA_P_OSC_LEVEL, VA_P_SUB, VA_P_SUB_WAVE, VA_P_SUB_OCT, VA_P_NOISE, VA_P_NOISE_TYPE, VA_P_DRIVE, VA_P_SLOP,
	// FILTER
	VA_P_CUTOFF, VA_P_RESO, VA_P_FTYPE, VA_P_FENV_AMT, VA_P_KEYTRACK, VA_P_FVEL, VA_P_HPF,
	// FILTER ENVELOPE
	VA_P_FATK, VA_P_FDEC, VA_P_FSUS, VA_P_FREL,
	// AMP ENVELOPE
	VA_P_AATK, VA_P_ADEC, VA_P_ASUS, VA_P_AREL, VA_P_AVEL,
	// LFO
	VA_P_LRATE, VA_P_LWAVE, VA_P_LPITCH, VA_P_LCUT, VA_P_LPWM, VA_P_LDELAY,
	// VOICE
	VA_P_MODE, VA_P_GATE, VA_P_GLIDE, VA_P_UNISON, VA_P_SPREAD, VA_P_WIDTH,
	// MIX
	VA_P_PRESET, VA_P_VOLUME, VA_P_PAN, VA_P_OUT, VA_P_MIDI_CHAN,

	VA_NUM_PARAMS,
	VA_NUM_SOUND_PARAMS = VA_P_PRESET	// the parameters a preset sets
};

// value lists of the menu parameters
enum { VA_WAVE_SAW, VA_WAVE_SQUARE, VA_WAVE_TRI, VA_WAVE_SINE, VA_NUM_WAVES };
enum { VA_NOISE_WHITE, VA_NOISE_PINK, VA_NOISE_BROWN, VA_NUM_NOISES };
enum { VA_FILTER_LP24, VA_FILTER_LP12, VA_FILTER_BP12, VA_FILTER_HP12, VA_NUM_FILTERS };
enum { VA_LFO_SINE, VA_LFO_TRI, VA_LFO_SAW, VA_LFO_SQUARE, VA_LFO_SH, VA_NUM_LFO_WAVES };
enum { VA_MODE_MONO, VA_MODE_LEGATO, VA_MODE_POLY, VA_NUM_MODES };

#define VA_MAX_PRESETS 24

typedef struct
{
	char name[13];
	uint8_t p[VA_NUM_SOUND_PARAMS];
} VaPreset;

extern const VaPreset * const va_presets[VA_NUM_SLOTS];
extern const uint8_t va_numPresets[VA_NUM_SLOTS];

void va_init(void);
// note on for a slot (0..3); the note lasts for the slot's GATE time
void va_noteOn(uint8_t slot, uint8_t note, uint8_t velocity);
// all notes of a slot (or of all slots for 0xff) released
void va_allNotesOff(uint8_t slot);
// a slot's parameter changed (paramIdx: enum VaParam)
void va_paramChanged(uint8_t slot, uint8_t paramIdx);
// copies preset presetNr of the slot's preset list into the slot's parameters
void va_loadPreset(uint8_t slot, uint8_t presetNr);
// the default sound of every slot (its first preset) and mix settings
void va_initKitParameters(uint8_t *params);
// renders one block of OUTPUT_DMA_SIZE samples of a slot, stereo, int16
// scaled; returns 0 if the slot is silent (and the buffers are untouched)
uint8_t va_render(uint8_t slot, int16_t *left, int16_t *right);

#endif /* VAENGINE_H_ */
