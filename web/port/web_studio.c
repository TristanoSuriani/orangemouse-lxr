/*
 * web_studio.c
 *
 * The studio in the browser (web/app/studio-worklet.js): what studio.wasm
 * exports. Engine 0 is the LXR, engine 1 Orange Mouse VA, engine 2 Dexed, engine 3 Plaits. The worklet loads
 * the saved SD card (web_sd_alloc) before om_web_init(), renders 64-frame
 * blocks with om_web_render(), and forwards the panels' notes and parameter
 * changes, which take effect at the start of the next block. The tracker
 * (studio/tracker.c) is the clock: it schedules its rows before each block.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "web_port.h"
#include "studio.h"
#include "tracker.h"
#include "VaEngine.h"
#include "dexed_engine.h"
#include "plaits_engine.h"
#include "om_registry_lxr.h"
#include "om_registry_plaits.h"

extern const OmEngineClass om_lxr_engine, om_va_engine;
void om_lxr_set_seed(uint32_t seed);

static OmStudio *studio;
static OmTracker *tracker;
static float mix[2][OM_MAX_BLOCK];   // left block, right block

WEB_EXPORT int om_web_init(float rate, uint32_t seed)
{
	om_lxr_set_seed(seed);
	studio = om_studio_create(rate);
	if (!studio)
		return -1;
	if (om_studio_add_engine(studio, &om_lxr_engine) != 0 || om_studio_add_engine(studio, &om_va_engine) != 1 ||
	    om_studio_add_engine(studio, &om_dexed_engine) != 2 || om_studio_add_engine(studio, &om_plaits_engine) != 3)
		return -2;
	for (uint8_t s = 0; s < VA_NUM_SLOTS; s++)   // -6 dB: VA's slots are loud next to the LXR
		om_studio_mix(studio, (uint8_t)om_studio_channel(studio, 1, s), OM_MIX_LEVEL, 79);
	tracker = om_tracker_create(studio, rate);
	if (!tracker)
		return -3;
	// tracks 1-7: the LXR's voices; 8-11: VA's slots; 12-13: Dexed's parts; 14-15: Plaits'; 16: not routed yet
	for (uint8_t k = 0; k < 7; k++)
		om_tracker_route(tracker, k, 0, k);
	for (uint8_t k = 0; k < VA_NUM_SLOTS; k++)
		om_tracker_route(tracker, (uint8_t)(7 + k), 1, k);
	for (uint8_t k = 0; k < DEXED_PARTS; k++)
		om_tracker_route(tracker, (uint8_t)(11 + k), 2, k);
	for (uint8_t k = 0; k < PLAITS_PARTS; k++)
		om_tracker_route(tracker, (uint8_t)(13 + k), 3, k);
	return 0;
}

WEB_EXPORT float *om_web_render(void)
{
	om_tracker_advance(tracker, OM_MAX_BLOCK);
	om_studio_render(studio, mix[0], mix[1], OM_MAX_BLOCK);
	return &mix[0][0];
}

WEB_EXPORT int om_web_block_size(void)
{
	return OM_MAX_BLOCK;
}

WEB_EXPORT void om_web_note_on(uint8_t engine, uint8_t part, uint8_t note, uint8_t velocity)
{
	om_studio_note_on(studio, om_studio_frame(studio), engine, part, note, velocity);
}

WEB_EXPORT void om_web_note_off(uint8_t engine, uint8_t part, uint8_t note)
{
	om_studio_note_off(studio, om_studio_frame(studio), engine, part, note);
}

WEB_EXPORT void om_web_set_param(uint8_t engine, uint8_t part, uint16_t param, uint8_t value)
{
	om_studio_set_param(studio, om_studio_frame(studio), engine, part, param, value);
}

WEB_EXPORT uint8_t om_web_get_param(uint8_t engine, uint8_t part, uint16_t param)
{
	return om_studio_get_param(studio, engine, part, param);
}

WEB_EXPORT void om_web_set_tempo(float bpm)
{
	om_studio_set_tempo(studio, bpm);
}

// ---- the mixer (studio.h, registry/mix.json and fx.json) ----

static float meters[OM_MAX_CHANNELS + 5];

WEB_EXPORT int om_web_channels(void) { return om_studio_channels(studio); }
WEB_EXPORT int om_web_channel(uint8_t engine, uint8_t stem) { return om_studio_channel(studio, engine, stem); }
WEB_EXPORT void om_web_mix(uint8_t target, uint8_t param, uint8_t value) { om_studio_mix(studio, target, param, value); }
WEB_EXPORT uint8_t om_web_mix_value(uint8_t target, uint8_t param) { return om_studio_mix_value(studio, target, param); }
WEB_EXPORT void om_web_fx(uint8_t bus, uint16_t param, uint8_t value) { om_studio_fx(studio, bus, param, value); }
WEB_EXPORT uint8_t om_web_fx_value(uint8_t bus, uint16_t param) { return om_studio_fx_value(studio, bus, param); }
// the peaks since the last call (om_studio_meters): channels, buses, master L/R, limiter gain
WEB_EXPORT float *om_web_meters(void)
{
	om_studio_meters(studio, meters, OM_MAX_CHANNELS + 5);
	return meters;
}

// Orange Mouse VA's preset banks, for the panel: how many a slot has, a
// preset's name and its sound parameters (enum VaParam order).
WEB_EXPORT int om_web_va_presets(uint8_t slot)
{
	return slot < VA_NUM_SLOTS ? va_numPresets[slot] : 0;
}

WEB_EXPORT const char *om_web_va_preset_name(uint8_t slot, uint8_t preset)
{
	return va_presets[slot][preset].name;
}

WEB_EXPORT const uint8_t *om_web_va_preset_params(uint8_t slot, uint8_t preset)
{
	return va_presets[slot][preset].p;
}

WEB_EXPORT int om_web_va_preset_param_count(void)
{
	return VA_NUM_SOUND_PARAMS;
}

// The LXR's kit as the song keeps it: every parameter's value (registry/lxr.json order).
static uint8_t lxrKit[OM_LXR_NUM_PARAMS];
WEB_EXPORT int om_web_lxr_kit_size(void) { return OM_LXR_NUM_PARAMS; }
WEB_EXPORT const uint8_t *om_web_lxr_kit(void)
{
	for (uint16_t p = 0; p < OM_LXR_NUM_PARAMS; p++)
		lxrKit[p] = om_studio_get_param(studio, 0, 0, p);
	return lxrKit;
}

// Dexed's cartridges, for the bank browser: the banks' names and the programs'.
static char dexedName[11];
WEB_EXPORT int om_web_dexed_banks(void) { return om_dexed_banks; }
WEB_EXPORT const char *om_web_dexed_bank_name(int bank) { return om_dexed_bank_name(bank); }
WEB_EXPORT const char *om_web_dexed_program_name(int bank, int program)
{
	om_dexed_program_name(bank, program, dexedName);
	return dexedName;
}

// ---- the tracker (studio/tracker.h), driven by the tracker device ----

WEB_EXPORT void om_web_tracker_cell(uint8_t pattern, uint8_t row, uint8_t track,
                                    uint8_t n0, uint8_t n1, uint8_t n2, uint8_t n3, uint8_t velocity, uint8_t retrig,
                                    uint8_t fx, uint8_t fxValue)
{
	const OmCell c = {{n0, n1, n2, n3}, velocity, retrig, fx, fxValue};
	om_tracker_set_cell(tracker, pattern, row, track, &c);
}

WEB_EXPORT void om_web_tracker_clear(void) { om_tracker_clear(tracker); }
WEB_EXPORT void om_web_tracker_length(uint8_t pattern, uint8_t rows) { om_tracker_set_length(tracker, pattern, rows); }
WEB_EXPORT void om_web_tracker_pattern_tempo(uint8_t pattern, float bpm) { om_tracker_set_pattern_tempo(tracker, pattern, bpm); }
WEB_EXPORT void om_web_tracker_order(uint16_t position, uint8_t pattern) { om_tracker_set_order(tracker, position, pattern); }
WEB_EXPORT void om_web_tracker_tempo(float bpm) { om_tracker_set_tempo(tracker, bpm); }
WEB_EXPORT void om_web_tracker_route(uint8_t track, uint8_t engine, uint8_t part) { om_tracker_route(tracker, track, engine, part); }
WEB_EXPORT void om_web_tracker_mute(uint8_t track, uint8_t on) { om_tracker_set_mute(tracker, track, on); }
WEB_EXPORT void om_web_tracker_solo(uint8_t track, uint8_t on) { om_tracker_set_solo(tracker, track, on); }
WEB_EXPORT void om_web_tracker_play_song(uint16_t position) { om_tracker_play_song(tracker, position); }
WEB_EXPORT void om_web_tracker_play_pattern(uint8_t pattern) { om_tracker_play_pattern(tracker, pattern); }
WEB_EXPORT void om_web_tracker_stop(void) { om_tracker_stop(tracker); }

// playing, pattern, position, row (as floats, with the tempo): for the display
WEB_EXPORT float *om_web_tracker_state(void)
{
	static float out[5];
	const OmTrackerState s = om_tracker_state(tracker);
	out[0] = s.playing;
	out[1] = s.pattern;
	out[2] = s.position;
	out[3] = s.row;
	out[4] = s.bpm;
	return out;
}
