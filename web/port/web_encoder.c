/*
 * web_encoder.c
 *
 * Replaces encoder.c for the WebAssembly build: the browser reports detents
 * and the push button directly instead of quadrature phases on GPIO.
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */
#include "encoder.h"
#include "web_port.h"

// as in encoder.c: queue at most this many detents for the main loop
#define MAX_PENDING_DETENTS 4

static int8_t encDetents;
static uint8_t btnPressed;

void encode_init(void)
{
	encDetents = 0;
}

void encoder_callback(void)
{
}

void web_encoder_turn(int32_t detents)
{
	int32_t pending = encDetents + detents;
	if (pending > MAX_PENDING_DETENTS)
		pending = MAX_PENDING_DETENTS;
	if (pending < -MAX_PENDING_DETENTS)
		pending = -MAX_PENDING_DETENTS;
	encDetents = (int8_t)pending;
}

void web_encoder_setButton(uint8_t pressed)
{
	btnPressed = pressed != 0;
}

int8_t encode_read4(void)
{
	if (encDetents > 0)
	{
		encDetents--;
		return 1;
	}
	if (encDetents < 0)
	{
		encDetents++;
		return -1;
	}
	return 0;
}

uint8_t encode_readButton(void)
{
	return btnPressed;
}
