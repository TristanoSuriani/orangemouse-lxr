/*
 * web_io_stubs.c
 *
 * Hardware drivers that have nothing to drive in the browser:
 *  - the I2S codecs (AudioCodecManager.c, cs4344_cs5343.c): the port reads
 *    the rendered blocks from dma_buffer/dma_buffer2 itself,
 *  - USB-MIDI (usb_manager.c): browser MIDI goes through the DIN MIDI UART
 *    instead, see web_main.c.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */
#include "Soundengine/AudioCodecManager.h"
#include "Soundengine/USB/usb_manager.h"

INDTCMZ int16_t dma_buffer[OUTPUT_DMA_SIZE * 4];
INDTCMZ int16_t dma_buffer2[OUTPUT_DMA_SIZE * 4];
volatile INDTCMZ uint8_t bCurrentSampleValid = 0;

// Declared as "int CodecInit()" and called with the sample rate; the
// definition must take the argument, or WebAssembly traps on the call.
int CodecInit(uint32_t sampleRate)
{
	(void)sampleRate;
	return 0;
}

void usb_tick()
{
}

void usb_flushMidi()
{
}

void usb_sendMidi(MidiMsg msg)
{
	(void)msg;
}

uint8_t usb_getMidi(MidiMsg *msg)
{
	(void)msg;
	return 0;
}
