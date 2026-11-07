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
 * Then the same from an external MIDI clock (studio/midi_in.c): Start and the
 * ticks play it, Stop stops it, Continue picks up where it stopped.
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
#include "midi_in.h"

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

// `count` ticks of MIDI clock from now, each block's applied before it renders
// (peaks from 0); returns the blocks rendered
static int clockTicks(OmMidiIn *midi, int count, double perTick)
{
	const uint64_t from = om_studio_frame(st);
	double t = (double)from;
	int rendered = 0;
	for (int k = 0; k < count; k++, t += perTick)
	{
		om_midi_in_event(midi, (uint64_t)t, 0xF8, 0, 0);
		const int want = (int)((t + perTick - (double)from) / OM_MAX_BLOCK);
		om_midi_in_advance(midi, (uint32_t)(want - rendered) * OM_MAX_BLOCK);
		run(want - rendered, rendered);
		rendered = want;
	}
	return rendered;
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

	pressButton(33);   // Start/Stop: stops it
	run(800, -1);

	// an external MIDI clock at 120 BPM drives it: ticks stamped ahead, each block's applied before it renders
	{
		OmMidiIn *midi = om_midi_in_create(st, RATE, NULL);
		const double perTick = RATE * 60.0 / 120.0 / 24.0;
		int rendered;
		om_midi_in_event(midi, om_studio_frame(st), 0xFA, 0, 0);   // Start
		rendered = clockTicks(midi, 36, perTick);                                 // a beat and a half: kicks on 0 and 24
		int ext = onsets(0, rendered, hits, 64);
		const int firstHit = ext ? hits[0] : 0;   // a kick shows this many blocks after its tick
		CHECK(ext == 2, "Start and 36 ticks of MIDI clock: %d kicks, not 2", ext);
		if (ext == 2)
			CHECK(fabs((hits[1] - hits[0]) - beat) <= 3.0, "the kicks are a beat apart: %d blocks", hits[1] - hits[0]);
		om_midi_in_event(midi, om_studio_frame(st), 0xFC, 0, 0);   // Stop
		om_midi_in_advance(midi, OM_MAX_BLOCK);
		run(400, -1);
		run(344, 0);
		float stoppedPeak = 0.f;
		for (int b = 0; b < 344; b++) stoppedPeak = fmaxf(stoppedPeak, peaks[b]);
		CHECK(stoppedPeak < 0.01f, "the LXR stopped with Stop (peak %g)", stoppedPeak);

		om_midi_in_event(midi, om_studio_frame(st), 0xFB, 0, 0);   // Continue: 12 ticks to the next kick
		rendered = clockTicks(midi, 24, perTick);
		ext = onsets(0, rendered, hits, 64);
		CHECK(ext == 1, "Continue and 24 ticks: %d kicks, not 1", ext);
		if (ext >= 1)
		{
			const int expected = firstHit + (int)(12 * perTick / OM_MAX_BLOCK);
			CHECK(abs(hits[0] - expected) <= 3, "Continue picks up mid-bar: the kick is at block %d, not at %d", hits[0], expected);
			printf("from MIDI clock: kicks a beat apart; after Continue the next is at block %d (a restart would be %d)\n", hits[0], firstHit);
		}

		// song position pointer (in 16ths; kicks on 0, 4, 8, 12): where Continue
		// starts, past the pattern's end too, and while running where the next tick goes
		struct { uint16_t at; int ticks, kickTick; const char *what; } spp[] = {
			{5, 24, 18, "stopped at 16th 5: the kick on 8 comes 3 steps in"},
			{20, 12, 0, "stopped at 16th 20 (bar 2, 16th 4): a kick at once"},
		};
		for (int k = 0; k < 2; k++)
		{
			om_midi_in_event(midi, om_studio_frame(st), 0xFC, 0, 0);   // Stop
			om_midi_in_advance(midi, OM_MAX_BLOCK);
			run(400, -1);
			om_midi_in_event(midi, om_studio_frame(st), 0xF2, spp[k].at & 0x7f, spp[k].at >> 7);
			om_midi_in_event(midi, om_studio_frame(st), 0xFB, 0, 0);   // Continue
			rendered = clockTicks(midi, spp[k].ticks, perTick);
			ext = onsets(0, rendered, hits, 64);
			const int expected = firstHit + (int)(spp[k].kickTick * perTick / OM_MAX_BLOCK);
			CHECK(ext == 1 && abs(hits[0] - expected) <= 3, "%s: %d kicks, the first at block %d, not %d", spp[k].what, ext, ext ? hits[0] : -1, expected);
		}
		// running, now at 16th 6: a pointer to 7 puts the kick on 8 a step in, not two
		om_midi_in_event(midi, om_studio_frame(st), 0xF2, 7, 0);
		rendered = clockTicks(midi, 12, perTick);
		ext = onsets(0, rendered, hits, 64);
		CHECK(ext == 1 && abs(hits[0] - (firstHit + (int)(6 * perTick / OM_MAX_BLOCK))) <= 3,
		      "running, a pointer to 16th 7: %d kicks, the first at block %d", ext, ext ? hits[0] : -1);

		om_midi_in_event(midi, om_studio_frame(st), 0xFC, 0, 0);
		om_midi_in_advance(midi, OM_MAX_BLOCK);
		om_midi_in_destroy(midi);
		run(800, -1);
		pressButton(33);   // on its own clock again
		run(blocks, 0);
		CHECK(onsets(0, blocks, hits, 64) >= 6, "after the MIDI clock the LXR plays alone again");
		pressButton(33);
		run(800, -1);
	}

	// each voice on its own channel: drum 1-3, snare, cymbal, hihat (open and closed), FX bus
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
		run(2000, -1);   // the cymbal's tail lasts: 800 blocks once left -60 dB of it in the next voice's meters
	}

	if (failures)
	{
		printf("%d LXR sync check(s) failed\n", failures);
		return 1;
	}
	printf("all LXR sync checks passed\n");
	return 0;
}
