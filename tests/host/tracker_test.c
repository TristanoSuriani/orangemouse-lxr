/*
 * tracker_test.c
 *
 * Unit tests of the tracker's sequencer core (studio/tracker.c), with a mock
 * engine that records the exact frame of every note on and off. Exit code 0
 * when every check passes.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tracker.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---- a mock engine: 4 parts, records events with the frame they land on ----

typedef struct { uint64_t frame; uint8_t on, part, note, velocity; } Ev;
static Ev evs[4096];
static int nevs;
static uint64_t rendered;   // frames the mock has rendered: where an event lands

static void *mockCreate(float rate) { (void)rate; static int self; return &self; }
static void mockDestroy(void *self) { (void)self; }
static void mockOn(void *self, uint8_t part, uint8_t note, uint8_t vel) { (void)self; if (nevs < 4096) evs[nevs++] = (Ev){rendered, 1, part, note, vel}; }
static void mockOff(void *self, uint8_t part, uint8_t note) { (void)self; if (nevs < 4096) evs[nevs++] = (Ev){rendered, 0, part, note, 0}; }
static void mockParam(void *self, uint8_t part, uint16_t param, uint8_t value) { (void)self; (void)part; (void)param; (void)value; }
static uint8_t mockGet(void *self, uint8_t part, uint16_t param) { (void)self; (void)part; (void)param; return 0; }
static void mockTempo(void *self, float bpm) { (void)self; (void)bpm; }
static uint64_t clocks[1024][2];   // frame, event
static int nclocks;
static void mockClock(void *self, uint8_t event) { (void)self; if (nclocks < 1024) { clocks[nclocks][0] = rendered; clocks[nclocks][1] = event; nclocks++; } }
static void mockRender(void *self, float *const *stems, uint32_t frames)
{
	(void)self;
	memset(stems[0], 0, frames * sizeof(float));
	memset(stems[1], 0, frames * sizeof(float));
	rendered += frames;
}
static const OmEngineClass mock = {.name = "mock", .parts = 4, .stems = 1, .block = 0,
	.create = mockCreate, .destroy = mockDestroy, .note_on = mockOn, .note_off = mockOff,
	.set_param = mockParam, .get_param = mockGet, .set_tempo = mockTempo, .clock = mockClock, .render = mockRender};

static OmStudio *st;
static OmTracker *tr;

static void fresh(void)
{
	if (tr) om_tracker_destroy(tr);
	if (st) om_studio_destroy(st);
	st = om_studio_create(48000.f);
	om_studio_add_engine(st, &mock);
	tr = om_tracker_create(st, 48000.f);
	for (uint8_t k = 0; k < 4; k++)
		om_tracker_route(tr, k, 0, k);
	nevs = 0;
	nclocks = 0;
	rendered = 0;
}

static void run(uint32_t frames)
{
	static float l[OM_MAX_BLOCK], r[OM_MAX_BLOCK];
	for (uint32_t done = 0; done < frames; done += OM_MAX_BLOCK)
	{
		om_tracker_advance(tr, OM_MAX_BLOCK);
		om_studio_render(st, l, r, OM_MAX_BLOCK);
	}
}

static OmCell notes(uint8_t a, uint8_t b, uint8_t c, uint8_t retrig)
{
	return (OmCell){{a, b, c, OM_NOTE_EMPTY}, 100, retrig};
}

// the n-th event of a kind (on or off) for a part, or NULL
static const Ev *nth(uint8_t on, uint8_t part, int n)
{
	for (int i = 0; i < nevs; i++)
		if (evs[i].on == on && evs[i].part == part && n-- == 0)
			return &evs[i];
	return NULL;
}

int main(void)
{
	// at 120 BPM and 48 kHz a row (a sixteenth) is 6000 frames
	{
		fresh();
		OmCell c = notes(60, OM_NOTE_EMPTY, OM_NOTE_EMPTY, 0);
		om_tracker_set_cell(tr, 0, 0, 0, &c);
		om_tracker_set_cell(tr, 0, 1, 0, &c);
		om_tracker_set_cell(tr, 0, 4, 0, &c);
		om_tracker_play_pattern(tr, 0);
		run(30000);
		const Ev *a = nth(1, 0, 0), *b = nth(1, 0, 1), *d = nth(1, 0, 2);
		CHECK(a && a->frame == 0, "row 0 at frame %llu", a ? (unsigned long long)a->frame : 0);
		CHECK(b && b->frame == 6000, "row 1 at frame %llu, expected 6000", b ? (unsigned long long)b->frame : 0);
		CHECK(d && d->frame == 24000, "row 4 at frame %llu, expected 24000", d ? (unsigned long long)d->frame : 0);
		const Ev *off = nth(0, 0, 0);
		CHECK(off && off->frame == 6000, "the first note ends when the next starts (frame %llu)", off ? (unsigned long long)off->frame : 0);
	}
	// no drift: at 123 BPM row n starts at round(n * 48000 * 60 / 123 / 4)
	{
		fresh();
		om_tracker_set_tempo(tr, 123.f);
		om_tracker_set_length(tr, 0, 128);
		OmCell c = notes(60, OM_NOTE_EMPTY, OM_NOTE_EMPTY, 0);
		for (uint8_t r = 0; r < 128; r++)
			om_tracker_set_cell(tr, 0, r, 0, &c);
		om_tracker_play_pattern(tr, 0);
		run(48000 * 32);   // 256 rows of 5853.66 frames
		int bad = 0;
		for (int n = 0; n < 128 * 2; n++)
		{
			const Ev *e = nth(1, 0, n);
			const uint64_t want = (uint64_t)(n * 48000.0 * 60.0 / 123.0 / 4.0 + 0.5);
			if (!e || e->frame != want) bad++;
		}
		CHECK(bad == 0, "%d of 256 rows off their exact frame at 123 BPM", bad);
	}
	// a chord: all its notes together, all released by the next note; a note off cell
	{
		fresh();
		OmCell chord = notes(60, 64, 67, 0), single = notes(72, OM_NOTE_EMPTY, OM_NOTE_EMPTY, 0);
		OmCell off = {{OM_NOTE_OFF, OM_NOTE_EMPTY, OM_NOTE_EMPTY, OM_NOTE_EMPTY}, 0, 0};
		om_tracker_set_cell(tr, 0, 0, 1, &chord);
		om_tracker_set_cell(tr, 0, 2, 1, &single);
		om_tracker_set_cell(tr, 0, 3, 1, &off);
		om_tracker_play_pattern(tr, 0);
		run(30000);
		int on0 = 0, offAt2 = 0, offAt3 = 0;
		for (int i = 0; i < nevs; i++)
		{
			if (evs[i].on && evs[i].frame == 0) on0++;
			if (!evs[i].on && evs[i].frame == 12000) offAt2++;
			if (!evs[i].on && evs[i].frame == 18000 && evs[i].note == 72) offAt3++;
		}
		CHECK(on0 == 3, "a 3-note chord gave %d note ons", on0);
		CHECK(offAt2 == 3, "the next note released %d of the chord's 3 notes", offAt2);
		CHECK(offAt3 == 1, "the note off cell did not release the note");
	}
	// retrigger: 4 hits spread over the row, each ending the one before
	{
		fresh();
		OmCell c = notes(60, OM_NOTE_EMPTY, OM_NOTE_EMPTY, 4);
		om_tracker_set_cell(tr, 0, 0, 2, &c);
		om_tracker_play_pattern(tr, 0);
		run(12000);
		int ok = 1;
		for (int h = 0; h < 4; h++)
		{
			const Ev *e = nth(1, 2, h);
			if (!e || e->frame != (uint64_t)(h * 1500)) ok = 0;
		}
		CHECK(ok, "retrigger 4: hits not at 0, 1500, 3000, 4500");
		const Ev *off = nth(0, 2, 0);
		CHECK(off && off->frame == 1500, "the first hit ends at the second");
	}
	// each pattern's own tempo; the order list; the song ends (no wrap)
	{
		fresh();
		om_tracker_set_length(tr, 0, 2);
		om_tracker_set_length(tr, 1, 2);
		om_tracker_set_pattern_tempo(tr, 1, 60.f);   // a row is 12000 frames
		OmCell c = notes(60, OM_NOTE_EMPTY, OM_NOTE_EMPTY, 0);
		for (uint8_t p = 0; p < 2; p++)
			for (uint8_t r = 0; r < 2; r++)
				om_tracker_set_cell(tr, p, r, 0, &c);
		om_tracker_set_order(tr, 0, 0);
		om_tracker_set_order(tr, 1, 1);
		om_tracker_set_order(tr, 2, OM_ORDER_END);
		om_tracker_play_song(tr, 0);
		run(48000 * 2);
		const uint64_t want[4] = {0, 6000, 12000, 24000};
		int ok = 1;
		for (int n = 0; n < 4; n++)
		{
			const Ev *e = nth(1, 0, n);
			if (!e || e->frame != want[n]) ok = 0;
		}
		CHECK(ok, "song: rows not at 0, 6000 (120 BPM), 12000, 24000 (60 BPM)");
		CHECK(!nth(1, 0, 4), "the song wrapped instead of ending");
		CHECK(!om_tracker_state(tr).playing, "the tracker still plays after the song's end");
		const Ev *last = nth(0, 0, 3);
		CHECK(last && last->frame == 36000, "the last note ends with the song (frame %llu)", last ? (unsigned long long)last->frame : 0);
	}
	// loop mode repeats the pattern
	{
		fresh();
		om_tracker_set_length(tr, 3, 2);
		OmCell c = notes(60, OM_NOTE_EMPTY, OM_NOTE_EMPTY, 0);
		om_tracker_set_cell(tr, 3, 0, 0, &c);
		om_tracker_play_pattern(tr, 3);
		run(48000);
		const Ev *e = nth(1, 0, 3);
		CHECK(e && e->frame == 36000, "loop: the 4th pass at frame %llu, expected 36000", e ? (unsigned long long)e->frame : 0);
		CHECK(om_tracker_state(tr).playing && om_tracker_state(tr).pattern == 3, "loop stopped");
		om_tracker_stop(tr);
		run(64);
		CHECK(!om_tracker_state(tr).playing, "stop did not stop");
	}
	// mute and solo
	{
		fresh();
		OmCell c = notes(60, OM_NOTE_EMPTY, OM_NOTE_EMPTY, 0);
		for (uint8_t k = 0; k < 3; k++)
			om_tracker_set_cell(tr, 0, 0, k, &c);
		om_tracker_set_mute(tr, 0, 1);
		om_tracker_play_pattern(tr, 0);
		run(1000);
		CHECK(!nth(1, 0, 0) && nth(1, 1, 0) && nth(1, 2, 0), "mute: track 1 played or others did not");
		fresh();
		for (uint8_t k = 0; k < 3; k++)
			om_tracker_set_cell(tr, 0, 0, k, &c);
		om_tracker_set_solo(tr, 2, 1);
		om_tracker_play_pattern(tr, 0);
		run(1000);
		CHECK(!nth(1, 0, 0) && !nth(1, 1, 0) && nth(1, 2, 0), "solo: only track 3 should play");
	}

	// the clock: start, 6 ticks a row, stop at the song's end
	{
		fresh();
		om_tracker_set_length(tr, 0, 2);
		om_tracker_set_order(tr, 0, 0);
		om_tracker_set_order(tr, 1, OM_ORDER_END);
		om_tracker_play_song(tr, 0);
		run(30000);
		int ticks = 0, bad = 0, starts = 0, stops = 0;
		uint64_t stopAt = 0;
		for (int i = 0; i < nclocks; i++)
		{
			if (clocks[i][1] == OM_CLOCK_START) starts++;
			if (clocks[i][1] == OM_CLOCK_STOP) { stops++; stopAt = clocks[i][0]; }
			if (clocks[i][1] == OM_CLOCK_TICK) { if (clocks[i][0] != (uint64_t)ticks * 1000) bad++; ticks++; }
		}
		CHECK(starts == 1 && clocks[0][1] == OM_CLOCK_START && clocks[0][0] == 0, "the clock does not start at frame 0");
		CHECK(ticks == 12 && bad == 0, "%d ticks (12 expected, every 1000 frames), %d off", ticks, bad);
		CHECK(stops == 1 && stopAt == 12000, "the clock stops %d times, at frame %llu (12000 expected)", stops, (unsigned long long)stopAt);
	}

	{   // swing (PLAN.md 10.2): at 66 %, the second 16th of each pair comes late
		fresh();
		om_tracker_set_tempo(tr, 120.f);   // a row: 6000 frames at 48 kHz
		om_tracker_set_pattern_swing(tr, 0, 66);
		OmCell c = notes(60, OM_NOTE_EMPTY, OM_NOTE_EMPTY, 0);
		for (uint8_t r = 0; r < 4; r++) om_tracker_set_cell(tr, 0, r, 0, &c);
		om_tracker_play_song(tr, 0);
		run(30000);
		const Ev *a = nth(1, 0, 0), *b = nth(1, 0, 1), *d = nth(1, 0, 2), *e = nth(1, 0, 3);
		// the mock engine sees events at the start of the block they fall in
		CHECK(a && b && d && e && a->frame == 0 && b->frame / 64 == 7920 / 64 && d->frame / 64 == 12000 / 64 && e->frame / 64 == 19920 / 64,
		      "swung rows start at %llu %llu %llu %llu (0, 7920, 12000, 19920)", a ? (unsigned long long)a->frame : 0,
		      b ? (unsigned long long)b->frame : 0, d ? (unsigned long long)d->frame : 0, e ? (unsigned long long)e->frame : 0);
	}
	{   // note length and glide (PLAN.md 10.3)
		fresh();
		om_tracker_set_tempo(tr, 120.f);   // a row: 6000 frames
		OmCell a = notes(60, OM_NOTE_EMPTY, OM_NOTE_EMPTY, 0), b = notes(64, OM_NOTE_EMPTY, OM_NOTE_EMPTY, 0);
		a.length = 2;   // half a row
		om_tracker_set_cell(tr, 0, 0, 0, &a);
		b.glide = 1;
		OmCell c0 = notes(48, OM_NOTE_EMPTY, OM_NOTE_EMPTY, 0);
		om_tracker_set_cell(tr, 0, 4, 1, &c0);
		om_tracker_set_cell(tr, 0, 5, 1, &b);
		om_tracker_play_song(tr, 0);
		run(40000);
		const Ev *off = nth(0, 0, 0);
		CHECK(off && off->note == 60 && off->frame / 64 == 3000 / 64, "a half-row note ends at frame %llu (3000)", off ? (unsigned long long)off->frame : 0);
		// the glide: 64 starts before 48 ends, both at row 5
		int on64 = -1, off48 = -1;
		for (int i = 0; i < nevs; i++)
		{
			if (evs[i].part == 1 && evs[i].on && evs[i].note == 64 && on64 < 0) on64 = i;
			if (evs[i].part == 1 && !evs[i].on && evs[i].note == 48 && off48 < 0) off48 = i;
		}
		CHECK(on64 >= 0 && off48 > on64 && evs[on64].frame / 64 == 30000 / 64 && evs[off48].frame == evs[on64].frame,
		      "a glide plays its note first and then ends the one before (events %d, %d)", on64, off48);
	}
	{   // conditions (PLAN.md 10.4): 1:2 plays on passes 1, 3, 5...; 50 % about half the time, the same each play
		fresh();
		om_tracker_set_tempo(tr, 120.f);
		om_tracker_set_length(tr, 0, 1);   // one row: a pass every 6000 frames
		OmCell a = notes(60, OM_NOTE_EMPTY, OM_NOTE_EMPTY, 0), b = notes(62, OM_NOTE_EMPTY, OM_NOTE_EMPTY, 0);
		a.cond = OM_COND_AB;       // 1:2
		b.cond = 5;                // 50 %
		om_tracker_set_cell(tr, 0, 0, 0, &a);
		om_tracker_set_cell(tr, 0, 0, 1, &b);
		int counts[2][2];
		for (int play = 0; play < 2; play++)
		{
			nevs = 0;
			om_tracker_play_pattern(tr, 0);
			run(6000 * 40);
			om_tracker_stop(tr);
			int on = 0, half = 0, alternating = 1;
			uint64_t last = 0;
			for (int i = 0; i < nevs; i++)
			{
				if (!evs[i].on) continue;
				if (evs[i].part == 0)   // every other pass: 12000 frames apart (to a block)
				{
					if (on && (evs[i].frame - last + 3000) / 6000 != 2) alternating = 0;
					last = evs[i].frame;
					on++;
				}
				if (evs[i].part == 1) half++;
			}
			CHECK(on == 20 && alternating, "1:2 plays 20 times of 40, on every other pass (%d, %s)", on, alternating ? "alternating" : "not alternating");
			CHECK(half > 10 && half < 30, "50 %% plays %d times of 40", half);
			counts[play][0] = on, counts[play][1] = half;
		}
		CHECK(counts[0][1] == counts[1][1], "the chances are the same each play (%d, %d)", counts[0][1], counts[1][1]);
	}
	{   // the effect column (PLAN.md 10.1): a cutoff slide from 20 at row 0 to 100 at row 4, then put back
		fresh();
		om_tracker_set_tempo(tr, 120.f);   // a row: 6000 frames at 48 kHz
		OmCell a = {{OM_NOTE_EMPTY, OM_NOTE_EMPTY, OM_NOTE_EMPTY, OM_NOTE_EMPTY}, 0, 0, OM_FX_CUTOFF | OM_FX_SLIDE, 20};
		OmCell b = a;
		b.fx = OM_FX_CUTOFF;
		b.fxValue = 100;
		om_tracker_set_cell(tr, 0, 0, 0, &a);
		om_tracker_set_cell(tr, 0, 4, 0, &b);
		const uint8_t before = om_studio_mix_value(st, 0, OM_MIX_CUTOFF);
		om_tracker_play_song(tr, 0);
		run(640);
		const uint8_t at0 = om_studio_mix_value(st, 0, OM_MIX_CUTOFF);
		run(12000 - 640);
		const uint8_t mid = om_studio_mix_value(st, 0, OM_MIX_CUTOFF);
		run(12000 + 640);
		const uint8_t at4 = om_studio_mix_value(st, 0, OM_MIX_CUTOFF);
		om_tracker_stop(tr);
		const uint8_t after = om_studio_mix_value(st, 0, OM_MIX_CUTOFF);
		CHECK(at0 >= 20 && at0 <= 22 && mid >= 58 && mid <= 62 && at4 == 100, "the slide: %d at row 0, %d at row 2, %d at row 4", at0, mid, at4);
		CHECK(before == 127 && after == 127, "the stop puts the song's cutoff back: %d before, %d after", before, after);
		CHECK(nevs == 0, "a cell with only an effect plays no note (%d events)", nevs);
	}

	om_tracker_destroy(tr);
	om_studio_destroy(st);

	if (failures)
	{
		printf("%d tracker check(s) failed\n", failures);
		return 1;
	}
	printf("all tracker checks passed\n");
	return 0;
}
