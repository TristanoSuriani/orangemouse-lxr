/*
 * studio_render.c
 *
 * The studio's demo song (PLAN.md Phase 0, step 5): the LXR and Orange Mouse VA
 * through the studio's router and mixer, 8 s at 120 BPM. The LXR plays the
 * demo groove; VA1 a 16th-note bass line, VA2 a chord on every bar, VA3 a
 * lead in eighths, VA4 a noise swell every two bars. Prints the time per
 * 64-frame block and a hash of the mix; optionally writes it as raw float32
 * (left block, right block, ...) and as a WAV.
 *
 *   studio_render [--seconds S] [--rate HZ] [--raw out.f32] [--wav out.wav]
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "studio.h"

extern const OmEngineClass om_lxr_engine, om_va_engine, om_dexed_engine, om_plaits_engine;

void web_log(const char *msg) { fprintf(stderr, "lxr: %s\n", msg); }
void web_sd_written(uint32_t firstSector, uint32_t count) { (void)firstSector; (void)count; }
void web_midi_out(uint8_t byte) { (void)byte; }

static void writeWav(const char *path, const float *l, const float *r, uint32_t frames, uint32_t rate)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return;
	const uint32_t data = frames * 4, riff = 36 + data, fmt = 16, br = rate * 4;
	const uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
	fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
	fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
	fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
	fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
	for (uint32_t i = 0; i < frames; i++)
		for (int c = 0; c < 2; c++)
		{
			float v = c ? r[i] : l[i];
			v = v > 1.f ? 1.f : v < -1.f ? -1.f : v;
			const int16_t s = (int16_t)(v * 32767.f);
			fwrite(&s, 2, 1, f);
		}
	fclose(f);
}

// the song, in 16th-note steps
static void schedule(OmStudio *st, int lxr, int va, int dx, int pl, float rate, int steps)
{
	static const char *drums[7] = {
		"x...x...x...x...", "......x.......x.", "..........x.....", "....x.......x...",
		"x...............", "..x...x...x...x.", "...............x"};
	static const uint8_t drumNotes[7] = {36, 43, 50, 38, 49, 42, 46};
	static const uint8_t bass[16] = {33, 33, 45, 33, 36, 33, 43, 33, 31, 31, 43, 31, 34, 31, 41, 43};
	static const uint8_t chords[4][4] = {{57, 60, 64, 67}, {53, 57, 60, 64}, {48, 52, 55, 59}, {55, 59, 62, 65}};
	static const uint8_t lead[8] = {76, 74, 72, 74, 76, 79, 77, 74};
	const double step = rate * 60.0 / 120.0 / 4.0;
	for (int s = 0; s < steps; s++)
	{
		const uint64_t t = (uint64_t)(s * step + 0.5), off = (uint64_t)((s + 1) * step + 0.5) - 1;
		for (uint8_t v = 0; v < 7; v++)
			if (drums[v][s % 16] == 'x')
				om_studio_note_on(st, t, (uint8_t)lxr, v, drumNotes[v], s % 4 == 0 ? 120 : 90);
		om_studio_note_on(st, t, (uint8_t)va, 0, bass[s % 16], 100);
		om_studio_note_off(st, off, (uint8_t)va, 0, bass[s % 16]);
		if (s % 16 == 0)
			for (int n = 0; n < 4; n++)
			{
				const uint8_t note = chords[(s / 16) % 4][n];
				om_studio_note_on(st, t, (uint8_t)va, 1, note, 80);
				om_studio_note_off(st, t + (uint64_t)(12 * step), (uint8_t)va, 1, note);
			}
		if (s % 2 == 0)
		{
			const uint8_t note = lead[(s / 2) % 8];
			om_studio_note_on(st, t, (uint8_t)va, 2, note, 90);
			om_studio_note_off(st, t + (uint64_t)(1.5 * step), (uint8_t)va, 2, note);
		}
		if (s % 8 == 6)   // Dexed: off-beat stabs of the chord, an octave up
			for (int n = 0; n < 3; n++)
			{
				const uint8_t note = (uint8_t)(chords[(s / 16) % 4][n] + 12);
				om_studio_note_on(st, t, (uint8_t)dx, 0, note, 90);
				om_studio_note_off(st, t + (uint64_t)step, (uint8_t)dx, 0, note);
			}
		if (s % 16 == 10)   // Plaits: a dub chord on the and of beat 3
		{
			om_studio_note_on(st, t, (uint8_t)pl, 0, 50, 100);
			om_studio_note_off(st, t + (uint64_t)(2 * step), (uint8_t)pl, 0, 50);
		}
		if (s % 32 == 0)
		{
			om_studio_note_on(st, t, (uint8_t)va, 3, 60, 70);
			om_studio_note_off(st, t + (uint64_t)(24 * step), (uint8_t)va, 3, 60);
		}
	}
}

int main(int argc, char **argv)
{
	float seconds = 8.f, rate = 44100.f;
	const char *rawPath = NULL, *wavPath = NULL;
	for (int i = 1; i + 1 < argc; i += 2)
	{
		if (!strcmp(argv[i], "--seconds")) seconds = (float)atof(argv[i + 1]);
		else if (!strcmp(argv[i], "--rate")) rate = (float)atof(argv[i + 1]);
		else if (!strcmp(argv[i], "--raw")) rawPath = argv[i + 1];
		else if (!strcmp(argv[i], "--wav")) wavPath = argv[i + 1];
		else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
	}

	OmStudio *st = om_studio_create(rate);
	const int lxr = om_studio_add_engine(st, &om_lxr_engine);
	const int va = om_studio_add_engine(st, &om_va_engine);
	const int dx = om_studio_add_engine(st, &om_dexed_engine);
	const int pl = om_studio_add_engine(st, &om_plaits_engine);
	if (lxr < 0 || va < 0 || dx < 0 || pl < 0) { fprintf(stderr, "cannot create the engines\n"); return 1; }
	{   // Plaits' first part: the "Dub chord" preset, into the hall
		static const uint8_t dub[11] = {6, 60, 40, 40, 45, 30, 0, 90, 64, 95, 24};
		for (uint16_t i = 0; i < 11; i++)
			om_studio_set_param(st, 0, (uint8_t)pl, 0, i, dub[i]);
		om_studio_mix(st, (uint8_t)om_studio_channel(st, (uint8_t)pl, 0), OM_MIX_SEND_A, 85);
	}
	om_studio_set_param(st, 0, (uint8_t)dx, 0, 0, 1);   // bank: SynprezFM 01
	om_studio_set_param(st, 0, (uint8_t)dx, 0, 1, 10);  // program 11
	om_studio_mix(st, (uint8_t)om_studio_channel(st, (uint8_t)dx, 0), OM_MIX_PAN, 84);
	om_studio_mix(st, (uint8_t)om_studio_channel(st, (uint8_t)dx, 0), OM_MIX_SEND_B, 70);
	om_studio_set_tempo(st, 120.f);
	for (uint8_t s = 0; s < 4; s++)
	{
		const uint8_t c = (uint8_t)om_studio_channel(st, (uint8_t)va, s);
		om_studio_mix(st, c, OM_MIX_LEVEL, 79);   // -6 dB
		om_studio_mix(st, c, OM_MIX_PAN, s == 1 ? 45 : s == 2 ? 83 : 64);
	}
	// the send effects: the snare and VA's poly into the hall, the lead into the delay
	om_studio_mix(st, (uint8_t)om_studio_channel(st, (uint8_t)lxr, 3), OM_MIX_SEND_A, 70);
	om_studio_mix(st, (uint8_t)om_studio_channel(st, (uint8_t)va, 1), OM_MIX_SEND_A, 80);
	om_studio_mix(st, (uint8_t)om_studio_channel(st, (uint8_t)va, 2), OM_MIX_SEND_B, 75);

	const uint32_t blocks = (uint32_t)(seconds * rate / OM_MAX_BLOCK);
	schedule(st, lxr, va, dx, pl, rate, (int)(seconds * 120.f / 60.f * 4.f));   // all of it: it fits the queues

	float *l = malloc(sizeof(float) * OM_MAX_BLOCK * blocks), *r = malloc(sizeof(float) * OM_MAX_BLOCK * blocks);
	FILE *raw = rawPath ? fopen(rawPath, "wb") : NULL;
	uint64_t hash = 1469598103934665603ull;
	double busy = 0;
	for (uint32_t b = 0; b < blocks; b++)
	{
		const clock_t t0 = clock();
		om_studio_render(st, l + b * OM_MAX_BLOCK, r + b * OM_MAX_BLOCK, OM_MAX_BLOCK);
		busy += (double)(clock() - t0) / CLOCKS_PER_SEC;
		const uint8_t *bytes = (const uint8_t *)(l + b * OM_MAX_BLOCK);
		for (size_t i = 0; i < sizeof(float) * OM_MAX_BLOCK; i++)
			hash = (hash ^ bytes[i]) * 1099511628211ull;
		bytes = (const uint8_t *)(r + b * OM_MAX_BLOCK);
		for (size_t i = 0; i < sizeof(float) * OM_MAX_BLOCK; i++)
			hash = (hash ^ bytes[i]) * 1099511628211ull;
		if (raw)
		{
			fwrite(l + b * OM_MAX_BLOCK, sizeof(float), OM_MAX_BLOCK, raw);
			fwrite(r + b * OM_MAX_BLOCK, sizeof(float), OM_MAX_BLOCK, raw);
		}
	}
	if (raw)
		fclose(raw);
	if (wavPath)
		writeWav(wavPath, l, r, blocks * OM_MAX_BLOCK, (uint32_t)rate);
	printf("blocks %u of %d frames, %.2f us/block, %.2f %% of a core\n", blocks, OM_MAX_BLOCK,
	       1e6 * busy / blocks, 100.0 * busy / (blocks * OM_MAX_BLOCK / rate));
	printf("hash %016llx\n", (unsigned long long)hash);
	free(l);
	free(r);
	om_studio_destroy(st);
	return 0;
}
