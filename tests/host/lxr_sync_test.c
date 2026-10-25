/*
 * lxr_sync_test.c
 *
 * The clock hand-over (PLAN.md Phase 2, step 5): the LXR's own sequencer
 * follows the tracker. A kick is programmed on the LXR's steps 1, 5, 9, 13
 * from its front panel, as a user would; then:
 *   - nothing plays while the tracker is stopped (the LXR waits);
 *   - while the tracker loops an empty pattern at 120 BPM, the LXR's kick
 *     lands on every beat, in time with the tracker's rows;
 *   - when the tracker stops, the LXR stops too, and plays on its own clock
 *     again afterwards (its own Start).
 * And its stems: each voice is its own mixer channel.
 * Exit code 0 when every check passes.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "tracker.h"

extern const OmEngineClass om_lxr_engine;
void web_set_button(uint8_t button, uint8_t pressed);

void web_log(const char *msg) { (void)msg; }
void web_sd_written(uint32_t firstSector, uint32_t count) { (void)firstSector; (void)count; }
void web_midi_out(uint8_t byte) { (void)byte; }

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define RATE 44100.f
static OmStudio *st;
static OmTracker *tr;
static float peaks[1 << 16];   // per 64-frame block

// Renders `blocks` blocks; each block's peak goes to peaks[] from `at`.
static void run(int blocks, int at)
{
	float l[OM_MAX_BLOCK], r[OM_MAX_BLOCK];
	for (int b = 0; b < blocks; b++)
	{
		om_tracker_advance(tr, OM_MAX_BLOCK);
		om_studio_render(st, l, r, OM_MAX_BLOCK);
		float p = 0.f;
		for (int i = 0; i < OM_MAX_BLOCK; i++)
			p = fmaxf(p, fmaxf(fabsf(l[i]), fabsf(r[i])));
		if (at >= 0)
			peaks[at + b] = p;
	}
}

static void pressButton(uint8_t id)
{
	web_set_button(id, 1);
	run(8, -1);
	web_set_button(id, 0);
	run(8, -1);
}

// blocks where the level jumps: a hit starts
static int onsets(int from, int to, int *out, int max)
{
	int n = 0;
	for (int b = from + 1; b < to && n < max; b++)
		if (peaks[b] > 0.05f && peaks[b] > 3.f * peaks[b - 1] + 0.01f)
			out[n++] = b;
	return n;
}

int main(void)
{
	st = om_studio_create(RATE);
	if (om_studio_add_engine(st, &om_lxr_engine) != 0) { printf("FAIL: no LXR\n"); return 1; }
	tr = om_tracker_create(st, RATE);
	om_tracker_set_tempo(tr, 120.f);
	om_tracker_set_length(tr, 0, 16);
	run(400, -1);   // boot settles

	// Drum 1 is selected at boot: its steps 1, 5, 9, 13 (buttons in web/app/main.js SEQ_BUTTONS)
	const uint8_t steps[4] = {39, 31, 23, 15};
	for (int i = 0; i < 4; i++)
		pressButton(steps[i]);

	// stopped: the LXR waits
	run(700, 0);
	float idle = 0.f;
	for (int b = 0; b < 700; b++) idle = fmaxf(idle, peaks[b]);
	CHECK(idle < 0.01f, "the LXR plays while the tracker is stopped (peak %g)", idle);

	// the tracker loops an empty pattern: the LXR's kick on every beat
	om_tracker_play_pattern(tr, 0);
	const int blocks = (int)(RATE * 4.f / OM_MAX_BLOCK);   // 4 s = 8 beats
	run(blocks, 0);
	int hits[64];
	const int n = onsets(0, blocks, hits, 64);
	const double beat = RATE * 60.0 / 120.0 / OM_MAX_BLOCK;   // in blocks: 344.5
	CHECK(n >= 7, "the LXR followed the tracker with %d kicks in 8 beats", n);
	int offbeat = 0;
	for (int i = 1; i < n; i++)
	{
		const double d = hits[i] - hits[i - 1];
		if (fabs(d - beat) > 3.0) offbeat++;   // within 3 blocks (4 ms)
	}
	CHECK(n > 1 && offbeat == 0, "%d of %d kicks off the beat (blocks between hits should be %.1f)", offbeat, n - 1, beat);
	if (n > 1)
		printf("kicks every %.1f blocks (beat %.1f), first at block %d\n", (double)(hits[n - 1] - hits[0]) / (n - 1), beat, hits[0]);

	// the tracker stops: so does the LXR
	om_tracker_stop(tr);
	run(400, -1);   // the last kick's tail
	run(700, 0);
	float after = 0.f;
	for (int b = 0; b < 700; b++) after = fmaxf(after, peaks[b]);
	CHECK(after < 0.01f, "the LXR kept playing after the tracker stopped (peak %g)", after);

	// on its own again: its Start button plays it on its own clock
	pressButton(33);   // Start/Stop: starts it
	run(blocks, 0);
	const int alone = onsets(0, blocks, hits, 64);
	CHECK(alone >= 6, "the LXR alone played %d kicks in 4 s", alone);

	// each voice on its own channel: drum 1-3, snare, cymbal, hihat (open and closed), FX bus
	pressButton(33);   // Start/Stop: stops it
	run(800, -1);
	CHECK(om_studio_channels(st) == 7, "the LXR has %d channels, not 7", om_studio_channels(st));
	const uint8_t channelOf[7] = {0, 1, 2, 3, 4, 5, 5};
	for (uint8_t part = 0; part < 7; part++)
	{
		float m[16];
		om_studio_meters(st, m, 16);   // reset
		om_studio_note_on(st, om_studio_frame(st), 0, part, 60, 120);
		run(100, -1);
		om_studio_meters(st, m, 16);
		int others = 0;
		for (int c = 0; c < 7; c++)
			if (c != channelOf[part] && m[c] > 0.001f) { others++; printf("  voice %d: channel %d at %g\n", part + 1, c, m[c]); }
		CHECK(m[channelOf[part]] > 0.01f && others == 0, "voice %d: its channel %g, %d other channels sound", part + 1, m[channelOf[part]], others);
		run(800, -1);
	}

	if (failures)
	{
		printf("%d LXR sync check(s) failed\n", failures);
		return 1;
	}
	printf("all LXR sync checks passed\n");
	return 0;
}
