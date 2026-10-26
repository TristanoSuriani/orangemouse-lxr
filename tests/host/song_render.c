/*
 * song_render.c
 *
 * Renders a song (PLAN.md Phase 6: the genre demos are the regression
 * songs) through the whole studio: the LXR, Orange Mouse VA, Dexed and
 * Plaits, the tracker, the mixer and its effects. The song comes as the
 * message stream the page sends (tools/song_stream.mjs), so this plays what
 * the browser plays. Prints a hash of the samples, health (NaN, clipping, DC)
 * and loudness, and the CPU per block; and fails if a mixer channel the song
 * plays notes on stays silent (a route, a preset or a level gone wrong).
 *
 *   song_render <stream.txt> [--seconds N] [--wav out.wav]
 *
 * Without --seconds it plays the whole song and 2 s of tail, and checks that
 * every channel the song plays notes on sounds.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tracker.h"

extern const OmEngineClass om_lxr_engine, om_va_engine, om_dexed_engine, om_plaits_engine;

void web_log(const char *msg) { (void)msg; }
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

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: song_render <stream.txt> [--seconds N] [--wav out.wav]\n"); return 2; }
	const float rate = 44100.f;
	float seconds = 0.f;
	const char *wavPath = NULL;
	for (int i = 2; i + 1 < argc; i += 2)
	{
		if (!strcmp(argv[i], "--seconds")) seconds = (float)atof(argv[i + 1]);
		else if (!strcmp(argv[i], "--wav")) wavPath = argv[i + 1];
	}
	FILE *in = fopen(argv[1], "r");
	if (!in) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }

	OmStudio *st = om_studio_create(rate);
	const OmEngineClass *engines[4] = {&om_lxr_engine, &om_va_engine, &om_dexed_engine, &om_plaits_engine};
	for (int e = 0; e < 4; e++)
		if (om_studio_add_engine(st, engines[e]) != e) { fprintf(stderr, "cannot create engine %d\n", e); return 1; }
	OmTracker *tr = om_tracker_create(st, rate);

	char line[256], op[16];
	float songSeconds = 0.f;
	int bad = 0;
	int routeEngine[OM_TRACKS], routePart[OM_TRACKS], played[OM_TRACKS] = {0};
	for (int k = 0; k < OM_TRACKS; k++) routeEngine[k] = -1, routePart[k] = 0;
	while (fgets(line, sizeof line, in))
	{
		int a[10] = {0};
		float f = 0.f;
		if (line[0] == 'T' && sscanf(line, "T %15s", op) == 1)
		{
			const char *args = line + 2 + strlen(op);
			if (!strcmp(op, "clear")) om_tracker_clear(tr);
			else if (!strcmp(op, "tempo") && sscanf(args, "%f", &f) == 1) om_tracker_set_tempo(tr, f);
			else if (!strcmp(op, "pattern") && sscanf(args, "%d %d %f", &a[0], &a[1], &f) == 3)
			{
				om_tracker_set_length(tr, (uint8_t)a[0], (uint8_t)a[1]);
				om_tracker_set_pattern_tempo(tr, (uint8_t)a[0], f);
			}
			else if (!strcmp(op, "order") && sscanf(args, "%d %d", &a[0], &a[1]) == 2) om_tracker_set_order(tr, (uint16_t)a[0], (uint8_t)a[1]);
			else if (!strcmp(op, "cell") && sscanf(args, "%d %d %d %d %d %d %d %d %d", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7], &a[8]) == 9)
			{
				const OmCell c = {{(uint8_t)a[3], (uint8_t)a[4], (uint8_t)a[5], (uint8_t)a[6]}, (uint8_t)a[7], (uint8_t)a[8]};
				om_tracker_set_cell(tr, (uint8_t)a[0], (uint8_t)a[1], (uint8_t)a[2], &c);
				if (a[2] < OM_TRACKS && a[3] < 128) played[a[2]] = 1;
			}
			else if (!strcmp(op, "route") && sscanf(args, "%d %d %d", &a[0], &a[1], &a[2]) == 3)
			{
				om_tracker_route(tr, (uint8_t)a[0], (uint8_t)a[1], (uint8_t)a[2]);
				if (a[0] < OM_TRACKS) routeEngine[a[0]] = a[1], routePart[a[0]] = a[2];
			}
			else if (!strcmp(op, "mute") && sscanf(args, "%d %d", &a[0], &a[1]) == 2) om_tracker_set_mute(tr, (uint8_t)a[0], (uint8_t)a[1]);
			else if (!strcmp(op, "solo") && sscanf(args, "%d %d", &a[0], &a[1]) == 2) om_tracker_set_solo(tr, (uint8_t)a[0], (uint8_t)a[1]);
			else bad++;
		}
		else if (sscanf(line, "P %d %d %d %d", &a[0], &a[1], &a[2], &a[3]) == 4)
			om_studio_set_param(st, 0, (uint8_t)a[0], (uint8_t)a[1], (uint16_t)a[2], (uint8_t)a[3]);
		else if (sscanf(line, "M %d %d %d", &a[0], &a[1], &a[2]) == 3)
			om_studio_mix(st, (uint8_t)a[0], (uint8_t)a[1], (uint8_t)a[2]);
		else if (sscanf(line, "F %d %d %d", &a[0], &a[1], &a[2]) == 3)
			om_studio_fx(st, (uint8_t)a[0], (uint16_t)a[1], (uint8_t)a[2]);
		else if (sscanf(line, "S %f", &f) == 1)
			songSeconds = f;
		else if (line[0] != '\n')
			bad++;
	}
	fclose(in);
	if (bad) { fprintf(stderr, "%d lines not understood\n", bad); return 1; }
	const int whole = seconds <= 0.f;
	if (whole) seconds = songSeconds + 2.f;

	// the engines take their parameters, the LXR boots: then the song plays
	float l[OM_MAX_BLOCK], r[OM_MAX_BLOCK];
	for (int b = 0; b < 400; b++) { om_tracker_advance(tr, OM_MAX_BLOCK); om_studio_render(st, l, r, OM_MAX_BLOCK); }
	om_tracker_play_song(tr, 0);

	const uint32_t blocks = (uint32_t)(seconds * rate / OM_MAX_BLOCK);
	float *wl = wavPath ? malloc(sizeof(float) * OM_MAX_BLOCK * blocks) : NULL, *wr = wavPath ? malloc(sizeof(float) * OM_MAX_BLOCK * blocks) : NULL;
	float chPeak[OM_MAX_CHANNELS + 5] = {0}, meters[OM_MAX_CHANNELS + 5];
	const int channels = om_studio_channels(st);
	om_studio_meters(st, meters, OM_MAX_CHANNELS + 5);   // from here on
	uint64_t hash = 1469598103934665603ull;
	double busy = 0, energy = 0, dc = 0;
	float peak = 0.f;
	long nonFinite = 0, limited = 0;
	for (uint32_t b = 0; b < blocks; b++)
	{
		const clock_t t0 = clock();
		om_tracker_advance(tr, OM_MAX_BLOCK);
		om_studio_render(st, l, r, OM_MAX_BLOCK);
		busy += (double)(clock() - t0) / CLOCKS_PER_SEC;
		for (int i = 0; i < OM_MAX_BLOCK; i++)
		{
			const float x[2] = {l[i], r[i]};
			for (int c = 0; c < 2; c++)
			{
				if (!isfinite(x[c])) { nonFinite++; continue; }
				peak = fmaxf(peak, fabsf(x[c]));
				limited += fabsf(x[c]) >= 0.976f;
				energy += (double)x[c] * x[c];
				dc += x[c];
			}
		}
		const uint8_t *bytes = (const uint8_t *)l;
		for (size_t i = 0; i < sizeof l; i++) { hash ^= bytes[i]; hash *= 1099511628211ull; }
		bytes = (const uint8_t *)r;
		for (size_t i = 0; i < sizeof r; i++) { hash ^= bytes[i]; hash *= 1099511628211ull; }
		if (wl) { memcpy(wl + b * OM_MAX_BLOCK, l, sizeof l); memcpy(wr + b * OM_MAX_BLOCK, r, sizeof r); }
		om_studio_meters(st, meters, OM_MAX_CHANNELS + 5);
		for (int c = 0; c < channels; c++) chPeak[c] = fmaxf(chPeak[c], meters[c]);
	}
	// every channel the song plays notes on must sound (the LXR's two hihats share a stem)
	int silent = 0;
	for (int k = 0; k < OM_TRACKS; k++)
	{
		if (!whole || !played[k] || routeEngine[k] < 0) continue;
		const int stem = routeEngine[k] == 0 && routePart[k] == 6 ? 5 : routePart[k];
		const int c = om_studio_channel(st, (uint8_t)routeEngine[k], (uint8_t)stem);
		if (c >= 0 && chPeak[c] < 0.001f)
		{
			printf("silent: track %d (engine %d, part %d) plays notes, its channel peaks at %g\n", k + 1, routeEngine[k], routePart[k], chPeak[c]);
			silent++;
		}
	}
	const double n = 2.0 * blocks * OM_MAX_BLOCK;
	const double rms = sqrt(energy / n);
	printf("%.1f s, %u blocks, %.2f us/block, %.2f %% of a core\n", seconds, blocks, 1e6 * busy / blocks, 100.0 * busy / seconds);
	printf("health: %ld not finite, peak %.3f, %.2f %% of samples at the limiter, DC %.5f, RMS %.1f dBFS\n",
	       nonFinite, peak, 100.0 * limited / n, dc / n, 20.0 * log10(rms + 1e-12));
	printf("hash %016llx\n", (unsigned long long)hash);
	if (wl) { writeWav(wavPath, wl, wr, blocks * OM_MAX_BLOCK, (uint32_t)rate); free(wl); free(wr); }
	om_tracker_destroy(tr);
	om_studio_destroy(st);
	return nonFinite || silent ? 1 : 0;
}
