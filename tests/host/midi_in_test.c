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

#include "om_registry_va.h"
#include "om_registry_dexed.h"
#include "om_registry_plaits.h"
#include "om_registry_rings.h"
extern const OmEngineClass om_va_engine, om_dexed_engine, om_plaits_engine, om_rings_engine;

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define RATE 44100.f
#define RIDE_STEP 1.5   // how much bigger a sample step may get under a ridden CC

// ---- a mock engine: 2 parts, renders any length, logs each event with the
// frame it lands on (it counts the frames it rendered) ----
typedef struct { int type; int part, note, value; uint64_t frame; } Logged;
enum { L_ON, L_OFF, L_CLOCK, L_PARAM, L_BEND, L_PROGRAM };
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
static uint8_t mockValues[2][4];
static uint64_t mappedRendered;   // the mapped engine's frames (the first engine's are `rendered`)
static void mockParam(void *self, uint8_t part, uint16_t param, uint8_t value)
{
	(void)self;
	record(L_PARAM, part, param, value);
	logged[logCount - 1].frame = mappedRendered;
	mockValues[part][param] = value > 40 && param == 3 ? 40 : value;
}
static uint8_t mockGet(void *self, uint8_t part, uint16_t param) { (void)self; return mockValues[part][param]; }
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

// the same engine with a CC map, as VA, Dexed, Plaits and Rings have: a full
// range on 74, three values on 12, a range from 1 on 13, and on 7 one the engine
// holds lower than asked (as Dexed holds its banks)
static const OmParamInfo mappedParams[] = {
	{"FULL", "FUL", "Full", 0, 127, 0, 0, 74},
	{"FEW", "FEW", "Few", 0, 2, 0, 0, 12},
	{"ONE", "ONE", "From one", 1, 127, 1, 0, 13},
	{"HELD", "HLD", "Held", 0, 127, 0, 0, 7},
};
static OmEngineClass mappedClass;
static float bendSeen[2];
static void mappedBend(void *self, uint8_t part, float semitones) { (void)self; record(L_BEND, part, 0, 0); bendSeen[part] = semitones; }
static void mappedProgram(void *self, uint8_t part, uint8_t number) { (void)self; record(L_PROGRAM, part, number, 0); mockValues[part][0] = number; }
static void mappedRender(void *self, float *const *stems, uint32_t frames)
{
	(void)self;
	mappedRendered += frames;
	for (uint32_t i = 0; i < frames; i++)
		stems[0][i] = stems[1][i] = 0.f;
}

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
	mappedClass = mockClass;
	mappedClass.params = mappedParams;
	mappedClass.numParams = 4;
	mappedClass.clock = NULL;
	mappedClass.render = mappedRender;
	mappedClass.pitch_bend = mappedBend;
	mappedClass.program = mappedProgram;
	bendSeen[0] = bendSeen[1] = 0.f;
	om_studio_add_engine(st, &mappedClass);
	memset(mockValues, 0, sizeof(mockValues));
	mappedRendered = 0;
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

static int paramCount(void) { return countOf(L_PARAM); }

