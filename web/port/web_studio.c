/*
 * web_studio.c
 *
 * The studio in the browser (web/app/studio-worklet.js): what studio.wasm
 * exports. Engine 0 is the LXR, engine 1 Orange Mouse VA. The worklet loads
 * the saved SD card (web_sd_alloc) before om_web_init(), renders 64-frame
 * blocks with om_web_render(), and forwards the panels' notes and parameter
 * changes, which take effect at the start of the next block.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "web_port.h"
#include "studio.h"
#include "VaEngine.h"

extern const OmEngineClass om_lxr_engine, om_va_engine;
void om_lxr_set_seed(uint32_t seed);

static OmStudio *studio;
static float mix[2][OM_MAX_BLOCK];   // left block, right block

WEB_EXPORT int om_web_init(float rate, uint32_t seed)
{
	om_lxr_set_seed(seed);
	studio = om_studio_create(rate);
	if (!studio)
		return -1;
	if (om_studio_add_engine(studio, &om_lxr_engine) != 0 || om_studio_add_engine(studio, &om_va_engine) != 1)
		return -2;
	for (uint8_t s = 0; s < VA_NUM_SLOTS; s++)
		om_studio_set_stem(studio, 1, s, 0.5f, 0.f);
	return 0;
}

WEB_EXPORT float *om_web_render(void)
{
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

WEB_EXPORT void om_web_set_stem(uint8_t engine, uint8_t stem, float level, float balance)
{
	om_studio_set_stem(studio, engine, stem, level, balance);
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
