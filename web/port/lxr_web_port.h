/*
 * lxr_web_port.h
 *
 * Force-included into every translation unit of the WebAssembly build
 * (-include lxr_web_port.h). It lets the unmodified firmware sources compile
 * for wasm32:
 *
 *  - the STM32 peripheral register blocks (GPIO, timers, USART, RCC, ...) are
 *    moved from their fixed MCU addresses into ordinary arrays in linear
 *    memory, so register reads and writes in the firmware become plain memory
 *    accesses that the port can inspect (e.g. the USART3 MIDI interrupt or the
 *    page-down LED on GPIOB),
 *  - the DWT cycle counter advances on every access, so busy waits terminate,
 *  - the Cortex-M DSP intrinsics the audio code uses get portable C versions.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */
#ifndef LXR_WEB_PORT_H_
#define LXR_WEB_PORT_H_

#include <stdint.h>
#include "stm32f7xx.h"

// ---- peripheral register blocks in linear memory ---------------------------
// APB1, APB2 and AHB1 lie within 0x40000000..0x4007FFFF on the MCU; AHB2
// (USB OTG FS, RNG) starts at 0x50000000; the System Control Space and the
// debug blocks (DWT, CoreDebug) are in the 0xE000xxxx region.
#define WEB_PERIPH_SIZE      0x80000u
#define WEB_PERIPH_AHB2_SIZE 0x61000u
#define WEB_PERIPH_CORE_SIZE 0x10000u

extern uint8_t web_periph[WEB_PERIPH_SIZE];
extern uint8_t web_periph_ahb2[WEB_PERIPH_AHB2_SIZE];
extern uint8_t web_periph_core[WEB_PERIPH_CORE_SIZE];

#undef PERIPH_BASE
#define PERIPH_BASE ((uint32_t)(uintptr_t)web_periph)
#undef AHB2PERIPH_BASE
#define AHB2PERIPH_BASE ((uint32_t)(uintptr_t)web_periph_ahb2)

// Core peripherals, all relative to 0xE0000000.
#undef SCS_BASE
#define SCS_BASE ((uint32_t)(uintptr_t)web_periph_core + 0xE000u)
#undef CoreDebug_BASE
#define CoreDebug_BASE ((uint32_t)(uintptr_t)web_periph_core + 0xEDF0u)
#undef ITM_BASE
#define ITM_BASE ((uint32_t)(uintptr_t)web_periph_core + 0x0000u)
#undef DWT_BASE
#define DWT_BASE ((uint32_t)(uintptr_t)web_periph_core + 0x1000u)

// Every access to DWT advances the cycle counter, so the firmware's
// DWT-based busy waits and LCD timeouts finish.
DWT_Type *web_dwt_access(void);
#undef DWT
#define DWT (web_dwt_access())

// ---- Cortex-M7 registers and DSP intrinsics -----------------------------------
// calcNoise() (Oscillator.c) reads the overflow flag after an addition; there
// is no flags register here, it also checks for the wrap explicitly.
#define __get_APSR() 0u

// cmsis_gcc.h only defines these for __ARM_FEATURE_DSP. __SSAT and __CLZ
// already have portable versions there.
#define __PKHBT(ARG1, ARG2, ARG3) ((((uint32_t)(ARG1)) & 0x0000FFFFUL) | \
                                   ((((uint32_t)(ARG2)) << (ARG3)) & 0xFFFF0000UL))

static inline int32_t web_sat16(int32_t x)
{
	return x > 32767 ? 32767 : (x < -32768 ? -32768 : x);
}

static inline uint32_t __QADD16(uint32_t op1, uint32_t op2)
{
	const int32_t lo = web_sat16((int16_t)op1 + (int16_t)op2);
	const int32_t hi = web_sat16((int16_t)(op1 >> 16) + (int16_t)(op2 >> 16));
	return ((uint32_t)hi << 16) | ((uint32_t)lo & 0xFFFFu);
}

static inline uint32_t __QSUB16(uint32_t op1, uint32_t op2)
{
	const int32_t lo = web_sat16((int16_t)op1 - (int16_t)op2);
	const int32_t hi = web_sat16((int16_t)(op1 >> 16) - (int16_t)(op2 >> 16));
	return ((uint32_t)hi << 16) | ((uint32_t)lo & 0xFFFFu);
}

static inline uint32_t __SADD16(uint32_t op1, uint32_t op2)
{
	const uint32_t lo = (uint16_t)((int16_t)op1 + (int16_t)op2);
	const uint32_t hi = (uint16_t)((int16_t)(op1 >> 16) + (int16_t)(op2 >> 16));
	return (hi << 16) | lo;
}

static inline int32_t __QADD(int32_t op1, int32_t op2)
{
	const int64_t r = (int64_t)op1 + op2;
	return r > INT32_MAX ? INT32_MAX : (r < INT32_MIN ? INT32_MIN : (int32_t)r);
}

static inline int32_t __QSUB(int32_t op1, int32_t op2)
{
	const int64_t r = (int64_t)op1 - op2;
	return r > INT32_MAX ? INT32_MAX : (r < INT32_MIN ? INT32_MIN : (int32_t)r);
}

static inline uint64_t __SMLALD(uint32_t op1, uint32_t op2, uint64_t acc)
{
	return acc + (int64_t)((int32_t)(int16_t)op1 * (int16_t)op2)
	           + (int64_t)((int32_t)(int16_t)(op1 >> 16) * (int16_t)(op2 >> 16));
}

#endif /* LXR_WEB_PORT_H_ */