static void testCcs(void)
{
	setUp();
	om_midi_in_route(m, 2, 1, 1);   // channel 3: the mapped engine's second part
	om_midi_in_route(m, 3, 0, 0);   // channel 4: an engine without a map, as the LXR

	// on its frame, on its part, and it stops here
	om_midi_in_event(m, 100, 0xB2, 74, 90);
	run(4);
	CHECK(paramCount() == 1 && logged[0].part == 1 && logged[0].note == 0 && logged[0].value == 90 && logged[0].frame == 100,
		"CC 74 moves parameter 0 of part 1 on frame 100 (%d events, frame %llu)", paramCount(), (unsigned long long)logged[0].frame);
	CHECK(rawCount == 0, "a mapped CC does not reach the raw sink (%d bytes)", rawCount);

	// the panels hear of it, once, with the value the engine has
	OmMidiInChange c;
	CHECK(om_midi_in_change(m, &c) == 1 && c.engine == 1 && c.part == 1 && c.param == 0 && c.value == 90, "the change is reported");
	CHECK(om_midi_in_change(m, &c) == 0, "and only once");
	om_midi_in_event(m, om_studio_frame(st), 0xB2, 7, 100);
	run(1);
	CHECK(om_midi_in_change(m, &c) == 1 && c.param == 3 && c.value == 40, "what the engine held it to is reported, not what was asked: %d", c.value);

	// the whole range, whatever the parameter's range is
	static const struct { uint8_t cc, value, want; int param; } scale[] = {
		{12, 0, 0, 1}, {12, 42, 0, 1}, {12, 43, 1, 1}, {12, 85, 1, 1}, {12, 86, 2, 1}, {12, 127, 2, 1},
		{13, 0, 1, 2}, {13, 127, 127, 2},
	};
	for (size_t i = 0; i < sizeof(scale) / sizeof(scale[0]); i++)
	{
		mockValues[1][scale[i].param] = 255;
		om_midi_in_event(m, om_studio_frame(st), 0xB2, scale[i].cc, scale[i].value);
		run(1);
		CHECK(mockValues[1][scale[i].param] == scale[i].want, "CC %d = %d gives %d, not %d", scale[i].cc, scale[i].value, mockValues[1][scale[i].param], scale[i].want);
		while (om_midi_in_change(m, &c)) {}
	}

	// a value it already has does nothing
	mockValues[1][1] = 0;
	logCount = 0;
	for (uint8_t v = 0; v < 40; v++)
		om_midi_in_event(m, om_studio_frame(st), 0xB2, 12, v);
	run(1);
	CHECK(paramCount() == 0, "CC values in the parameter's first slice send nothing (%d)", paramCount());
	CHECK(om_midi_in_change(m, &c) == 0, "and nothing is reported");

	// there and back within one block ends back there, in that order
	logCount = 0;
	mockValues[1][0] = 10;
	const uint64_t f = om_studio_frame(st);
	om_midi_in_event(m, f + 1, 0xB2, 74, 20);
	om_midi_in_event(m, f + 2, 0xB2, 74, 10);
	run(1);
	CHECK(paramCount() == 2 && mockValues[1][0] == 10, "CC there and back in a block ends where it began: %d, %d events", mockValues[1][0], paramCount());
	while (om_midi_in_change(m, &c)) {}

	// an unmapped CC on a mapped channel goes nowhere
	logCount = rawCount = 0;
	om_midi_in_event(m, om_studio_frame(st), 0xB2, 50, 64);
	run(1);
	CHECK(paramCount() == 0 && rawCount == 0, "an unmapped CC stops here (%d params, %d raw bytes)", paramCount(), rawCount);

	// All Notes Off still ends the notes on a mapped channel, and reaches the firmware as before
	om_midi_in_event(m, om_studio_frame(st), 0x92, 60, 100);
	om_midi_in_event(m, om_studio_frame(st), 0xB2, 123, 0);
	run(1);
	CHECK(countOf(L_OFF) == 1, "CC 123 on a mapped channel ends its note");
	CHECK(rawCount == 3, "and goes to the raw sink (%d bytes)", rawCount);

	// a channel of an engine without a map, a channel set to off or to the
	// firmware: their CCs are the firmware's, as ever
	static const uint8_t asRaw[] = {3, 9};
	om_midi_in_route(m, 9, OM_MIDI_RAW, 0);
	for (size_t i = 0; i < sizeof(asRaw); i++)
	{
		logCount = rawCount = 0;
		om_midi_in_event(m, om_studio_frame(st), (uint8_t)(0xB0 | asRaw[i]), 74, 64);
		run(1);
		CHECK(rawCount == 3 && paramCount() == 0, "channel %d's CC goes to the raw sink (%d bytes)", asRaw[i] + 1, rawCount);
	}
	rawCount = 0;
	om_midi_in_event(m, om_studio_frame(st), 0xB9, 74, 64);
	om_midi_in_route(m, 2, 1, 1);
	om_midi_in_route(m, 2, 0, 0);   // re-routed away from the map
	om_midi_in_event(m, om_studio_frame(st), 0xB2, 74, 64);
	run(1);
	CHECK(rawCount == 6, "a channel routed off the mapped engine passes CCs on again (%d bytes)", rawCount);

	// every part and every parameter fit the table, however many arrive: all
	// are reported, with no repeats
	while (om_midi_in_change(m, &c)) {}
	om_midi_in_route(m, 2, 1, 1);
	for (int round = 0; round < 3000; round++)
		om_midi_in_event(m, om_studio_frame(st), 0xB2, 74, (uint8_t)(round & 0x7f));
	run(2);
	int reports = 0;
	while (om_midi_in_change(m, &c))
		reports++;
	CHECK(reports == 1, "a flood of one CC is one report (%d)", reports);
	tearDown();
}

