/*
 * plaits_test.c
 *
 * Plaits in the studio (engines/plaits/): every preset sounds at a low, a
 * middle and a high note, stays finite and bounded, and falls silent after
 * its note off; A4 is 440 Hz at the studio's 44.1 kHz (Plaits tunes itself
 * for 47872 Hz); a part plays 4-note chords and steals a voice for a fifth;
 * the parts are separate stems; every engine with all its knobs at 0 and at
 * 127 stays finite. Exit code 0 when every check passes.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "studio.h"
#include "plaits_engine.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define RATE 44100.f
static float L[OM_MAX_BLOCK], R[OM_MAX_BLOCK];

// renders blocks; returns the peak (INFINITY if anything is not finite); counts upward zero crossings
static float run(OmStudio *st, int blocks, int *crossings)
{
	static float last;
	float peak = 0.f;
	for (int b = 0; b < blocks; b++)
	{
		om_studio_render(st, L, R, OM_MAX_BLOCK);
		for (int i = 0; i < OM_MAX_BLOCK; i++)
		{
			if (!isfinite(L[i]) || !isfinite(R[i])) return INFINITY;
			peak = fmaxf(peak, fmaxf(fabsf(L[i]), fabsf(R[i])));
			if (crossings && last < 0.f && L[i] >= 0.f) (*crossings)++;
			last = L[i];
		}
	}
	return peak;
}

static OmStudio *fresh(void)
{
	OmStudio *st = om_studio_create(RATE);
	if (om_studio_add_engine(st, &om_plaits_engine) != 0)
		printf("FAIL: Plaits could not be created\n"), failures++;
	return st;
}

static void setAll(OmStudio *st, uint8_t part, const uint8_t *p)
{
	for (uint16_t i = 0; i < PLAITS_PRESET_PARAMS; i++)
		om_studio_set_param(st, om_studio_frame(st), 0, part, i, p[i]);
}

int main(void)
{
	// every preset, three notes: sound, bounded, then silence (pads within 9 s)
	OmStudio *st = fresh();
	const uint8_t notes[3] = {36, 60, 84};
	for (int k = 0; k < om_plaits_num_presets; k++)
	{
		const PlaitsPreset *pr = &om_plaits_presets[k];
		setAll(st, 0, pr->p);
		float on = 0.f, after = 0.f;
		for (int n = 0; n < 3; n++)
		{
			om_studio_note_on(st, om_studio_frame(st), 0, 0, notes[n], 110);
			on = fmaxf(on, run(st, 350, NULL));            // 0.5 s
			om_studio_note_off(st, om_studio_frame(st), 0, 0, notes[n]);
			run(st, 2100, NULL);                           // 3 s
			float tail = run(st, 20, NULL);
			for (int more = 0; more < 6 && tail >= 0.001f; more++)   // long pads: up to 9 s
				tail = run(st, 690, NULL);
			after = fmaxf(after, tail);
		}
		CHECK(isfinite(on) && on > 0.01f && on < 2.f, "%s: peak %g", pr->name, on);
		CHECK(after < 0.001f, "%s: still %g 9 s after its note off", pr->name, after);
	}
	om_studio_destroy(st);

	// tuning: a sine (2-op FM, no modulation) at A4
	st = fresh();
	const uint8_t sine[PLAITS_PRESET_PARAMS] = {3, 64, 0, 0, 64, 127, 0, 64, 64, 100, 24};
	setAll(st, 0, sine);
	om_studio_note_on(st, 0, 0, 0, 69, 100);
	run(st, 70, NULL);
	int crossings = 0;
	run(st, 689, &crossings);   // 1 s (689 blocks of 64 = 44096 frames)
	const float hz = crossings * RATE / (689 * 64.f);
	CHECK(fabsf(hz - 440.f) < 2.f, "A4 plays at %.1f Hz", hz);
	printf("A4: %.1f Hz\n", hz);
	om_studio_destroy(st);

	// chords: four notes sound together (each adds), a fifth takes the oldest's voice
	st = fresh();
	setAll(st, 0, om_plaits_presets[1].p);
	float single = 0.f, chord = 0.f;
	om_studio_note_on(st, 0, 0, 0, 60, 110);
	single = run(st, 100, NULL);
	for (uint8_t n = 64; n <= 70; n += 3)
		om_studio_note_on(st, om_studio_frame(st), 0, 0, n, 110);
	chord = run(st, 100, NULL);
	om_studio_note_on(st, om_studio_frame(st), 0, 0, 72, 110);
	const float five = run(st, 100, NULL);
	CHECK(chord > single * 1.3f && isfinite(five) && five < 2.f, "a chord: %g against one note %g, then %g", chord, single, five);
	om_studio_destroy(st);

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

	// every engine, all knobs at 0 and at 127
	for (uint8_t engine = 0; engine < 11; engine++)
		for (int extreme = 0; extreme < 2; extreme++)
		{
			st = fresh();
			uint8_t p[PLAITS_PRESET_PARAMS];
			memset(p, extreme ? 127 : 0, sizeof(p));
			p[0] = engine;
			p[9] = 100;   // volume 0 dB
			p[10] = 24;
			setAll(st, 0, p);
			om_studio_note_on(st, 0, 0, 0, 48, 127);
			om_studio_note_on(st, 0, 0, 0, 96, 127);
			const float peak = run(st, 350, NULL);
			CHECK(isfinite(peak) && peak < 4.f, "engine %d with every knob at %d: peak %g", engine, extreme ? 127 : 0, peak);
			om_studio_destroy(st);
		}

	if (failures)
	{
		printf("%d Plaits check(s) failed\n", failures);
		return 1;
	}
	printf("all Plaits checks passed (%d presets)\n", om_plaits_num_presets);
	return 0;
}
