/*
 * plaits_presets.c
 *
 * Plaits' presets, six per genre (PLAN.md Phase 5): starting points, set
 * from what each engine's knobs do; the genre packs (Phase 6) tune them by
 * ear. Names fit the device's display (10 characters). Parameters: engine, harmonics, timbre, morph, decay, LPG colour,
 * attack, timbre envelope, morph envelope (64: none), volume, transpose (24: 0).
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "plaits_engine.h"

enum { VCF, VA, WSHP, FM, PD, WTBL, CHORD, STRNG, SPEECH, MODAL, PARTICLE };
enum { TRANCE, ELECTRO, SYNTHWAVE, DUB };

const PlaitsPreset om_plaits_presets[] = {
	{"Supersaw",  TRANCE,    {VA,       30,  20,  95,  70,  90,   0,  64,  64,  95, 24}},
	{"Pluck",          TRANCE,    {VCF,      70,  40,  30,  40,  60,   0, 100,  64, 100, 24}},
	{"Wave arp",  TRANCE,    {WTBL,     30,  60,  50,  45,  80,   0,  80,  64, 100, 24}},
	{"Trance pad",     TRANCE,    {STRNG,     0,  90,  64, 100,  40,  92,  64,  64,  95, 24}},
	{"Hoover",         TRANCE,    {PD,       80,  70,  60,  70,  90,   0,  64,  64,  90, 12}},
	{"FM bell",        TRANCE,    {FM,       90,  50,  20,  60, 100,   0, 100,  64,  95, 24}},

	{"Robot vox",    ELECTRO,   {SPEECH,   30,  64,  64,  50,  70,   0,  64,  64, 100, 24}},
	{"FM bass",   ELECTRO,   {FM,       32,  70,  40,  35,  40,   0,  90,  64, 100, 12}},
	{"CZ bass",        ELECTRO,   {PD,       40,  80,  30,  40,  50,   0,  95,  64, 100, 12}},
	{"Fold lead",    ELECTRO,   {WSHP,     50,  70,  64,  60,  80,   0,  64,  64,  95, 24}},
	{"Zap",            ELECTRO,   {VCF,     100,  20,  30,  25,  40,   0, 127,  64,  95, 24}},
	{"Vocoder",  ELECTRO,   {CHORD,    40,  50, 100,  50,  60,   0,  64,  64,  95, 24}},

	{"Poly brass",     SYNTHWAVE, {VCF,      40,  55,  45,  70,  50,  50,  90,  64,  95, 24}},
	{"Stringer",       SYNTHWAVE, {STRNG,     0,  30,  30, 100,  30,  80,  64,  64,  95, 24}},
	{"Retro bass",     SYNTHWAVE, {VA,        0,  50, 100,  40,  30,   0,  64,  64, 100, 12}},
	{"Sync lead",      SYNTHWAVE, {VA,       64, 110,  60,  70,  90,   0,  64,  64,  90, 24}},
	{"Glass keys",     SYNTHWAVE, {FM,       60,  40,  10,  70, 100,   0,  90,  64,  95, 24}},
	{"Chord mem",   SYNTHWAVE, {CHORD,    70,  40,  30,  60,  60,   0,  64,  64,  95, 24}},

	{"Dub chord",      DUB,       {CHORD,    60,  40,  40,  45,  30,   0,  90,  64,  95, 24}},
	{"Dub stab",       DUB,       {VCF,      60,  35,  35,  38,  30,   0, 100,  64, 100, 24}},
	{"Ping",           DUB,       {MODAL,    50,  60,  60,  64,  64,   0,  64,  64, 100, 24}},
	{"Crackle",        DUB,       {PARTICLE, 40,  30,  40,  80,  64,   0,  64,  64,  90, 24}},
	{"Deep pad",       DUB,       {STRNG,    40,  60,  20, 110,  20, 100,  64,  64,  95, 24}},
	{"Sub bass",       DUB,       {VCF,      30,  15,   0,  50,  20,   0,  64,  64, 100, 12}},
};

const int om_plaits_num_presets = sizeof(om_plaits_presets) / sizeof(om_plaits_presets[0]);
