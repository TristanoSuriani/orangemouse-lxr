/*
 * lxr_render.c
 *
 * The LXR firmware on the host, without a browser: boots it on an empty SD
 * card, plays the demo groove (web_test_demo_block) and renders it offline.
 * Prints the time per block and a hash of the output; optionally writes the
 * output as raw float32 (all four channels, planar per block, exactly what
 * web_render() returns) and as a WAV of both stereo outputs mixed.
 *
 *   lxr_render [--seconds S] [--rate HZ] [--raw out.f32] [--wav out.wav]
 *   lxr_render --dump-kit      the init kit's sound parameters after boot, one per line
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// web_port.h's imports, which the AudioWorklet provides in the browser
void web_log(const char *msg) { fprintf(stderr, "lxr: %s\n", msg); }
void web_sd_written(uint32_t firstSector, uint32_t count) { (void)firstSector; (void)count; }
void web_midi_out(uint8_t byte) { (void)byte; }

uint8_t *web_sd_alloc(uint32_t sectors);
void web_boot(float rate, uint32_t seed);
float *web_render(void);
int web_block_size(void);
void web_test_demo_block(uint32_t block, float rate);
uint16_t web_test_num_sound_params(void);
extern uint8_t activeParameters[];

static void writeWav(const char *path, const int16_t *s, uint32_t frames, uint32_t rate)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return;
	const uint32_t data = frames * 4, riff = 36 + data, fmt = 16, br = rate * 4;
	const uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
	fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
	fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
	fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
	fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(s, 2, frames * 2, f);
	fclose(f);
}

int main(int argc, char **argv)
{
	float seconds = 8.f, rate = 44100.f;
	const char *rawPath = NULL, *wavPath = NULL;
	if (argc == 2 && !strcmp(argv[1], "--dump-kit"))
	{
		web_sd_alloc(32768);
		web_boot(rate, 1);
		for (int p = 0; p < web_test_num_sound_params(); p++)
			printf("%d %d\n", p, activeParameters[p]);
		return 0;
	}
	for (int i = 1; i + 1 < argc; i += 2)
	{
		if (!strcmp(argv[i], "--seconds")) seconds = (float)atof(argv[i + 1]);
		else if (!strcmp(argv[i], "--rate")) rate = (float)atof(argv[i + 1]);
		else if (!strcmp(argv[i], "--raw")) rawPath = argv[i + 1];
		else if (!strcmp(argv[i], "--wav")) wavPath = argv[i + 1];
		else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
	}

	web_sd_alloc(32768);  // 16 MB, empty: the firmware formats it, as on first use in the browser
	web_boot(rate, 1);

	const int n = web_block_size();
	const uint32_t blocks = (uint32_t)(seconds * rate / n);
	FILE *raw = rawPath ? fopen(rawPath, "wb") : NULL;
	int16_t *wav = wavPath ? malloc(sizeof(int16_t) * 2 * n * blocks) : NULL;
	uint64_t hash = 1469598103934665603ull;  // FNV-1a over the float output
	double busy = 0;
	for (uint32_t b = 0; b < blocks; b++)
	{
		web_test_demo_block(b, rate);
		const clock_t t0 = clock();
		const float *out = web_render();
		busy += (double)(clock() - t0) / CLOCKS_PER_SEC;
		const uint8_t *bytes = (const uint8_t *)out;
		for (size_t i = 0; i < sizeof(float) * 4 * (size_t)n; i++)
			hash = (hash ^ bytes[i]) * 1099511628211ull;
		if (raw)
			fwrite(out, sizeof(float), 4 * (size_t)n, raw);
		if (wav)
			for (int i = 0; i < n; i++)
				for (int c = 0; c < 2; c++)
				{
					float v = out[c * n + i] + out[(c + 2) * n + i];  // both outputs, mixed as in the browser
					v = v > 1.f ? 1.f : v < -1.f ? -1.f : v;
					wav[((size_t)b * n + i) * 2 + c] = (int16_t)(v * 32767.f);
				}
	}
	if (raw)
		fclose(raw);
	if (wav)
	{
		writeWav(wavPath, wav, n * blocks, (uint32_t)rate);
		free(wav);
	}
	const double blockSeconds = n / rate;
	printf("blocks %u of %d frames, %.2f us/block, %.2f %% of a core\n", blocks, n,
	       1e6 * busy / blocks, 100.0 * busy / (blocks * blockSeconds));
	printf("hash %016llx\n", (unsigned long long)hash);
	return 0;
}
