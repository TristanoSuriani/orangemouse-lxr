/*
 * web_arm_division.c
 *
 * Integer division with Cortex-M7 semantics. The MCU runs with division by
 * zero trapping disabled: UDIV/SDIV by 0 give 0, so x % 0 (computed as
 * x - (x / y) * y) gives x, and INT_MIN / -1 gives INT_MIN. The firmware
 * relies on this, e.g. "% length" with an empty pattern. WebAssembly traps in
 * these cases instead, so tools/arm_division.py replaces every division and
 * remainder instruction in lxr.wasm by a call to these functions.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */
#include "web_port.h"

WEB_EXPORT uint32_t web_arm_div_u32(uint32_t a, uint32_t b)
{
	return b ? a / b : 0;
}

WEB_EXPORT uint32_t web_arm_rem_u32(uint32_t a, uint32_t b)
{
	return b ? a % b : a;
}

WEB_EXPORT int32_t web_arm_div_s32(int32_t a, int32_t b)
{
	if (b == 0)
		return 0;
	if (b == -1)
		return (int32_t)(0u - (uint32_t)a);  // INT_MIN / -1 = INT_MIN
	return a / b;
}

WEB_EXPORT int32_t web_arm_rem_s32(int32_t a, int32_t b)
{
	if (b == 0)
		return a;
	if (b == -1)
		return 0;
	return a % b;
}

WEB_EXPORT uint64_t web_arm_div_u64(uint64_t a, uint64_t b)
{
	return b ? a / b : 0;
}

WEB_EXPORT uint64_t web_arm_rem_u64(uint64_t a, uint64_t b)
{
	return b ? a % b : a;
}

WEB_EXPORT int64_t web_arm_div_s64(int64_t a, int64_t b)
{
	if (b == 0)
		return 0;
	if (b == -1)
		return (int64_t)(0u - (uint64_t)a);
	return a / b;
}

WEB_EXPORT int64_t web_arm_rem_s64(int64_t a, int64_t b)
{
	if (b == 0)
		return a;
	if (b == -1)
		return 0;
	return a % b;
}
