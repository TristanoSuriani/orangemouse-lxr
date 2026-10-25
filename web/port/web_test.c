/*
 * web_test.c
 *
 * Hooks for tests: trigger a voice and set a parameter without going through
 * the panel or MIDI channel settings. The native host tests (tests/host/) and
 * the WebAssembly parity check (tests/host/parity.mjs) drive the firmware
 * through the same calls, so both builds render the same thing.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */
#include "web_port.h"
#include "Parameters.h"
#include "menu/menu.h"
#include "Soundengine/MIDI/MidiVoiceControl.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Preset/PresetManager.h"

// voice 0..6 (drum 1-3, snare, cymbal, closed hihat, open hihat)
WEB_EXPORT void web_test_note_on(uint8_t voice, uint8_t note, uint8_t velocity)
{
	if (voice < NUM_TRACKS)
		voiceControl_noteOn(voice, note, velocity);
}

WEB_EXPORT void web_test_note_off(uint8_t voice)
{
	voiceControl_noteOff(voice);
}

// A sound parameter (enum in Parameters.h, below END_OF_SOUND_PARAMETERS) in
// the kit being edited. Nothing is heard until web_test_load_kit().
WEB_EXPORT void web_test_set_param(uint16_t param, uint8_t value)
{
	if (param < END_OF_SOUND_PARAMETERS)
		activeParameters[param] = value;
}

// Sends the whole kit to the sound engine exactly as loading a kit from the
// SD card does (preset_sendDrumsetParametersImmediate), modulation targets
// included.
WEB_EXPORT void web_test_load_kit(void)
{
	preset_sendDrumsetParametersImmediate();
}

WEB_EXPORT uint16_t web_test_num_sound_params(void)
{
	return END_OF_SOUND_PARAMETERS;
}

// A busier kit than the init kit, so parameters that the init kit leaves idle
// are heard too: drum 2 and the snare go to the FX bus (delay, half wet), every LFO
// modulates its own voice's third target, and velocity modulates each voice's
// second target. Like web_test_set_param, it only edits the kit.
WEB_EXPORT void web_test_busy_kit(void)
{
	web_test_set_param(PAR_AUDIO_OUT2, 6);  // "FX" (outputNames in MenuText.h)
	web_test_set_param(PAR_AUDIO_OUT4, 6);
	web_test_set_param(PAR_FX_TYPE, 4);     // "del" (fxNames in MenuText.h)
	web_test_set_param(PAR_FX_DRY_WET, 64);
	for (uint8_t i = 0; i < 6; i++)
	{
		web_test_set_param((uint16_t)(PAR_VOICE_LFO1 + i), (uint8_t)(i + 1));
		web_test_set_param((uint16_t)(PAR_TARGET_LFO1 + i), (uint8_t)(modTargetVoiceOffsets[i].start + 2));
		web_test_set_param((uint16_t)(PAR_FREQ_LFO1 + i), 40);
		web_test_set_param((uint16_t)(PAR_AMOUNT_LFO1 + i), 100);
		web_test_set_param((uint16_t)(PAR_VEL_DEST_1 + i), (uint8_t)(modTargetVoiceOffsets[i].start + 1));
		web_test_set_param((uint16_t)(PAR_VELO_MOD_AMT_1 + i), 100);
	}
}

// The demo groove the renders and the parity check play: one bar of 16ths at
// 120 BPM on all seven voices, repeated. Call it before each web_render() with
// the index of the block about to be rendered; it triggers the notes that
// start in that block. Written in C so every build plays exactly the same notes.
WEB_EXPORT void web_test_demo_block(uint32_t block, float rate)
{
	//                       step: 0123456789abcdef
	static const char *pattern[7] = {
		"x...x...x...x...",  // drum 1: four on the floor
		"......x.......x.",  // drum 2
		"..........x.....",  // drum 3
		"....x.......x...",  // snare
		"x...............",  // cymbal
		"..x...x...x...x.",  // closed hihat
		"...............x",  // open hihat
	};
	static const uint8_t notes[7] = {36, 43, 50, 38, 49, 42, 46};
	const double stepSeconds = 60.0 / 120.0 / 4.0;
	const double t0 = block * (double)OUTPUT_DMA_SIZE / rate;
	const double t1 = (block + 1) * (double)OUTPUT_DMA_SIZE / rate;
	const uint32_t s0 = (uint32_t)(t0 / stepSeconds + 0.999999);
	if (s0 * stepSeconds >= t1)
		return;
	const uint32_t step = s0 % 16;
	for (uint8_t v = 0; v < 7; v++)
		if (pattern[v][step] == 'x')
			voiceControl_noteOn(v, notes[v], step % 4 == 0 ? 120 : 90);
}
