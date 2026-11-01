/*
 * rings_test.c
 *
 * Rings in the studio (engines/rings/): it is silent, exactly, until a note;
 * every preset and every model rings at a low, a middle and a high note,
 * stays finite and bounded, and falls back to exact silence; A4 is 440 Hz at
 * the studio's 44.1 kHz (Rings runs at 48 kHz, resampled); a chord strums
 * its notes; every model with all its knobs at 0 and at 127 stays finite;
 * with INPUT set, a channel's sound excites it. Exit code 0 when every check
 * passes, and a hash of everything rendered (native and WebAssembly must
 * agree). RINGS_LOUDNESS=1 prints each preset's loudness, for balancing.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "studio.h"
#include "rings_engine.h"
#include "om_registry_rings.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define RATE 44100.f
#define SECOND 690   // blocks
static float L[OM_MAX_BLOCK], R[OM_MAX_BLOCK];

// every sample rendered, hashed (FNV-1a over the bits): tests/run.sh compares
// the native build's with WebAssembly's
static uint64_t hash = 1469598103934665603ull;
static void hashSample(float x)
{
	uint32_t bits;
	memcpy(&bits, &x, 4);
	for (int k = 0; k < 4; k++)
		hash = (hash ^ ((bits >> (8 * k)) & 0xff)) * 1099511628211ull;
}

// renders blocks; returns the peak (INFINITY if anything is not finite); counts upward zero crossings
static float run(OmStudio *st, int blocks, int *crossings, int *nonzero)
{
	static float last;
	float peak = 0.f;
	for (int b = 0; b < blocks; b++)
	{
		om_studio_render(st, L, R, OM_MAX_BLOCK);
		for (int i = 0; i < OM_MAX_BLOCK; i++)
		{
			hashSample(L[i]);
			hashSample(R[i]);
			if (!isfinite(L[i]) || !isfinite(R[i])) return INFINITY;
			peak = fmaxf(peak, fmaxf(fabsf(L[i]), fabsf(R[i])));
			if (crossings && last < 0.f && L[i] >= 0.f) (*crossings)++;
			if (nonzero && (L[i] != 0.f || R[i] != 0.f)) (*nonzero)++;
			last = L[i];
		}
	}
	return peak;
}

// ---- a mock engine before Rings: a burst of noise after each note on ----

typedef struct { int left; uint32_t seed; } Burst;
static void *burstCreate(float rate) { (void)rate; return calloc(1, sizeof(Burst)); }
static void burstDestroy(void *self) { free(self); }
static void burstNoteOn(void *self, uint8_t part, uint8_t note, uint8_t vel) { (void)part; (void)note; (void)vel; ((Burst *)self)->left = 2000; }
static void burstNoteOff(void *self, uint8_t part, uint8_t note) { (void)self; (void)part; (void)note; }
static void burstParam(void *self, uint8_t part, uint16_t param, uint8_t value) { (void)self; (void)part; (void)param; (void)value; }
static uint8_t burstGetParam(void *self, uint8_t part, uint16_t param) { (void)self; (void)part; (void)param; return 0; }
static void burstTempo(void *self, float bpm) { (void)self; (void)bpm; }
static void burstRender(void *self, float *const *stems, uint32_t frames)
{
	Burst *b = self;
	for (uint32_t i = 0; i < frames; i++)
	{
		b->seed = b->seed * 1664525u + 1013904223u;
		const float x = b->left > 0 ? ((b->seed >> 9) * (1.f / 4194304.f) - 1.f) * 0.3f : 0.f;
		if (b->left > 0) b->left--;
		stems[0][i] = stems[1][i] = x;
	}
}
static const OmEngineClass burst = {"burst", 1, 1, 0, NULL, 0, burstCreate, burstDestroy, burstNoteOn, burstNoteOff,
	burstParam, burstGetParam, burstTempo, NULL, burstRender, NULL, NULL};

static OmStudio *fresh(int withBurst)
{
	OmStudio *st = om_studio_create(RATE);
	om_studio_dc_blocker(st, 0);   // exact zeros stay exact
	if (withBurst)
		om_studio_add_engine(st, &burst);
	if (om_studio_add_engine(st, &om_rings_engine) != withBurst)
		printf("FAIL: Rings could not be created\n"), failures++;
	return st;
}

static void setAll(OmStudio *st, uint8_t engine, const uint8_t *p)
{
	for (uint16_t i = 0; i < RINGS_PRESET_PARAMS; i++)
		om_studio_set_param(st, om_studio_frame(st), engine, 0, i, p[i]);
}

static void loudness(void)
{
	for (int k = 0; k < OM_RINGS_NUM_PRESETS; k++)
	{
		OmStudio *st = fresh(0);
		uint8_t p[RINGS_PRESET_PARAMS];
		memcpy(p, om_rings_presets[k].p, sizeof p);
		p[RN_P_VOLUME] = 100;
		setAll(st, 0, p);
		double e = 0;
		long count = 0;
		const uint8_t notes[4] = {60, 64, 67, 48};
		for (int i = 0; i < 4; i++)
		{
			om_studio_note_on(st, om_studio_frame(st), 0, 0, notes[i], 100);
			for (int b = 0; b < SECOND / 2; b++)
			{
				om_studio_render(st, L, R, OM_MAX_BLOCK);
				for (int j = 0; j < OM_MAX_BLOCK; j++, count += 2) e += (double)L[j] * L[j] + (double)R[j] * R[j];
			}
		}
		printf("%-12s %6.1f dBFS\n", om_rings_presets[k].name, 10 * log10(e / count + 1e-12));
		om_studio_destroy(st);
	}
}

int main(void)
{
	if (getenv("RINGS_LOUDNESS")) { loudness(); return 0; }

	{   // silent, exactly, before any note (the studio adds nothing to the mix)
		OmStudio *st = fresh(0);
		int nonzero = 0;
		run(st, SECOND, NULL, &nonzero);
		CHECK(nonzero == 0, "%d samples not zero before any note", nonzero);
		om_studio_destroy(st);
	}
	{   // every preset rings at C2, C4 and C6, stays bounded, then falls back to exact silence
		for (int k = 0; k < OM_RINGS_NUM_PRESETS; k++)
		{
			OmStudio *st = fresh(0);
			setAll(st, 0, om_rings_presets[k].p);
			float peak = 0.f, low = INFINITY;
			const uint8_t notes[3] = {36, 60, 84};
			for (int i = 0; i < 3; i++)
			{
				om_studio_note_on(st, om_studio_frame(st), 0, 0, notes[i], 100);
				const float p = run(st, SECOND / 2, NULL, NULL);
				peak = fmaxf(peak, p);
				low = fminf(low, p);
			}
			CHECK(low > 0.01f && peak < 1.f, "%s: peaks from %g to %g", om_rings_presets[k].name, low, peak);
			run(st, 30 * SECOND, NULL, NULL);   // long damping rings for seconds
			int nonzero = 0;
			run(st, SECOND, NULL, &nonzero);
			CHECK(nonzero == 0, "%s: %d samples not zero 30 s after the last note", om_rings_presets[k].name, nonzero);
			om_studio_destroy(st);
		}
	}
	{   // A4 is 440 Hz: the plucked string, one voice, no stiffness (STRUCTURE sharpens its
		// partials: at 40 % the strongest is 2.8 % high), dark, struck in the middle
		OmStudio *st = fresh(0);
		const uint8_t p[RINGS_PRESET_PARAMS] = {2, 0, 0, 20, 120, 64, 100, 24};
		setAll(st, 0, p);
		om_studio_note_on(st, 0, 0, 0, 69, 100);
		run(st, SECOND / 5, NULL, NULL);
		// the period: the strongest autocorrelation between 220 and 880 Hz (a string's
		// harmonics cross zero more often than its fundamental), refined by a parabola
		enum { N = 8192 };
		static float x[N];
		for (int b = 0; b < N / OM_MAX_BLOCK; b++)
		{
			om_studio_render(st, L, R, OM_MAX_BLOCK);
			memcpy(x + b * OM_MAX_BLOCK, L, sizeof L);
		}
		double c[202] = {0};
		int best = 50;
		for (int lag = 49; lag <= 201; lag++)
		{
			for (int i = 0; i + lag < N; i++) c[lag] += (double)x[i] * x[i + lag];
			if (lag > 49 && lag < 201 && c[lag] > c[best]) best = lag;
		}
		const double a = c[best - 1], b = c[best], d = c[best + 1];
		const double period = best + 0.5 * (a - d) / (a - 2.0 * b + d);
		const float hz = (float)(RATE / period);
		CHECK(fabsf(hz - 440.f) < 440.f * 0.01f, "A4: %.1f Hz", hz);
		printf("A4: %.1f Hz\n", hz);
		om_studio_destroy(st);
	}
	{   // a chord strums: all four voices of the modal model ring together
		OmStudio *st = fresh(0);
		const uint8_t one[RINGS_PRESET_PARAMS] = {0, 0, 40, 70, 110, 50, 100, 24};
		setAll(st, 0, one);
		om_studio_note_on(st, 0, 0, 0, 60, 100);
		const float single = run(st, SECOND / 2, NULL, NULL);
		om_studio_destroy(st);
		st = fresh(0);
		const uint8_t four[RINGS_PRESET_PARAMS] = {0, 2, 40, 70, 110, 50, 100, 24};
		setAll(st, 0, four);
		for (int i = 0; i < 4; i++)
			om_studio_note_on(st, 0, 0, 0, (uint8_t)(60 + 4 * i), 100);
		const float chord = run(st, SECOND / 2, NULL, NULL);
		CHECK(chord > 1.2f * single, "a 4-note chord peaks at %g, one note at %g", chord, single);
		om_studio_destroy(st);
	}
	{   // every model with every knob at 0, then at 127: finite and bounded
		for (uint8_t m = 0; m <= om_rings_params[RN_P_MODEL].max; m++)
			for (int hi = 0; hi < 2; hi++)
			{
				OmStudio *st = fresh(0);
				const uint8_t k = hi ? 127 : 0;
				const uint8_t p[RINGS_PRESET_PARAMS] = {m, hi ? 2 : 0, k, k, k, k, 127, hi ? 48 : 0};
				setAll(st, 0, p);
				float peak = 0.f;
				for (int i = 0; i < 8; i++)
				{
					om_studio_note_on(st, om_studio_frame(st), 0, 0, (uint8_t)(hi ? 127 - i : i), 127);
					peak = fmaxf(peak, run(st, SECOND / 4, NULL, NULL));
				}
				CHECK(isfinite(peak) && peak < 2.f, "model %d, knobs at %d: peak %g", m, k, peak);
				om_studio_destroy(st);
			}
	}
	{   // INPUT: a burst on channel 0 rings through Rings at its note; without it, nothing
		for (int in = 0; in < 2; in++)
		{
			OmStudio *st = fresh(1);
			const uint8_t p[RINGS_PRESET_PARAMS] = {0, 0, 40, 70, 110, 50, 100, 24};
			setAll(st, 1, p);
			om_studio_set_param(st, 0, 1, 0, RN_P_INPUT, in ? 1 : 0);
			om_studio_mix(st, 0, OM_MIX_MUTE, 1);    // only Rings is heard
			run(st, SECOND / 10, NULL, NULL);
			om_studio_note_on(st, om_studio_frame(st), 0, 0, 60, 100);   // the burst, not a Rings note
			const float peak = run(st, SECOND / 2, NULL, NULL);
			if (in)
				CHECK(peak > 0.01f, "a burst through INPUT: peak %g", peak);
			else
				CHECK(peak == 0.f, "INPUT off, a burst elsewhere: peak %g", peak);
			om_studio_destroy(st);
		}
	}

	if (failures)
	{
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("hash %016llx\n", (unsigned long long)hash);
	printf("all Rings checks passed (%d presets)\n", OM_RINGS_NUM_PRESETS);
	return 0;
}
