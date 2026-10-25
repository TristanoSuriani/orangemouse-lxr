/*
 * param_info.h
 *
 * What the studio knows about an engine's parameters, generated from the
 * parameter registry (registry/<engine>.json, tools/registry.py) into
 * generated/registry/om_registry_<engine>.h. The UI's richer view of the same
 * data (value display, help lines) is web/app/registry/<engine>.js.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef OM_PARAM_INFO_H_
#define OM_PARAM_INFO_H_

#include <stdint.h>

#define OM_PARAM_UNUSED 0x01   // kept for compatibility; the engine ignores it

typedef struct
{
	const char *id;          // the engine's enum name, e.g. "VA_P_CUTOFF"
	const char *shortName;   // the LCD's 3 characters, e.g. "CUT"
	const char *name;        // e.g. "Cutoff"
	uint8_t min, max, def;
	uint8_t flags;
} OmParamInfo;

#endif /* OM_PARAM_INFO_H_ */