// The largest step between samples on a note, while a CC rides a parameter (ride
// > 0) or holds it; the real engines. A CC every 8 blocks (86 a second), swinging
// 4 times a second: fast enough that a volume that jumps with each CC clicks.
static double stepUnderCc(const OmEngineClass *cls, uint8_t cc, int ride, double *rms)
{
	OmStudio *studio = om_studio_create(RATE);
	const int e = om_studio_add_engine(studio, cls);
	OmMidiIn *in = om_midi_in_create(studio, RATE, NULL);
	om_midi_in_route(in, 0, (uint8_t)e, 0);
	om_midi_in_event(in, 0, 0xB0, cc, 127);   // no louder than the ride gets
	om_midi_in_event(in, 0, 0x90, 48, 110);
	const int total = (int)RATE * 3 / OM_MAX_BLOCK;
	const uint64_t rideStart = (uint64_t)(total / 3) * OM_MAX_BLOCK;
	float l[OM_MAX_BLOCK], r[OM_MAX_BLOCK], last = 0.f;
	double worst = 0.0, sum = 0.0;
	for (int b = 0; b < total; b++)
	{
		const uint64_t now = om_studio_frame(studio);
		if (cls == &om_rings_engine && b % 300 == 0)   // it fades: strike it again
			om_midi_in_event(in, now, 0x90, 48, 110);
		if (ride && b > total / 3 && b % 8 == 0)
			om_midi_in_event(in, now, 0xB0, cc, (uint8_t)(70 + 57 * cos(2.0 * M_PI * 4.0 * (double)(now - rideStart) / RATE)));
		om_midi_in_advance(in, OM_MAX_BLOCK);
		om_studio_render(studio, l, r, OM_MAX_BLOCK);
		if (b > total / 3)
			for (int i = 0; i < OM_MAX_BLOCK; i++)
			{
				const double d = fabs((double)l[i] - last);
				worst = d > worst ? d : worst;
				sum += (double)l[i] * l[i];
				last = l[i];
			}
		else
			last = l[OM_MAX_BLOCK - 1];
	}
	*rms = sqrt(sum / (double)((total - total / 3) * OM_MAX_BLOCK));
	om_midi_in_destroy(in);
	om_studio_destroy(studio);
	return worst;
}

static void sendNow(uint8_t b0, uint8_t b1, uint8_t b2)
{
	om_midi_in_event(m, om_studio_frame(st), b0, b1, b2);
	run(1);
}

