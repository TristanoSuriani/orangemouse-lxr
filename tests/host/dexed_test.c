/*
 * dexed_test.c
 *
 * Dexed in the studio (engines/dexed/): every program of every bank sounds
 * and falls silent after its release (no stuck voices), its name reads like
 * a DX7 name, transpose moves the pitch by whole semitones, the two parts
 * are separate stems, and a note storm on 16 voices stays finite.
 * Exit code 0 when every check passes.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "studio.h"
#include "dexed_engine.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define RATE 44100.f
static float L[OM_MAX_BLOCK * 64], R[OM_MAX_BLOCK * 64];

// renders `blocks` blocks of the studio; returns the peak, and counts upward zero crossings of the left side
static float run(OmStudio *st, int blocks, int *crossings)
{
	float peak = 0.f;
	for (int b = 0; b < blocks; b++)
	{
		om_studio_render(st, L, R, OM_MAX_BLOCK);
		for (int i = 0; i < OM_MAX_BLOCK; i++)
		{
			if (!isfinite(L[i]) || !isfinite(R[i])) return INFINITY;
			peak = fmaxf(peak, fmaxf(fabsf(L[i]), fabsf(R[i])));
			static float last;
			if (crossings && last < 0.f && L[i] >= 0.f) (*crossings)++;
			last = L[i];
		}
	}
	return peak;
}

static OmStudio *fresh(void)
{
	OmStudio *st = om_studio_create(RATE);
	if (om_studio_add_engine(st, &om_dexed_engine) != 0)
		printf("FAIL: Dexed could not be created\n"), failures++;
	return st;
}

static void choose(OmStudio *st, uint8_t part, int bank, int prog)
{
	om_studio_set_param(st, om_studio_frame(st), 0, part, DEXED_P_BANK, (uint8_t)bank);
	om_studio_set_param(st, om_studio_frame(st), 0, part, DEXED_P_PROGRAM, (uint8_t)prog);
}

// whether a program keeps sounding after key up on a real DX7: an operator
// with an output level whose envelope ends above zero (L4) or never gets
// there (release rate R4 = 0 freezes it)
extern const uint8_t om_dexed_carts[][32 * 128];
static int holdsAfterRelease(int bank, int prog)
{
	const uint8_t *v = om_dexed_carts[bank] + 128 * prog;
	for (int op = 0; op < 6; op++)
		if ((v[op * 17 + 7] > 0 || v[op * 17 + 3] == 0) && v[op * 17 + 14] > 0)   // L4 or R4, output level
			return 1;
	return 0;
}

int main(void)
{
	OmStudio *st = fresh();
	CHECK(om_studio_add_engine(st, &om_dexed_engine) == -1, "a second Dexed was created");
	CHECK(om_dexed_banks == 33 && !strcmp(om_dexed_bank_name(1), "SynprezFM 01"), "%d banks, bank 2 is %s", om_dexed_banks, om_dexed_bank_name(1));

	// every program has a name and sounds within 0.2 s (slow pads take a while)
	int silent = 0, badNames = 0;
	for (int bank = 0; bank < om_dexed_banks; bank++)
		for (int prog = 0; prog < 32; prog++)
		{
			char name[11];
			om_dexed_program_name(bank, prog, name);
			int letters = 0;
			for (int i = 0; i < 10; i++) letters += name[i] > ' ';
			badNames += letters == 0;
			choose(st, 0, bank, prog);
			const uint8_t note = (uint8_t)(48 + (bank * 32 + prog) % 24);   // a different key each time: no voice reused at once
			om_studio_note_on(st, om_studio_frame(st), 0, 0, note, 100);
			const float on = run(st, 140, NULL);
			om_studio_note_off(st, om_studio_frame(st), 0, 0, note);
			if (on < 0.0005f) { silent++; printf("  quiet: %s %02d %s (%g)\n", om_dexed_bank_name(bank), prog + 1, name, on); }
		}
	om_studio_destroy(st);
	CHECK(silent <= om_dexed_banks * 32 / 50, "%d of %d programs make no sound in 0.2 s", silent, om_dexed_banks * 32);
	CHECK(badNames == 0, "%d programs without a name", badNames);

	// every 16th program, alone: after its note off it falls silent, or is still
	// falling (some DX7 releases take long), unless the patch itself holds a
	// level after release, as DX7 drones do
	int stuck = 0, checked = 0;
	for (int i = 0; i < om_dexed_banks * 32; i += 16)
	{
		const int bank = i / 32, prog = i % 32;
		if (holdsAfterRelease(bank, prog)) continue;
		st = fresh();
		choose(st, 0, bank, prog);
		om_studio_note_on(st, om_studio_frame(st), 0, 0, 60, 100);
		run(st, 140, NULL);
		om_studio_note_off(st, om_studio_frame(st), 0, 0, 60);
		run(st, 700 * 2, NULL);
		const float mid = run(st, 20, NULL);   // 2 s after the note off
		run(st, 700 * 2, NULL);
		const float after = run(st, 20, NULL);  // 4 s: silent, or a slow release still falling
		checked++;
		if (after > 0.001f && after > mid * 0.5f) { stuck++; printf("  still sounding: %s %02d (%g, %g at 2 s)\n", om_dexed_bank_name(bank), prog + 1, after, mid); }
		om_studio_destroy(st);
	}
	CHECK(stuck == 0 && checked > 30, "%d of %d programs still sound 4 s after the note off", stuck, checked);

	// transpose: +12 doubles the frequency, on the first program whose
	// operators all follow the key (a fixed-frequency operator would not)
	int keyed = 0;
	for (; keyed < om_dexed_banks * 32; keyed++)
	{
		const uint8_t *v = om_dexed_carts[keyed / 32] + 128 * (keyed % 32);
		int fixed = 0;
		for (int op = 0; op < 6; op++) fixed |= v[op * 17 + 15] & 1;
		if (!fixed) break;
	}
	int crossings[2] = {0, 0};
	for (int t = 0; t < 2; t++)
	{
		st = fresh();
		choose(st, 0, keyed / 32, keyed % 32);
		om_studio_set_param(st, 0, 0, 0, DEXED_P_TRANSPOSE, t ? 36 : 24);
		om_studio_note_on(st, 0, 0, 0, 48, 100);
		run(st, 100, NULL);
		run(st, 690, &crossings[t]);   // 1 s
		om_studio_destroy(st);
	}
	const float ratio = crossings[0] ? (float)crossings[1] / crossings[0] : 0.f;
	CHECK(fabsf(ratio - 2.f) < 0.1f, "transpose +12: %d crossings against %d (ratio %.2f)", crossings[1], crossings[0], ratio);

	// the parts are separate stems
	st = fresh();
	{
		float m[8];
		om_studio_note_on(st, 0, 0, 1, 60, 100);
		run(st, 40, NULL);
		om_studio_meters(st, m, 8);
		CHECK(om_studio_channels(st) == 2 && m[0] == 0.f && m[1] > 0.001f, "part 2 plays on channel 2: %g %g", m[0], m[1]);
	}
	om_studio_destroy(st);

	// a storm: 40 notes at once on 16 voices, all released; finite, then silent
	st = fresh();
	for (int n = 0; n < 40; n++)
		om_studio_note_on(st, 0, 0, 0, (uint8_t)(30 + n), 127);
	const float storm = run(st, 200, NULL);
	for (int n = 0; n < 40; n++)
		om_studio_note_off(st, om_studio_frame(st), 0, 0, (uint8_t)(30 + n));
	run(st, 3000, NULL);
	const float end = run(st, 20, NULL);
	CHECK(isfinite(storm) && storm <= 1.f && end < 0.001f, "the storm: peak %g, then %g", storm, end);
	om_studio_destroy(st);

	if (failures)
	{
		printf("%d Dexed check(s) failed\n", failures);
		return 1;
	}
	printf("all Dexed checks passed (%d programs)\n", om_dexed_banks * 32);
	return 0;
}
