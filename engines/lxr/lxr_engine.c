/*
 * lxr_engine.c
 *
 * The LXR in the studio (engines/engine.h): the whole firmware, booted as in
 * the browser (web/port/), so its menus, kits and own sequencer all work.
 * Seven parts (the drum voices: drum 1-3, snare, cymbal, closed and open
 * hihat), two stems (the LXR's stereo outputs 1 and 2). It renders in its
 * own 64-frame blocks, as on the hardware.
 *
 * The firmware keeps its state in globals, so there is one instance.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <string.h>
#include "engine.h"
#include "web_port.h"
#include "Parameters.h"
#include "menu/menu.h"
#include "frontPanelParser_ui.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/MIDI/MidiVoiceControl.h"
#include "Soundengine/Sequencer/sequencer.h"

// web/port/web_main.c and web_sdcard.c
uint8_t *web_sd_alloc(uint32_t sectors);
void web_boot(float rate, uint32_t seed);
float *web_render(void);

#define LXR_PARTS  7
#define LXR_STEMS  2
#define LXR_SD_SECTORS 32768   // a 16 MB card

static uint8_t lxr_created;

static void *lxrCreate(float sampleRate)
{
	if (lxr_created)
		return NULL;
	lxr_created = 1;
	web_sd_alloc(LXR_SD_SECTORS);   // empty: formatted on boot, as on first use in the browser
	web_boot(sampleRate, 1);
	return &lxr_created;
}

static void lxrDestroy(void *self)
{
	(void)self;
	voiceControl_noteOff(0xff);
	lxr_created = 0;
}

static void lxrNoteOn(void *self, uint8_t part, uint8_t note, uint8_t velocity)
{
	(void)self;
	if (part < LXR_PARTS)
		voiceControl_noteOn(part, note, velocity);
}

static void lxrNoteOff(void *self, uint8_t part, uint8_t note)
{
	(void)self;
	(void)note;
	if (part < LXR_PARTS)
		voiceControl_noteOff(part);
}

// One sound parameter of the kit, sent as loading a kit does
// (preset_sendDrumsetParametersImmediate in presetManager.c): the velocity and
// LFO targets are indices into modTargets that go out as parameter numbers,
// everything else as a CC (with the "+1" offset below 128).
static void lxrSetParam(void *self, uint8_t part, uint16_t param, uint8_t value)
{
	(void)self;
	(void)part;
	if (param >= END_OF_SOUND_PARAMETERS)
		return;
	activeParameters[param] = value;

	if (param >= PAR_VEL_DEST_1 && param < PAR_VEL_DEST_1 + 6)
	{
		const uint8_t i = (uint8_t)(param - PAR_VEL_DEST_1);
		const uint8_t target = (uint8_t)modTargets[value].param;
		frontPanel_sendData(CC_VELO_TARGET, (uint8_t)(((target & 0x80) >> 7) | ((i & 0x3f) << 1)), target & 0x7f);
		return;
	}
	if ((param >= PAR_TARGET_LFO1 && param < PAR_TARGET_LFO1 + 6) ||
	    (param >= PAR_VOICE_LFO1 && param < PAR_VOICE_LFO1 + 6))
	{
		const uint8_t i = (uint8_t)(param >= PAR_TARGET_LFO1 ? param - PAR_TARGET_LFO1 : param - PAR_VOICE_LFO1);
		if (activeParameters[PAR_VOICE_LFO1 + i] < 1 || activeParameters[PAR_VOICE_LFO1 + i] > 6)
			activeParameters[PAR_VOICE_LFO1 + i] = 1;
		const uint8_t target = (uint8_t)modTargets[activeParameters[PAR_TARGET_LFO1 + i]].param;
		frontPanel_sendData(CC_LFO_TARGET, (uint8_t)(((target & 0x80) >> 7) | ((i & 0x3f) << 1)), target & 0x7f);
		if (param >= PAR_TARGET_LFO1)
			return;
	}

	MidiMsg msg = {0};
	if (param < 128)
	{
		msg.status = MIDI_CC;
		msg.data1 = (uint8_t)((param + 1) & 0x7f);
	}
	else
	{
		msg.status = CC_2;
		msg.data1 = (uint8_t)((param - 128) & 0x7f);
	}
	msg.data2 = value;
	midiParser_ccHandler(msg, 1, 0, 0.f);
}

static void lxrSetTempo(void *self, float bpm)
{
	(void)self;
	seq_setBpm((uint16_t)(bpm + 0.5f));
}

static void lxrRender(void *self, float *const *stems, uint32_t frames)
{
	(void)self;
	(void)frames;  // always OUTPUT_DMA_SIZE (the class's block)
	const float *out = web_render();   // planar: out 1 L, out 1 R, out 2 L, out 2 R
	for (int c = 0; c < 2 * LXR_STEMS; c++)
		memcpy(stems[c], out + c * OUTPUT_DMA_SIZE, OUTPUT_DMA_SIZE * sizeof(float));
}

const OmEngineClass om_lxr_engine = {
	.name = "LXR",
	.parts = LXR_PARTS,
	.stems = LXR_STEMS,
	.block = OUTPUT_DMA_SIZE,
	.create = lxrCreate,
	.destroy = lxrDestroy,
	.note_on = lxrNoteOn,
	.note_off = lxrNoteOff,
	.set_param = lxrSetParam,
	.set_tempo = lxrSetTempo,
	.render = lxrRender,
};
