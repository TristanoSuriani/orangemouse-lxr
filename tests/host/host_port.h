/*
 * host_port.h
 *
 * Force-included before lxr_web_port.h in the native host build (tests/host/).
 * The firmware puts hot code and data into the MCU's tightly coupled memories
 * with __attribute__((section(".itcm_text"))) and similar (Soundengine/config.h,
 * main.h). Mach-O rejects those section names, and on the host there is no TCM,
 * so the attribute becomes a harmless one.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef HOST_PORT_H_
#define HOST_PORT_H_

#define section(name) used

#include "om_math.h"

#endif /* HOST_PORT_H_ */