static void testBendAndProgram(void)
{
	setUp();
	om_midi_in_route(m, 2, 1, 1);   // channel 3: the engine that takes them, part 1
	om_midi_in_route(m, 3, 0, 0);   // channel 4: one that does not, as the LXR

	// the middle is none, the ends are the range: 2 semitones
	static const struct { uint8_t lsb, msb; float want; } bends[] = {
		{0x7F, 0x7F, 2.f}, {0, 0, -2.f}, {0, 0x40, 0.f}, {0, 0x60, 1.f},
	};
	for (size_t i = 0; i < sizeof(bends) / sizeof(bends[0]); i++)
	{
		sendNow(0xE2, bends[i].lsb, bends[i].msb);
		CHECK(fabsf(bendSeen[1] - bends[i].want) < 1e-3f, "bend %02x %02x: %.4f semitones, not %.1f", bends[i].msb, bends[i].lsb, bendSeen[1], bends[i].want);
	}
	CHECK(rawCount == 0, "a bend for the engine stops here (%d bytes)", rawCount);

	// RPN 0 sets the range; another RPN's data does not
	sendNow(0xB2, 101, 0); sendNow(0xB2, 100, 0); sendNow(0xB2, 6, 12);
	sendNow(0xE2, 0x7F, 0x7F);
	CHECK(fabsf(bendSeen[1] - 12.f) < 1e-3f, "RPN 0 = 12: the wheel's end is 12 semitones (%.3f)", bendSeen[1]);
	sendNow(0xB2, 100, 1); sendNow(0xB2, 6, 1);
	sendNow(0xE2, 0x7F, 0x7F);
	CHECK(fabsf(bendSeen[1] - 12.f) < 1e-3f, "RPN 1's data leaves the range alone (%.3f)", bendSeen[1]);
	sendNow(0xB2, 100, 0); sendNow(0xB2, 6, 100);
	sendNow(0xE2, 0x7F, 0x7F);
	CHECK(fabsf(bendSeen[1] - 24.f) < 1e-3f, "the range stops at 24 (%.3f)", bendSeen[1]);
	CHECK(paramCount() == 0 && rawCount == 0, "RPN messages move no parameter, reach no firmware");

	// Reset All Controllers, a re-route and a lost port each end the bend
	sendNow(0xB2, 121, 0);
	CHECK(bendSeen[1] == 0.f, "CC 121 ends the bend (%.3f)", bendSeen[1]);
	sendNow(0xE2, 0x7F, 0x7F);
	om_midi_in_route(m, 2, 1, 0);
	run(1);
	CHECK(bendSeen[1] == 0.f, "a re-route ends the bend on the part it left (%.3f)", bendSeen[1]);
	sendNow(0xE2, 0x7F, 0x7F);
	om_midi_in_reset(m);
	run(1);
	CHECK(bendSeen[0] == 0.f, "a lost port ends the bend (%.3f)", bendSeen[0]);
	om_midi_in_route(m, 2, 1, 1);

	// a program: the engine loads it, the panels read the whole part, once
	OmMidiInChange c;
	while (om_midi_in_change(m, &c)) {}
	logCount = rawCount = 0;
	sendNow(0xB2, 74, 50);            // a single change, then the program
	om_midi_in_event(m, om_studio_frame(st), 0xC2, 7, 0);
	run(1);
	CHECK(countOf(L_PROGRAM) == 1 && logged[logCount - 1].note == 7 && logged[logCount - 1].part == 1, "program change 7 reaches part 1");
	CHECK(rawCount == 0, "and stops here");
	int whole = 0, single = 0;
	while (om_midi_in_change(m, &c))
		c.param == OM_MIDI_WHOLE_PART ? whole++ : single++;
	CHECK(whole == 1 && single == 0, "it is one whole-part change (%d whole, %d single)", whole, single);

	// a CC right after a program, in the same block, is not taken for a repeat
	// of the value the engine had before the program
	mockValues[1][0] = 50;
	const uint64_t f = om_studio_frame(st);
	om_midi_in_event(m, f, 0xC2, 9, 0);
	om_midi_in_event(m, f + 1, 0xB2, 74, 50);
	run(1);
	CHECK(mockValues[1][0] == 50, "CC 74 = 50 after program 9 sets 50, not the program's 9 (%d)", mockValues[1][0]);
	while (om_midi_in_change(m, &c)) {}

	// an engine that takes neither: both go to the firmware, as before
	rawCount = 0;
	sendNow(0xE3, 0x7F, 0x7F);
	sendNow(0xC3, 5, 0);
	CHECK(rawCount == 5, "bend and program on the LXR's channel reach the raw sink (%d bytes)", rawCount);
	tearDown();
}

