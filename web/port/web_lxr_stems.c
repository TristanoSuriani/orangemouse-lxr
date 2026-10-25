/*
 * web_lxr_stems.c
 *
 * The LXR's voices as separate stereo channels (stems) for the studio's
 * mixer; the hardware mixes them to two outputs. Off until the studio turns
 * it on (web_lxr_stems_on), so the LXR on its own renders as before.
 *
 * mixer.c calls web_voice_stem() with each voice's block before routing it:
 * the voice, placed by its own pan (the firmware's triggered pan), is its
 * stem, unless it is routed to the LXR's FX bus. The FX bus's output is the
 * last stem: mixer.c calls web_stems_before_fx() and web_stems_after_fx()
 * around FxBus_process(), and the outputs are cleared before it, so they
 * then hold only what the FX bus made.
 *
 * Stems: drum 1-3, snare, cymbal, hihat (open and closed), the FX bus.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */
#include <string.h>
#include "web_port.h"
#include "Soundengine/config.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/HiHat.h"

uint8_t web_lxr_stems_on;
float web_lxrStems[WEB_LXR_STEMS][2][OUTPUT_DMA_SIZE];   // [stem][left, right][frame]

void web_voice_stem(uint8_t voice, const int16_t *data)
{
	if (!web_lxr_stems_on || voice >= WEB_LXR_STEMS - 1)
		return;
	float *l = web_lxrStems[voice][0], *r = web_lxrStems[voice][1];
	if (mixer_audioRouting[voice] == MIXER_ROUTING_FX_BUS)
	{
		memset(l, 0, sizeof(web_lxrStems[0][0]));
		memset(r, 0, sizeof(web_lxrStems[0][0]));
		return;
	}
	float panL, panR;
	switch (voice)
	{
	case 3:  panL = snareVoice.triggeredPanL;  panR = snareVoice.triggeredPanR;  break;
	case 4:  panL = cymbalVoice.triggeredPanL; panR = cymbalVoice.triggeredPanR; break;
	case 5:  panL = hatVoice.triggeredPanL;    panR = hatVoice.triggeredPanR;    break;
	default: panL = voiceArray[voice].triggeredPanL; panR = voiceArray[voice].triggeredPanR; break;
	}
	// as the firmware does: the panned sample, as a 16-bit integer
	const float scale = 1.f / 32768.f;
	for (int i = 0; i < OUTPUT_DMA_SIZE; i++)
	{
		l[i] = (int16_t)(data[i] * panL) * scale;
		r[i] = (int16_t)(data[i] * panR) * scale;
	}
}

void web_stems_before_fx(int16_t *out, int16_t *out2)
{
	if (!web_lxr_stems_on)
		return;
	memset(out, 0, OUTPUT_DMA_SIZE * 2 * sizeof(int16_t));
	memset(out2, 0, OUTPUT_DMA_SIZE * 2 * sizeof(int16_t));
}

void web_stems_after_fx(const int16_t *out, const int16_t *out2)
{
	if (!web_lxr_stems_on)
		return;
	const float scale = 1.f / 32768.f;
	float *l = web_lxrStems[WEB_LXR_STEMS - 1][0], *r = web_lxrStems[WEB_LXR_STEMS - 1][1];
	for (int i = 0; i < OUTPUT_DMA_SIZE; i++)
	{
		l[i] = (out[2 * i] + out2[2 * i]) * scale;
		r[i] = (out[2 * i + 1] + out2[2 * i + 1]) * scale;
	}
}
