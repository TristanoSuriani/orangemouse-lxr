/*
 * studio_test.c
 *
 * Unit tests of the studio's router and mixer (studio/), with a mock engine
 * whose output is exactly 1.0 while a note is held, and of Orange Mouse VA's
 * note handling. Exit code 0 when every check passes.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "studio.h"

extern const OmEngineClass om_va_engine;

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---- a mock engine: 1 part, 1 stem, outputs 1.0 while its note is held ----

typedef struct { int held; int notesOn; float tempo; } Mock;

static void *mockCreate(float rate) { (void)rate; return calloc(1, sizeof(Mock)); }
static void mockDestroy(void *self) { free(self); }
static void mockNoteOn(void *self, uint8_t part, uint8_t note, uint8_t vel) { (void)part; (void)note; (void)vel; ((Mock *)self)->held = 1; ((Mock *)self)->notesOn++; }
static void mockNoteOff(void *self, uint8_t part, uint8_t note) { (void)part; (void)note; ((Mock *)self)->held = 0; }
static void mockParam(void *self, uint8_t part, uint16_t param, uint8_t value) { (void)self; (void)part; (void)param; (void)value; }
static void mockTempo(void *self, float bpm) { ((Mock *)self)->tempo = bpm; }
static void mockRender(void *self, float *const *stems, uint32_t frames)
{
	for (uint32_t i = 0; i < frames; i++)
		stems[0][i] = stems[1][i] = ((Mock *)self)->held ? 1.f : 0.f;
}

static OmEngineClass mockClass(uint16_t block)
{
	return (OmEngineClass){.name = "mock", .parts = 1, .stems = 1, .block = block,
		.create = mockCreate, .destroy = mockDestroy, .note_on = mockNoteOn, .note_off = mockNoteOff,
		.set_param = mockParam, .set_tempo = mockTempo, .render = mockRender};
}

// the first frame at or after `from` where the left channel is (or isn't) 1.0
static int firstFrame(const float *x, int n, int from, int on)
{
	for (int i = from; i < n; i++)
		if ((x[i] == 1.f) == on)
			return i;
	return -1;
}

int main(void)
{
	enum { N = 4096 };
	static float l[N], r[N];

	{   // events land on their exact frame in an engine that renders any length
		const OmEngineClass cls = mockClass(0);
		OmStudio *st = om_studio_create(48000.f);
		const int e = om_studio_add_engine(st, &cls);
		CHECK(e == 0, "add_engine returned %d", e);
		om_studio_note_on(st, 1000, 0, 0, 60, 100);
		om_studio_note_off(st, 1500, 0, 0, 60);
		om_studio_render(st, l, r, N);
		CHECK(firstFrame(l, N, 0, 1) == 1000, "note on at frame %d, expected 1000", firstFrame(l, N, 0, 1));
		CHECK(firstFrame(l, N, 1000, 0) == 1500, "note off at frame %d, expected 1500", firstFrame(l, N, 1000, 0));
		CHECK(om_studio_frame(st) == N, "frame counter %llu", (unsigned long long)om_studio_frame(st));
		om_studio_destroy(st);
	}
	{   // a block engine gets its events at the start of their 64-frame block
		const OmEngineClass cls = mockClass(64);
		OmStudio *st = om_studio_create(48000.f);
		om_studio_add_engine(st, &cls);
		om_studio_note_on(st, 1000, 0, 0, 60, 100);
		om_studio_render(st, l, r, N);
		CHECK(firstFrame(l, N, 0, 1) == 960, "block engine note on at frame %d, expected 960", firstFrame(l, N, 0, 1));
		om_studio_destroy(st);
	}
	{   // events scheduled out of order are played in order; late events play at once
		const OmEngineClass cls = mockClass(0);
		OmStudio *st = om_studio_create(48000.f);
		om_studio_add_engine(st, &cls);
		om_studio_note_off(st, 700, 0, 0, 60);
		om_studio_note_on(st, 300, 0, 0, 60, 100);
		om_studio_render(st, l, r, 1024);
		CHECK(firstFrame(l, 1024, 0, 1) == 300 && firstFrame(l, 1024, 300, 0) == 700,
		      "out-of-order events: on at %d, off at %d", firstFrame(l, 1024, 0, 1), firstFrame(l, 1024, 300, 0));
		om_studio_note_on(st, 10, 0, 0, 60, 100);   // already in the past
		om_studio_render(st, l, r, 64);
		CHECK(l[0] == 1.f, "a late event did not play at the start of the next render");
		om_studio_destroy(st);
	}
	{   // the queue refuses events beyond OM_MAX_EVENTS, and unknown engines and parts
		const OmEngineClass cls = mockClass(0);
		OmStudio *st = om_studio_create(48000.f);
		om_studio_add_engine(st, &cls);
		int ok = 0;
		for (int i = 0; i < OM_MAX_EVENTS + 10; i++)
			ok += om_studio_note_on(st, (uint64_t)i, 0, 0, 60, 100) == 0;
		CHECK(ok == OM_MAX_EVENTS, "queue took %d events, expected %d", ok, OM_MAX_EVENTS);
		CHECK(om_studio_note_on(st, 0, 1, 0, 60, 100) == -1, "an unknown engine was accepted");
		CHECK(om_studio_note_on(st, 0, 0, 1, 60, 100) == -1, "an unknown part was accepted");
		om_studio_destroy(st);
	}
	{   // the mixer: level and balance; the tempo reaches the engines
		const OmEngineClass cls = mockClass(0);
		OmStudio *st = om_studio_create(48000.f);
		om_studio_add_engine(st, &cls);
		om_studio_set_stem(st, 0, 0, 0.5f, 1.f);
		om_studio_set_tempo(st, 133.f);
		om_studio_note_on(st, 0, 0, 0, 60, 100);
		om_studio_render(st, l, r, 64);
		CHECK(l[10] == 0.f && r[10] == 0.5f, "balance right at level 0.5: L %g R %g", l[10], r[10]);
		CHECK(om_studio_tempo(st) == 133.f, "tempo %g", om_studio_tempo(st));
		om_studio_destroy(st);
	}
	{   // Orange Mouse VA: a held note sounds until its note off, then releases to silence
		OmStudio *st = om_studio_create(48000.f);
		const int va = om_studio_add_engine(st, &om_va_engine);
		CHECK(va == 0, "VA could not be created");
		CHECK(om_studio_add_engine(st, &om_va_engine) == -1, "a second VA instance was created");
		om_studio_note_on(st, 0, 0, 0, 45, 100);
		om_studio_note_off(st, 48000, 0, 0, 45);   // after 1 s
		float peakHeld = 0.f, peakEnd = 0.f;
		for (int b = 0; b < 48000 * 4 / N; b++)    // 4 s
		{
			om_studio_render(st, l, r, N);
			for (int i = 0; i < N; i++)
			{
				const float a = fabsf(l[i]) > fabsf(r[i]) ? fabsf(l[i]) : fabsf(r[i]);
				const int frame = b * N + i;
				if (frame > 24000 && frame < 48000 && a > peakHeld) peakHeld = a;
				if (frame > 48000 * 3 && a > peakEnd) peakEnd = a;
			}
		}
		CHECK(peakHeld > 0.05f, "VA bass is quiet while held: peak %g", peakHeld);
		CHECK(peakEnd < 1e-4f, "VA bass still sounds 2 s after its note off: peak %g", peakEnd);
		om_studio_destroy(st);
	}

	if (failures)
	{
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all studio checks passed\n");
	return 0;
}