// A note's period in frames, by autocorrelation, half a second in (the real engines)
#define PITCH_WINDOW 4096
static double period(const OmEngineClass *cls, int bendMsb, double *level)
{
	OmStudio *studio = om_studio_create(RATE);
	const int e = om_studio_add_engine(studio, cls);
	OmMidiIn *in = om_midi_in_create(studio, RATE, NULL);
	om_midi_in_route(in, 0, (uint8_t)e, 0);
	if (cls == &om_va_engine)   // one oscillator, no drift, no vibrato, the filter open: one clear period
	{
		static const uint8_t plain[][2] = {{17, 0}, {109, 0}, {1, 0}, {74, 127}, {111, 0}};
		for (size_t k = 0; k < sizeof(plain) / sizeof(plain[0]); k++)
			om_midi_in_event(in, 0, 0xB0, plain[k][0], plain[k][1]);
	}
	om_midi_in_event(in, 0, 0xE0, bendMsb == 0x7F ? 0x7F : 0, (uint8_t)bendMsb);
	om_midi_in_event(in, 0, 0x90, cls == &om_va_engine ? 81 : 45, 110);   // A2, 110 Hz (VA's slot 1 plays bass, 3 octaves down)
	static float x[PITCH_WINDOW];
	float l[OM_MAX_BLOCK], r[OM_MAX_BLOCK];
	const int skip = (int)RATE / 4 / OM_MAX_BLOCK;
	for (int b = 0; b < skip + PITCH_WINDOW / OM_MAX_BLOCK; b++)
	{
		om_midi_in_advance(in, OM_MAX_BLOCK);
		om_studio_render(studio, l, r, OM_MAX_BLOCK);
		if (b >= skip)   // both sides: Rings' models can sound on one only
			for (int i = 0; i < OM_MAX_BLOCK; i++)
				x[(b - skip) * OM_MAX_BLOCK + i] = l[i] + r[i];
	}
	om_midi_in_destroy(in);
	om_studio_destroy(studio);
	double energy = 0.0;
	for (int i = 0; i < PITCH_WINDOW; i++)
		energy += (double)x[i] * x[i];
	*level = sqrt(energy / PITCH_WINDOW);
	// the normalised autocorrelation from 40 to 1000 Hz; the shortest lag within
	// 5 % of the best one, so an octave below is not taken
	enum { MIN_LAG = 44, MAX_LAG = 1102 };
	static double ac[MAX_LAG + 2];
	double best = 0.0;
	for (int lag = MIN_LAG - 1; lag <= MAX_LAG + 1; lag++)
	{
		double s = 0.0, a = 0.0, c = 0.0;
		for (int i = 0; i + lag < PITCH_WINDOW; i++)
		{
			s += (double)x[i] * x[i + lag];
			a += (double)x[i] * x[i];
			c += (double)x[i + lag] * x[i + lag];
		}
		ac[lag] = a > 0.0 && c > 0.0 ? s / sqrt(a * c) : 0.0;
		if (lag >= MIN_LAG && lag <= MAX_LAG && ac[lag] > best)
			best = ac[lag];
	}
	for (int lag = MIN_LAG; lag <= MAX_LAG; lag++)
		if (ac[lag] >= 0.95 * best && ac[lag] >= ac[lag - 1] && ac[lag] >= ac[lag + 1])
		{
			// a parabola through the peak: a fraction of a frame
			const double d = ac[lag - 1] - 2.0 * ac[lag] + ac[lag + 1];
			return lag + (d != 0.0 ? 0.5 * (ac[lag - 1] - ac[lag + 1]) / d : 0.0);
		}
	return 0.0;
}

