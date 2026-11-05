/*
 * midi_in_test.c
 *
 * The MIDI input (studio/midi_in.c): routing, exact frames, the order of
 * messages, held notes, clock and transport, the tempo under jitter, with mock
 * engines that log what reaches them. Exit code 0 when every check passes.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "midi_in.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define RATE 44100.f

// ---- a mock engine: 2 parts, renders any length, logs each event with the
// frame it lands on (it counts the frames it rendered) ----
typedef struct { int type; int part, note, value; uint64_t frame; } Logged;
enum { L_ON, L_OFF, L_CLOCK };
static Logged logged[4096];
static int logCount;
static uint64_t rendered;
static float tempoSeen;

static void record(int type, int part, int note, int value)
{
	if (logCount < 4096)
		logged[logCount++] = (Logged){type, part, note, value, rendered};
}
static void *mockCreate(float rate) { (void)rate; return calloc(1, 1); }
static void mockDestroy(void *self) { free(self); }
static void mockOn(void *self, uint8_t part, uint8_t note, uint8_t vel) { (void)self; record(L_ON, part, note, vel); }
static void mockOff(void *self, uint8_t part, uint8_t note) { (void)self; record(L_OFF, part, note, 0); }
static void mockParam(void *self, uint8_t part, uint16_t param, uint8_t value) { (void)self; (void)part; (void)param; (void)value; }
static uint8_t mockGet(void *self, uint8_t part, uint16_t param) { (void)self; (void)part; (void)param; return 0; }
static void mockTempo(void *self, float bpm) { (void)self; tempoSeen = bpm; }
static void mockClock(void *self, uint8_t event) { (void)self; record(L_CLOCK, 0, event, 0); }
static void mockRender(void *self, float *const *stems, uint32_t frames)
{
	(void)self;
	for (uint32_t i = 0; i < frames; i++)
		stems[0][i] = stems[1][i] = 0.f;
	rendered += frames;
}
static const OmEngineClass mockClass = {
	.name = "mock", .parts = 2, .stems = 1, .block = 0,
	.create = mockCreate, .destroy = mockDestroy, .note_on = mockOn, .note_off = mockOff,
	.set_param = mockParam, .get_param = mockGet, .set_tempo = mockTempo, .clock = mockClock, .render = mockRender,
};

static uint8_t rawBytes[256];
static int rawCount;
static void rawSink(uint8_t b) { if (rawCount < 256) rawBytes[rawCount++] = b; }

static OmStudio *st;
static OmMidiIn *m;

static void setUp(void)
{
	logCount = rawCount = 0;
	rendered = 0;
	tempoSeen = 0.f;
	st = om_studio_create(RATE);
	om_studio_add_engine(st, &mockClass);
	m = om_midi_in_create(st, RATE, rawSink);
}
static void tearDown(void)
{
	om_midi_in_destroy(m);
	om_studio_destroy(st);
}
static void run(int blocks)
{
	float l[OM_MAX_BLOCK], r[OM_MAX_BLOCK];
	for (int b = 0; b < blocks; b++)
	{
		om_midi_in_advance(m, OM_MAX_BLOCK);
		om_studio_render(st, l, r, OM_MAX_BLOCK);
	}
}
static int countOf(int type)
{
	int n = 0;
	for (int i = 0; i < logCount; i++)
		n += logged[i].type == type;
	return n;
}
static int clockCount(int event)
{
	int n = 0;
	for (int i = 0; i < logCount; i++)
		n += logged[i].type == L_CLOCK && logged[i].note == event;
	return n;
}

static void testRouting(void)
{
	setUp();
	om_midi_in_route(m, 0, 0, 1);
	om_midi_in_event(m, 100, 0x90, 60, 90);   // channel 1: part 1
	om_midi_in_event(m, 120, 0x91, 61, 90);   // channel 2: off
	run(4);
	CHECK(logCount == 1 && logged[0].type == L_ON && logged[0].part == 1 && logged[0].note == 60 && logged[0].value == 90,
		"channel 1's note reaches its part, channel 2's nothing (%d events)", logCount);
	CHECK(rawCount == 0, "a routed note does not also reach the firmware (%d bytes)", rawCount);

	om_midi_in_route(m, 2, OM_MIDI_RAW, 0);
	om_midi_in_event(m, 400, 0x92, 48, 100);
	run(4);
	CHECK(rawCount == 3 && rawBytes[0] == 0x92 && rawBytes[1] == 48 && rawBytes[2] == 100, "a raw channel's note reaches the firmware (%d bytes)", rawCount);
	CHECK(logCount == 1, "and no part");

	rawCount = 0;
	om_midi_in_event(m, 600, 0xB3, 5, 64);    // a CC
	om_midi_in_event(m, 600, 0xC3, 7, 0);     // a program change: 2 bytes
	om_midi_in_event(m, 600, 0xF0, 1, 2);     // system exclusive: not passed on
	run(4);
	CHECK(rawCount == 5 && rawBytes[0] == 0xB3 && rawBytes[3] == 0xC3 && rawBytes[4] == 7, "CCs and program changes reach the firmware (%d bytes)", rawCount);
	tearDown();
}

static void testExactFrames(void)
{
	setUp();
	om_midi_in_route(m, 0, 0, 0);
	const uint64_t want[] = {64, 100, 127, 128, 500, 1000};
	for (size_t i = 0; i < sizeof(want) / sizeof(*want); i++)
		om_midi_in_event(m, want[i], 0x90, (uint8_t)(40 + i), 100);
	run(20);
	CHECK(logCount == 6, "every note landed (%d)", logCount);
	for (int i = 0; i < logCount; i++)
		CHECK(logged[i].frame == want[i], "note %d lands on frame %llu, not %llu", i, (unsigned long long)want[i], (unsigned long long)logged[i].frame);
	tearDown();
}

static void testOrderAndLate(void)
{
	setUp();
	om_midi_in_route(m, 0, 0, 0);
	om_midi_in_event(m, 300, 0x90, 60, 100);
	om_midi_in_event(m, 200, 0x80, 60, 0);    // stamped earlier, arrived later: must not end the note before it starts
	run(10);
	CHECK(logCount == 2 && logged[0].type == L_ON && logged[1].type == L_OFF && logged[1].frame == 300, "the order of arrival is kept");

	run(100);
	const uint64_t now = om_studio_frame(st);
	om_midi_in_event(m, now - 441, 0x90, 62, 100);   // 10 ms late
	run(1);
	CHECK(logCount == 3 && logged[2].frame == now, "a late note plays at once (frame %llu, now %llu)", (unsigned long long)logged[2].frame, (unsigned long long)now);
	OmMidiInState s = om_midi_in_state(m);
	CHECK(s.late == 1 && fabsf(s.lateMs - 10.f) < 0.1f, "and counts: %u, %.2f ms", s.late, s.lateMs);

	const int before = logCount;
	om_midi_in_event(m, om_studio_frame(st) + (uint64_t)(RATE * 100.f), 0x90, 63, 100);   // no sane time
	run(1);
	CHECK(logCount == before + 1, "a stamp far in the future plays at once, it doesn't wait");
	tearDown();
}

static void testHeldNotes(void)
{
	setUp();
	om_midi_in_route(m, 0, 0, 0);
	om_midi_in_route(m, 1, 0, 1);
	om_midi_in_event(m, 0, 0x90, 60, 100);
	om_midi_in_event(m, 0, 0x90, 64, 100);
	om_midi_in_event(m, 0, 0x91, 50, 100);
	run(2);
	om_midi_in_event(m, 200, 0x90, 60, 0);    // velocity 0 is a note off
	run(2);
	CHECK(countOf(L_OFF) == 1, "velocity 0 ends the note");
	om_midi_in_event(m, 400, 0x80, 99, 0);    // never started
	run(2);
	CHECK(countOf(L_OFF) == 1, "an off for a note never played does nothing");

	om_midi_in_event(m, 500, 0xB0, 123, 0);   // all notes off, channel 1
	run(2);
	CHECK(countOf(L_OFF) == 2, "All Notes Off ends the channel's notes only (%d offs)", countOf(L_OFF));

	om_midi_in_route(m, 1, 0, 0);             // channel 2 moves: its note ends on the old part
	run(1);
	CHECK(countOf(L_OFF) == 3 && logged[logCount - 1].part == 1 && logged[logCount - 1].note == 50, "a re-route ends the channel's note on the old part");

	om_midi_in_event(m, om_studio_frame(st), 0x90, 70, 100);
	om_midi_in_event(m, om_studio_frame(st), 0xFC, 0, 0);   // Stop
	run(2);
	CHECK(countOf(L_OFF) == 4, "Stop ends every note");
	const OmMidiInState s = om_midi_in_state(m);
	CHECK(s.activity & 1, "activity shows channel 1");
	CHECK(om_midi_in_state(m).activity == 0, "reading it clears it");
	tearDown();
}

static void tickAt(uint64_t frame) { om_midi_in_event(m, frame, 0xF8, 0, 0); }

static void testTransport(void)
{
	setUp();
	const double perTick = RATE * 60.0 / 120.0 / 24.0;
	uint64_t f = 0;
	double t = 0.0;
	// ticks while stopped: tempo only
	for (int i = 0; i < 48; i++, t += perTick)
		tickAt((uint64_t)t);
	run((int)(t / OM_MAX_BLOCK) + 1);
	CHECK(clockCount(OM_CLOCK_TICK) == 0, "ticks while stopped move nothing");
	CHECK(fabsf(om_studio_tempo(st) - 120.f) < 0.11f, "but give the tempo: %.2f", om_studio_tempo(st));
	CHECK(om_midi_in_state(m).locked && !om_midi_in_state(m).running, "locked, not running");

	f = om_studio_frame(st);
	om_midi_in_event(m, f, 0xFA, 0, 0);
	t = (double)f;
	for (int i = 0; i < 24; i++, t += perTick)
		tickAt((uint64_t)t);
	run((int)(perTick * 24 / OM_MAX_BLOCK) + 2);
	CHECK(clockCount(OM_CLOCK_START) == 1 && clockCount(OM_CLOCK_TICK) == 24, "Start, then 24 ticks (%d)", clockCount(OM_CLOCK_TICK));
	CHECK(logged[logCount - 24 - 1].note == OM_CLOCK_START, "Start comes before its ticks");
	CHECK(om_midi_in_state(m).running, "running");

	f = om_studio_frame(st);
	om_midi_in_event(m, f, 0xFA, 0, 0);       // Start while running: a clean restart
	run(1);
	CHECK(clockCount(OM_CLOCK_STOP) == 1 && clockCount(OM_CLOCK_START) == 2, "Start while running stops first");
	om_midi_in_event(m, om_studio_frame(st), 0xFB, 0, 0);
	run(1);
	CHECK(clockCount(OM_CLOCK_CONTINUE) == 0, "Continue while running does nothing");

	om_midi_in_event(m, om_studio_frame(st), 0xFC, 0, 0);
	om_midi_in_event(m, om_studio_frame(st), 0xFC, 0, 0);
	run(1);
	CHECK(clockCount(OM_CLOCK_STOP) == 2, "two Stops are one stop");
	om_midi_in_event(m, om_studio_frame(st), 0xFB, 0, 0);
	run(1);
	CHECK(clockCount(OM_CLOCK_CONTINUE) == 1 && om_midi_in_state(m).running, "Continue resumes");
	tearDown();
}

static void testTempo(void)
{
	setUp();
	om_midi_in_tempo(m, 100.f);
	CHECK(om_studio_tempo(st) == 100.f && tempoSeen == 100.f, "without a clock, the knob sets the tempo");

	// 128 BPM with +-2 ms of jitter: the tempo settles and then stays put
	srand(7);
	const double perTick = RATE * 60.0 / 128.0 / 24.0;
	double t = 0.0;
	int changes = 0;
	float last = om_studio_tempo(st);
	for (int i = 0; i < 24 * 64; i++, t += perTick)
	{
		const double jitter = ((double)rand() / RAND_MAX - 0.5) * 2.0 * 0.002 * RATE;
		tickAt((uint64_t)fmax(0.0, t + jitter));
		while ((double)om_studio_frame(st) + OM_MAX_BLOCK < t)
			run(1);
		if (i > 24 * 8 && om_studio_tempo(st) != last)
			changes++;
		last = om_studio_tempo(st);
	}
	CHECK(fabsf(om_studio_tempo(st) - 128.f) < 0.5f, "128 BPM with jitter reads %.2f", om_studio_tempo(st));
	CHECK(changes <= 2, "and holds still: %d changes in 56 beats", changes);
	printf("128 BPM, +-2 ms of jitter: %.1f BPM, %d changes in 56 beats\n", om_studio_tempo(st), changes);

	om_midi_in_tempo(m, 90.f);
	CHECK(fabsf(om_studio_tempo(st) - 128.f) < 0.5f, "a running clock wins over the knob");

	// a tempo change follows within a few beats
	const double perTick2 = RATE * 60.0 / 140.0 / 24.0;
	for (int i = 0; i < 24 * 8; i++, t += perTick2)
	{
		tickAt((uint64_t)t);
		while ((double)om_studio_frame(st) + OM_MAX_BLOCK < t)
			run(1);
	}
	CHECK(fabsf(om_studio_tempo(st) - 140.f) < 0.5f, "a new tempo is followed: %.2f", om_studio_tempo(st));

	run((int)(RATE * 1.5f / OM_MAX_BLOCK));   // the clock went quiet
	CHECK(!om_midi_in_state(m).locked, "no ticks for a second: not locked");
	om_midi_in_tempo(m, 90.f);
	CHECK(om_studio_tempo(st) == 90.f, "and the knob sets the tempo again");
	tearDown();
}

static void testReset(void)
{
	setUp();
	om_midi_in_route(m, 0, 0, 0);
	om_midi_in_tempo(m, 111.f);
	om_midi_in_event(m, 0, 0xFA, 0, 0);
	om_midi_in_event(m, 0, 0x90, 60, 100);
	const double perTick = RATE * 60.0 / 150.0 / 24.0;
	double t = 0.0;
	for (int i = 0; i < 48; i++, t += perTick)
		tickAt((uint64_t)t);
	run((int)(t / OM_MAX_BLOCK) + 1);
	CHECK(fabsf(om_studio_tempo(st) - 150.f) < 0.5f, "the clock set the tempo");
	om_midi_in_event(m, om_studio_frame(st) + 5000, 0x90, 61, 100);
	om_midi_in_reset(m);
	run(200);
	CHECK(clockCount(OM_CLOCK_STOP) == 1, "a reset stops the clock");
	CHECK(countOf(L_OFF) == 1, "ends the note");
	CHECK(countOf(L_ON) == 1, "and forgets what was queued");
	CHECK(om_studio_tempo(st) == 111.f, "and the knob's tempo is back: %.1f", om_studio_tempo(st));
	const OmMidiInState s = om_midi_in_state(m);
	CHECK(!s.running && !s.locked, "not running, not locked");
	tearDown();
}

static void testBadInput(void)
{
	setUp();
	om_midi_in_route(m, 200, 0, 0);             // no such channel
	om_midi_in_route(m, 0, 9, 9);               // no such engine: its notes go nowhere
	om_midi_in_event(m, 0, 0x90, 60, 100);
	om_midi_in_event(m, 0, 0x80, 60, 0);
	om_midi_in_event(m, 0, 0x12, 60, 100);      // not a status byte
	om_midi_in_event(m, 0, 0x91, 200, 200);     // data bytes over 127
	om_midi_in_tempo(m, 0.f);
	om_midi_in_tempo(m, NAN);
	run(2);
	CHECK(logCount == 0, "nothing reaches the engine (%d)", logCount);
	CHECK(om_studio_tempo(st) == 120.f, "impossible tempos are refused");
	for (int i = 0; i < 3000; i++)              // more than the queue holds
		om_midi_in_event(m, om_studio_frame(st) + 10, 0xB0, 1, 1);
	CHECK(om_midi_in_state(m).dropped == 3000 - 1024, "a full queue drops and counts: %u", om_midi_in_state(m).dropped);
	run(4);
	tearDown();
}

int main(void)
{
	testRouting();
	testExactFrames();
	testOrderAndLate();
	testHeldNotes();
	testTransport();
	testTempo();
	testReset();
	testBadInput();
	if (failures)
	{
		printf("%d midi_in check(s) failed\n", failures);
		return 1;
	}
	printf("midi_in: all checks passed\n");
	return 0;
}
