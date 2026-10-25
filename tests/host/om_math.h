/*
 * om_math.h
 *
 * Force-included into every audio source of the native host build. The libm
 * functions the engines call at run time come from musl, as in the browser
 * build, so both builds compute them identically (musl_rename.h).
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef OM_MATH_H_
#define OM_MATH_H_

#ifdef LXR_HOST_MUSL_MATH
#define sinf om_sinf
#define cosf om_cosf
#define expf om_expf
#define exp om_exp
#define logf om_logf
#define exp2f om_exp2f
#define powf om_powf
#define sin om_sin
#endif

#endif /* OM_MATH_H_ */