static void testRealBendAndProgram(void)
{
	static const struct { const char *name; const OmEngineClass *cls; } engines[] = {
		{"VA", &om_va_engine}, {"Dexed", &om_dexed_engine}, {"Plaits", &om_plaits_engine}, {"Rings", &om_rings_engine},
	};
	for (size_t i = 0; i < sizeof(engines) / sizeof(engines[0]); i++)
	{
		double lf, lu, ld;
		const double flat = period(engines[i].cls, 0x40, &lf), up = period(engines[i].cls, 0x7F, &lu), down = period(engines[i].cls, 0, &ld);
		printf("%-7s period %.2f frames; bent up %.4f, down %.4f of it (2 semitones: 0.8909, 1.1225); level %.4f\n",
			engines[i].name, flat, up / flat, down / flat, lf);
		CHECK(lf > 1e-3 && flat > 0.0 && fabs(up / flat - 0.8909) < 0.005 && fabs(down / flat - 1.1225) < 0.005, "%s bends 2 semitones", engines[i].name);
	}

	// a program loads the preset, as the panel does
	OmStudio *studio = om_studio_create(RATE);
	om_studio_add_engine(studio, &om_va_engine);
	om_studio_add_engine(studio, &om_dexed_engine);
	om_studio_add_engine(studio, &om_plaits_engine);
	om_studio_add_engine(studio, &om_rings_engine);
	OmMidiIn *in = om_midi_in_create(studio, RATE, NULL);
	static const struct { uint8_t engine, preset; uint16_t param; uint8_t number; } programs[] = {
		{0, VA_P_PRESET, VA_P_PRESET, 5}, {1, DEXED_P_PROGRAM, DEXED_P_PROGRAM, 9}, {2, PL_P_PRESET, PL_P_PRESET, 3}, {3, RN_P_PRESET, RN_P_PRESET, 4},
	};
	float l[OM_MAX_BLOCK], r[OM_MAX_BLOCK];
	for (size_t i = 0; i < sizeof(programs) / sizeof(programs[0]); i++)
	{
		om_midi_in_route(in, (uint8_t)i, programs[i].engine, 1 % om_studio_engine(studio, programs[i].engine)->parts);
		om_midi_in_event(in, om_studio_frame(studio), (uint8_t)(0xC0 | i), programs[i].number, 0);
	}
	om_midi_in_advance(in, OM_MAX_BLOCK);
	om_studio_render(studio, l, r, OM_MAX_BLOCK);
	for (size_t i = 0; i < sizeof(programs) / sizeof(programs[0]); i++)
	{
		const uint8_t part = 1 % om_studio_engine(studio, programs[i].engine)->parts;
		const uint8_t got = om_studio_get_param(studio, programs[i].engine, part, programs[i].param);
		CHECK(got == programs[i].number, "%s takes program %d (%d)", om_studio_engine(studio, programs[i].engine)->name, programs[i].number, got);
	}
	int same = 1;
	for (int k = 0; k < PLAITS_PRESET_PARAMS; k++)
		same &= om_studio_get_param(studio, 2, 1, (uint16_t)k) == om_plaits_presets[3].p[k];
	CHECK(same, "Plaits' program 3 is its preset 3's sound");
	same = 1;
	for (int k = 0; k < RINGS_PRESET_PARAMS; k++)
		same &= om_studio_get_param(studio, 3, 0, (uint16_t)k) == om_rings_presets[4].p[k];
	CHECK(same, "Rings' program 4 is its preset 4's sound");
	om_midi_in_event(in, om_studio_frame(studio), 0xC2, 120, 0);   // Plaits has 64
	om_midi_in_advance(in, OM_MAX_BLOCK);
	om_studio_render(studio, l, r, OM_MAX_BLOCK);
	CHECK(om_studio_get_param(studio, 2, 1, PL_P_PRESET) == 3, "a program it does not have changes nothing");
	om_midi_in_destroy(in);
	om_studio_destroy(studio);
}

static void testRealEngines(void)
{
	static const struct { const char *name; const OmEngineClass *cls; uint8_t cc; } cases[] = {
		{"VA volume", &om_va_engine, 7}, {"VA cutoff", &om_va_engine, 74},
		{"Dexed volume", &om_dexed_engine, 7},
		{"Plaits volume", &om_plaits_engine, 7}, {"Plaits timbre", &om_plaits_engine, 74},
		{"Rings volume", &om_rings_engine, 7}, {"Rings brightness", &om_rings_engine, 74},
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		double rmsHeld, rmsRide;
		const double held = stepUnderCc(cases[i].cls, cases[i].cc, 0, &rmsHeld);
		const double ride = stepUnderCc(cases[i].cls, cases[i].cc, 1, &rmsRide);
		printf("%-16s step held %.4f, ridden %.4f; rms %.4f, %.4f\n", cases[i].name, held, ride, rmsHeld, rmsRide);
		CHECK(rmsHeld > 1e-4 && rmsRide > 1e-4, "%s sounds", cases[i].name);
		CHECK(ride < held * RIDE_STEP + 0.002, "%s steps %.4f ridden, %.4f held", cases[i].name, ride, held);
	}
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
	testCcs();
	testRealEngines();
	testBendAndProgram();
	testRealBendAndProgram();
	if (failures)
	{
		printf("%d midi_in check(s) failed\n", failures);
		return 1;
	}
	printf("midi_in: all checks passed\n");
	return 0;
}
