/*
 * web_port.h
 *
 * Internal interface between the modules of the WebAssembly port.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */
#ifndef WEB_PORT_H_
#define WEB_PORT_H_

#include <stdint.h>

#define WEB_EXPORT __attribute__((used, visibility("default")))
#define WEB_IMPORT(name) __attribute__((import_module("env"), import_name(#name)))

// front panel shift registers, see IO/DIO.h (NUM_INPUTS / NUM_OUTS): five
// bytes on the panel, a sixth for the VA voice buttons and LEDs
#define NUM_INPUTS_BYTES  6
#define NUM_OUTPUTS_BYTES 6

extern uint8_t web_buttonState[NUM_INPUTS_BYTES];
extern uint8_t web_ledState[NUM_OUTPUTS_BYTES];

// web_hal.c
void web_hal_incTick(void);
uint32_t web_rng_next(void);
void web_rng_seed(uint32_t seed);

// web_lcd.c: HD44780 display model
#define WEB_LCD_COLS 16
#define WEB_LCD_ROWS 2
extern uint8_t web_lcdText[WEB_LCD_ROWS][WEB_LCD_COLS];  // character codes
extern uint8_t web_lcdCgram[64];                         // 8 custom characters, 8 rows each
extern uint8_t web_lcdOn;
extern uint32_t web_lcdVersion;                          // bumped on every visible change

// web_encoder.c
void web_encoder_turn(int32_t detents);
void web_encoder_setButton(uint8_t pressed);

// web_sdcard.c
extern uint8_t *web_sdImage;
extern uint32_t web_sdSectors;
extern uint8_t web_sdPresent;

// implemented in JavaScript (see app/lxr-worklet.js)
WEB_IMPORT(web_log) void web_log(const char *msg);
WEB_IMPORT(web_sd_written) void web_sd_written(uint32_t firstSector, uint32_t count);
WEB_IMPORT(web_midi_out) void web_midi_out(uint8_t byte);

#endif /* WEB_PORT_H_ */
