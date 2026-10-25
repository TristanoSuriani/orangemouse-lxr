/*
 * web_lxr_ui.c
 *
 * What the LXR's panel shows, for the page around it: the parameters the four
 * pots change right now, so the help strip can explain them (web/app/main.js).
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */
#include "web_port.h"
#include "menu/menu.h"

// [0..3] the parameter on each pot (as menu_parseKnobValue picks it), [4] the
// display's cursor (0..7), [5] the menu page
WEB_EXPORT uint16_t *web_lxr_pots(void)
{
	static uint16_t out[6];
	const uint8_t index = getMenuIndx();
	const uint8_t activePage = (index & MASK_PAGE) >> PAGE_SHIFT;
	const uint8_t activeParameter = index & MASK_PARAMETER;
	const uint8_t second = (activeParameter > 3) * 4;
	for (uint8_t i = 0; i < 4; i++)
		out[i] = pgm_read_word(&menuPages[menu_activePage][activePage].bot1 + i + second);
	out[4] = activeParameter;
	out[5] = menu_activePage;
	return out;
}
