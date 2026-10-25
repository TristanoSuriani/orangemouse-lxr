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

// The libm functions the firmware calls at run time come from musl, as in the
// browser build, so both builds compute them identically (musl_rename.h).
#ifdef LXR_HOST_MUSL_MATH
#define sinf om_sinf
#define cosf om_cosf
#define expf om_expf
#define exp om_exp
#define logf om_logf
#endif

#endif /* HOST_PORT_H_ */
