/*
 * dexed_engine.h
 *
 * Dexed in the studio (PLAN.md Phase 4): the DX7 engine of Dexed (msfa and
 * EngineMkI, third_party/dexed), playing the DX7 cartridges Dexed ships with.
 * Presets only, as on a DX7 played from its cartridges: two parts (DX1, DX2),
 * each choosing a bank and a program, with a volume and a transpose.
 * 16 voices per part; each part is one stem (the DX7 is mono).
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef OM_DEXED_ENGINE_H_
#define OM_DEXED_ENGINE_H_

#include <stdint.h>
#include "engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEXED_PARTS 2

// a part's parameters (registry/dexed.json)
enum { DEXED_P_BANK, DEXED_P_PROGRAM, DEXED_P_VOLUME, DEXED_P_TRANSPOSE, DEXED_NUM_PARAMS };

extern const OmEngineClass om_dexed_engine;

// The cartridges (generated from Dexed's builtin_pgm.zip by tools/dexed_carts.py).
extern const int om_dexed_banks;
const char *om_dexed_bank_name(int bank);
// a program's name: 10 characters, as on the DX7's display
void om_dexed_program_name(int bank, int program, char out[11]);

#ifdef __cplusplus
}
#endif

#endif /* OM_DEXED_ENGINE_H_ */
