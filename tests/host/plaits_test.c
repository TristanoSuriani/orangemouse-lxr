/*
 * plaits_test.c
 *
 * Plaits in the studio (engines/plaits/): every preset sounds at a low, a
 * middle and a high note, stays finite and bounded, and falls silent after
 * its note off; A4 is 440 Hz at the studio's 44.1 kHz; a part plays 4-note
 * chords and steals a voice for a fifth; the parts are separate stems; every
 * engine with all its knobs at 0 and at 127 stays finite; percussive presets
 * decay as fast, in seconds, as at Plaits' own rate (it runs at that rate,
 * resampled). Exit code 0 when every check passes.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "studio.h"
#include "plaits_engine.h"
#include "om_registry_plaits.h"

#define om_plaits_num_presets OM_PLAITS_NUM_PRESETS

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

// PLAITS_LOUDNESS=1: each preset's loudness (RMS of a held C4 and a C3/C5
// pair over 1 s at volume 0 dB), for balancing their volumes
static void loudness(void)
{
	for (int k = 0; k < om_plaits_num_presets; k++)
	{
		OmStudio *st = fresh();
		uint8_t p[PLAITS_PRESET_PARAMS];
		memcpy(p, om_plaits_presets[k].p, sizeof p);
		p[PL_P_VOLUME] = 100;
		setAll(st, 0, p);
		double e = 0;
		long count = 0;
		const uint8_t notes[3] = {60, 48, 72};
		for (int i = 0; i < 3; i++)
		{
			om_studio_note_on(st, om_studio_frame(st), 0, 0, notes[i], 100);
			for (int b = 0; b < 690; b++)
			{
				om_studio_render(st, L, R, OM_MAX_BLOCK);
				for (int j = 0; j < OM_MAX_BLOCK; j++, count += 2) e += (double)L[j] * L[j] + (double)R[j] * R[j];
			}
			om_studio_note_off(st, om_studio_frame(st), 0, 0, notes[i]);
			run(st, 2000, NULL);
		}
		printf("%-12s %6.1f dBFS\n", om_plaits_presets[k].name, 10 * log10(e / count + 1e-12));
		om_studio_destroy(st);
	}
}

// PLAITS_WAVS=<dir>: each preset as a WAV (C4, then C3 and C5), for listening
static void wavs(const char *dir)
{
	char path[512];
	snprintf(path, sizeof path, "mkdir -p '%s'", dir);
	if (system(path) != 0) return;
	for (int k = 0; k < om_plaits_num_presets; k++)
	{
		OmStudio *st = fresh();
		setAll(st, 0, om_plaits_presets[k].p);
		enum { FRAMES = 44100 * 6 };
		static int16_t pcm[FRAMES * 2];
		const uint8_t notes[3] = {60, 48, 72};
		uint32_t f = 0;
		for (int i = 0; i < 3; i++)
		{
			om_studio_note_on(st, om_studio_frame(st), 0, 0, notes[i], 100);
			for (int b = 0; b < 1378; b++, f += OM_MAX_BLOCK)   // 2 s a note: 1 s held, 1 s release
			{
				if (b == 689) om_studio_note_off(st, om_studio_frame(st), 0, 0, notes[i]);
				om_studio_render(st, L, R, OM_MAX_BLOCK);
				for (int j = 0; j < OM_MAX_BLOCK && f + j < FRAMES; j++)
				{
					pcm[2 * (f + j)] = (int16_t)(fmaxf(-1.f, fminf(1.f, L[j])) * 32767.f);
					pcm[2 * (f + j) + 1] = (int16_t)(fmaxf(-1.f, fminf(1.f, R[j])) * 32767.f);
				}
			}
		}
		om_studio_destroy(st);
		snprintf(path, sizeof path, "%s/%02d %s.wav", dir, k + 1, om_plaits_presets[k].name);
		FILE *out = fopen(path, "wb");
		if (!out) continue;
		const uint32_t data = FRAMES * 4, riff = 36 + data, fmt = 16, rate = 44100, br = rate * 4;
		const uint16_t pcmFormat = 1, ch = 2, ba = 4, bits = 16;
		fwrite("RIFF", 1, 4, out); fwrite(&riff, 4, 1, out); fwrite("WAVEfmt ", 1, 8, out);
		fwrite(&fmt, 4, 1, out); fwrite(&pcmFormat, 2, 1, out); fwrite(&ch, 2, 1, out);
		fwrite(&rate, 4, 1, out); fwrite(&br, 4, 1, out); fwrite(&ba, 2, 1, out); fwrite(&bits, 2, 1, out);
		fwrite("data", 1, 4, out); fwrite(&data, 4, 1, out); fwrite(pcm, 4, FRAMES, out);
		fclose(out);
	}
}

// How long a preset takes, in seconds, to fall 40 dB after its note off
// (from its level then), at a sample rate: the decay as heard. Levels are
// the RMS of 10 ms windows, the crossing interpolated between them in dB.
static float decaySeconds(float rate, int preset, uint8_t note)
{
	OmStudio *st = om_studio_create(rate);
	om_studio_add_engine(st, &om_plaits_engine);
	setAll(st, 0, om_plaits_presets[preset].p);
	run(st, 10, NULL);
	om_studio_note_on(st, om_studio_frame(st), 0, 0, note, 110);
	run(st, (int)(0.05f * rate / OM_MAX_BLOCK), NULL);   // held 50 ms
	om_studio_note_off(st, om_studio_frame(st), 0, 0, note);
	const int window = (int)(0.01f * rate + 0.5f);
	double energy = 0.0, first = 0.0, before = 0.0;
	int n = 0, w = 0;
	float seconds = 10.f;
	for (int b = 0; b < (int)(10.f * rate / OM_MAX_BLOCK) && seconds == 10.f; b++)
	{
		om_studio_render(st, L, R, OM_MAX_BLOCK);
		for (int i = 0; i < OM_MAX_BLOCK && seconds == 10.f; i++)
		{
			energy += (double)L[i] * L[i];
			if (++n < window) continue;
			const double db = 10.0 * log10(energy / n + 1e-30);
			energy = 0.0, n = 0;
			if (w == 0) first = db;
			else if (db < first - 40.0)
				seconds = (float)(0.01 * (w - 1 + (before - (first - 40.0)) / (before - db)));
			before = db;
			w++;
		}
	}
	om_studio_destroy(st);
	return seconds;
}

// Plaits' envelopes and low-pass gate are timed for its own 47872 Hz: in
// seconds, a preset's decay must be the same here as there (PLAN.md 7.6).
static void timing(void)
{
	static const char *names[] = {"Pluck", "FM bell", "Dub stab", "Ping", "Glass keys"};
	for (int i = 0; i < 5; i++)
	{
		int k = 0;
		while (k < om_plaits_num_presets && strcmp(om_plaits_presets[k].name, names[i])) k++;
		if (k == om_plaits_num_presets) { CHECK(0, "no preset %s", names[i]); continue; }
		const float here = decaySeconds(RATE, k, 60), there = decaySeconds(47872.34f, k, 60);
		printf("%s: falls 40 dB in %.3f s here, %.3f s at Plaits' own rate (%+.1f %%)\n", names[i], here, there, 100.f * (here / there - 1.f));
		CHECK(fabsf(here / there - 1.f) < 0.03f, "%s decays in %.3f s, %.3f s at Plaits' own rate", names[i], here, there);
	}
}

int main(void)
{
	if (getenv("PLAITS_TIMING")) { timing(); return failures ? 1 : 0; }
	timing();
	if (getenv("PLAITS_LOUDNESS")) { loudness(); return 0; }
	if (getenv("PLAITS_WAVS")) { wavs(getenv("PLAITS_WAVS")); return 0; }
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
