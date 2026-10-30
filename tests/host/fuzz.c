/*
 * fuzz.c
 *
 * Fuzzing the whole studio (PLAN.md Phase 7.3, and Testing: "random
 * parameter sweeps and random note and MIDI streams, including note-on
 * storms, missing note-offs and panics; every run must pass the health
 * checks"). One studio (the LXR's firmware boots once), and rounds of:
 *
 *   1. parameters: every engine's, the mixer's and the effects', random
 *      values (now and then out of range), mixed with notes;
 *   2. a note-on storm in one block, on every part, velocity 0 too, and no
 *      note-offs;
 *   3. a random song on all 16 tracks: random routes (some invalid), lengths,
 *      tempos, chords, note-offs and retrigs, played, switched and stopped;
 *   4. random MIDI bytes into the LXR (running status, clock, sysex, junk);
 *   5. a panic: every parameter back to its default, every note off, the
 *      tracker stopped, MIDI all-notes-off and stop; then the studio must
 *      fall silent: every channel and the master below -80 dBFS.
 *
 * Health, checked on every sample of the master and every channel's meter:
 * finite, no denormals, the limiter holds (peak at most 1).
 *
 *   fuzz [rounds] [seed]       (tests/run.sh: 4 rounds from seed 1)
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "tracker.h"
#include "om_registry_fx.h"
#include "om_registry_mix.h"

extern const OmEngineClass om_lxr_engine, om_va_engine, om_dexed_engine, om_plaits_engine;
void web_log(const char *msg) { (void)msg; }
void web_sd_written(uint32_t firstSector, uint32_t count) { (void)firstSector; (void)count; }
void web_midi_out(uint8_t byte) { (void)byte; }
void web_midi_in(uint8_t byte);

#define RATE 44100.f
#define ENGINES 4
#define SILENT 1e-4f   // -80 dBFS

static uint32_t rng = 1;
static uint32_t rnd(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return rng;
}
static int below(int n) { return (int)(rnd() % (uint32_t)n); }
static int chance(int percent) { return below(100) < percent; }

static OmStudio *st;
static OmTracker *tr;
static const OmEngineClass *engines[ENGINES] = {&om_lxr_engine, &om_va_engine, &om_dexed_engine, &om_plaits_engine};
static int failures;

typedef struct
{
	long samples, nonFinite, denormal, badMeters;
	float peak, channelPeak[OM_MAX_CHANNELS + 5];
} Health;

static void render(uint32_t blocks, Health *h)
{
	float l[OM_MAX_BLOCK], r[OM_MAX_BLOCK], meters[OM_MAX_CHANNELS + 5];
	const int n = om_studio_channels(st) + 4;   // the channels, the two buses, the master's left and right
	for (uint32_t b = 0; b < blocks; b++)
	{
		om_tracker_advance(tr, OM_MAX_BLOCK);
		om_studio_render(st, l, r, OM_MAX_BLOCK);
		for (int i = 0; i < OM_MAX_BLOCK; i++)
			for (int c = 0; c < 2; c++)
			{
				const float x = c ? r[i] : l[i];
				h->samples++;
				if (!isfinite(x)) { h->nonFinite++; continue; }
				if (x != 0.f && fabsf(x) < FLT_MIN) h->denormal++;
				h->peak = fmaxf(h->peak, fabsf(x));
			}
		om_studio_meters(st, meters, OM_MAX_CHANNELS + 5);
		for (int c = 0; c < n; c++)
		{
			if (!isfinite(meters[c])) { h->badMeters++; continue; }
			h->channelPeak[c] = fmaxf(h->channelPeak[c], meters[c]);
		}
	}
}

static void expect(int ok, int round, const char *phase, const char *what)
{
	if (ok)
		return;
	printf("FAIL round %d, %s: %s\n", round, phase, what);
	failures++;
}

static void healthy(const Health *h, int round, const char *phase)
{
	char text[160];
	snprintf(text, sizeof text, "%ld samples not finite, %ld denormal, %ld meters not finite, peak %.3f",
	         h->nonFinite, h->denormal, h->badMeters, h->peak);
	expect(!h->nonFinite && !h->denormal && !h->badMeters && h->peak <= 1.f, round, phase, text);
	if (getenv("FUZZ_VERBOSE"))
		printf("   round %d, %s: %s\n", round, phase, text);
}

static uint64_t now(void) { return om_studio_frame(st); }

// a value for a parameter: in its range, now and then any byte
static uint8_t value(const OmParamInfo *p)
{
	if (chance(3))
		return (uint8_t)below(256);
	return (uint8_t)(p->min + below(p->max - p->min + 1));
}

static void randomNote(uint64_t at, int offs)
{
	const int e = below(ENGINES);
	const int part = chance(2) ? below(16) : below(engines[e]->parts);
	const uint8_t note = (uint8_t)(chance(2) ? below(256) : below(128));
	if (offs && chance(40))
		om_studio_note_off(st, at, (uint8_t)e, (uint8_t)part, note);
	else
		om_studio_note_on(st, at, (uint8_t)e, (uint8_t)part, note, (uint8_t)below(128));
}

static void phaseParams(int round)
{
	Health h = {0};
	for (int step = 0; step < 40; step++)
	{
		for (int i = 0; i < 60; i++)
		{
			const uint64_t at = now() + (uint64_t)below(OM_MAX_BLOCK * 4);
			const int what = below(10);
			if (what < 6)
			{
				const int e = below(ENGINES);
				const OmEngineClass *cls = engines[e];
				const uint16_t param = chance(2) ? (uint16_t)below(1000) : (uint16_t)below(cls->numParams);
				const uint8_t v = param < cls->numParams ? value(&cls->params[param]) : (uint8_t)below(256);
				om_studio_set_param(st, at, (uint8_t)e, (uint8_t)below(cls->parts + (chance(2) ? 4 : 0)), param, v);
			}
			else if (what < 8)
			{
				const int target = chance(80) ? below(om_studio_channels(st) + 2) : below(256);
				const uint8_t param = (uint8_t)(chance(95) ? below(OM_MIX_NUM_PARAMS) : below(256));
				om_studio_mix(st, (uint8_t)target, param, param < OM_MIX_NUM_PARAMS ? value(&om_mix_params[param]) : (uint8_t)below(256));
			}
			else if (what < 9)
			{
				const uint16_t param = (uint16_t)(chance(95) ? below(OM_FX_NUM_PARAMS) : below(1000));
				om_studio_fx(st, (uint8_t)below(3), param, param < OM_FX_NUM_PARAMS ? value(&om_fx_params[param]) : (uint8_t)below(256));
			}
			else
				randomNote(at, 1);
		}
		om_studio_set_tempo(st, chance(5) ? (float)below(1000) : 40.f + (float)below(220));
		render(3, &h);
	}
	render(200, &h);
	healthy(&h, round, "parameters");
}

static void phaseStorm(int round)
{
	Health h = {0};
	const uint64_t at = now();
	for (int i = 0; i < 1500; i++)
		randomNote(at + (uint64_t)below(OM_MAX_BLOCK), 0);
	render(700, &h);
	healthy(&h, round, "note-on storm");
}

static void phaseTracker(int round)
{
	Health h = {0};
	om_tracker_clear(tr);
	om_tracker_set_tempo(tr, 40.f + (float)below(260));
	for (int k = 0; k < OM_TRACKS; k++)
	{
		const int e = chance(5) ? below(10) : below(ENGINES);
		om_tracker_route(tr, (uint8_t)k, (uint8_t)e, (uint8_t)(chance(5) ? below(20) : below(e < ENGINES ? engines[e]->parts : 1)));
		om_tracker_set_mute(tr, (uint8_t)k, (uint8_t)chance(10));
		om_tracker_set_solo(tr, (uint8_t)k, (uint8_t)chance(3));
	}
	const int patterns = 1 + below(4);
	for (int p = 0; p < patterns; p++)
	{
		const int rows = 1 + below(OM_MAX_ROWS);
		om_tracker_set_length(tr, (uint8_t)p, (uint8_t)rows);
		om_tracker_set_pattern_tempo(tr, (uint8_t)p, chance(50) ? 0.f : 30.f + (float)below(400));
		om_tracker_set_order(tr, (uint16_t)p, (uint8_t)p);
		for (int i = 0; i < rows * 6; i++)
		{
			OmCell c = {{OM_NOTE_EMPTY, OM_NOTE_EMPTY, OM_NOTE_EMPTY, OM_NOTE_EMPTY}, (uint8_t)below(128), 0};
			if (chance(15))
				c.note[0] = OM_NOTE_OFF;
			else
				for (int j = 0, n = 1 + below(OM_CHORD); j < n; j++)
					c.note[j] = (uint8_t)below(128);
			if (chance(20))
				c.retrig = (uint8_t)(chance(10) ? below(256) : 2 + below(7));
			if (chance(20)) c.length = (uint8_t)(chance(5) ? below(256) : below(65));   // note lengths, glides
			if (chance(20)) c.glide = (uint8_t)(chance(5) ? below(256) : 1);
			if (chance(20)) c.cond = (uint8_t)(chance(5) ? below(256) : below(OM_COND_COUNT));
			if (chance(15))   // the effect column: any control (now and then out of range), slides
				c.fx = (uint8_t)((chance(5) ? below(128) : below(OM_FX_COUNT)) | (chance(50) ? OM_FX_SLIDE : 0)), c.fxValue = (uint8_t)below(256);
			om_tracker_set_cell(tr, (uint8_t)p, (uint8_t)below(rows + 2), (uint8_t)below(OM_TRACKS + 2), &c);
		}
	}
	om_tracker_set_order(tr, (uint16_t)patterns, OM_ORDER_END);
	om_tracker_play_song(tr, 0);
	render(600, &h);
	om_tracker_play_pattern(tr, (uint8_t)below(patterns + 1));
	render(400, &h);
	om_tracker_stop(tr);
	om_tracker_play_song(tr, (uint16_t)below(patterns + 2));
	for (int i = 0; i < 20; i++)
	{
		render((uint32_t)(1 + below(40)), &h);
		if (chance(30)) om_tracker_stop(tr);
		if (chance(30)) om_tracker_play_pattern(tr, (uint8_t)below(patterns));
		if (chance(30)) om_tracker_set_tempo(tr, chance(5) ? (float)below(2000) : 40.f + (float)below(260));
	}
	render(200, &h);
	healthy(&h, round, "tracker");
}

static void phaseMidi(int round)
{
	Health h = {0};
	static const uint8_t status[] = {0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0, 0xf7, 0xf8, 0xfa, 0xfb, 0xfc, 0xfe, 0xff};
	for (int step = 0; step < 50; step++)
	{
		for (int i = 0; i < 40; i++)
		{
			uint8_t b;
			if (chance(30)) b = (uint8_t)(status[below(sizeof status)] | (status[0] < 0xf0 ? below(16) : 0));
			else if (chance(5)) b = (uint8_t)below(256);
			else b = (uint8_t)below(128);
			if ((b & 0xf0) < 0xf0 && b >= 0x80) b = (uint8_t)((b & 0xf0) | below(16));
			web_midi_in(b);
		}
		render(4, &h);
	}
	render(200, &h);
	healthy(&h, round, "MIDI");
}

// Each engine queues OM_MAX_EVENTS events: the panic renders a block after
// every part, so none is refused (refused counts those that were).
static long refused;
static void drain(void)
{
	float l[OM_MAX_BLOCK], r[OM_MAX_BLOCK];
	om_studio_render(st, l, r, OM_MAX_BLOCK);
}

static void defaults(void)
{
	for (int e = 0; e < ENGINES; e++)
	{
		const OmEngineClass *cls = engines[e];
		// the LXR's parameters are its kit's, the same for every part
		for (uint8_t part = 0; part < (e == 0 ? 1 : cls->parts); part++)
		{
			for (uint16_t p = 0; p < cls->numParams; p++)
				refused += om_studio_set_param(st, now(), (uint8_t)e, part, p, cls->params[p].def) < 0;
			drain();
		}
	}
	for (int c = 0; c < om_studio_channels(st); c++)
		for (uint8_t p = 0; p < OM_MIX_NUM_PARAMS; p++)
			if (p != OM_MIX_FX_TYPE && p != OM_MIX_RETURN)
				om_studio_mix(st, (uint8_t)c, p, om_mix_params[p].def);
	for (uint8_t bus = 0; bus < 2; bus++)
	{
		for (uint16_t p = 0; p < OM_FX_NUM_PARAMS; p++)
			om_studio_fx(st, bus, p, om_fx_params[p].def);
		om_studio_mix(st, (uint8_t)(OM_MIX_BUS_A + bus), OM_MIX_FX_TYPE, om_mix_params[OM_MIX_FX_TYPE].def);
		om_studio_mix(st, (uint8_t)(OM_MIX_BUS_A + bus), OM_MIX_RETURN, om_mix_params[OM_MIX_RETURN].def);
	}
	om_studio_mix(st, OM_MIX_MASTER, OM_MIX_LEVEL, om_mix_params[OM_MIX_LEVEL].def);
	om_studio_set_tempo(st, 120.f);
}

static void phasePanic(int round)
{
	om_tracker_stop(tr);
	om_tracker_clear(tr);
	web_midi_in(0xfc);   // MIDI stop, and all notes off on every channel
	for (uint8_t ch = 0; ch < 16; ch++)
	{
		web_midi_in((uint8_t)(0xb0 | ch));
		web_midi_in(123);
		web_midi_in(0);
	}
	om_studio_clock(st, now(), OM_CLOCK_STOP);
	defaults();
	for (int e = 0; e < ENGINES; e++)
		for (uint8_t part = 0; part < engines[e]->parts; part++)
		{
			for (int note = 0; note < 128; note++)
				refused += om_studio_note_off(st, now(), (uint8_t)e, part, (uint8_t)note) < 0;
			drain();
		}
	// The LXR's drums ignore note-offs (as on the hardware): a hit made while a
	// decay was extreme rings on for a minute. One quiet hit of each at the
	// defaults takes over its voice, so only that hit's own decay remains.
	for (uint8_t part = 0; part < engines[0]->parts; part++)
		refused += om_studio_note_on(st, now(), 0, part, 60, 1) < 0;
	drain();
	// Dexed's sounding voices keep the sound they started with, and a DX7
	// release can last a minute. A quiet note on each of its 16 voices, at the
	// default sound, takes them all over (a stuck voice would not be taken).
	for (uint8_t part = 0; part < engines[2]->parts; part++)
	{
		for (uint8_t note = 0; note < 16; note++)
			refused += om_studio_note_on(st, now(), 2, part, (uint8_t)(40 + note), 1) < 0;
		drain();
		for (uint8_t note = 0; note < 16; note++)
			refused += om_studio_note_off(st, now(), 2, part, (uint8_t)(40 + note)) < 0;
		drain();
	}
	expect(!refused, round, "panic", "the studio refused some of the panic's events (its queue was full)");
	refused = 0;
	Health tail = {0}, last = {0};
	render((uint32_t)(10.f * RATE / OM_MAX_BLOCK), &tail);   // releases and the reverbs' tails
	render((uint32_t)(1.f * RATE / OM_MAX_BLOCK), &last);
	healthy(&tail, round, "panic");
	const int channels = om_studio_channels(st);
	for (int c = 0; c < channels + 4; c++)
		if (last.channelPeak[c] >= SILENT)
		{
			char text[120];
			snprintf(text, sizeof text, "%s %d still sounds 11 s after the panic (peak %.6f)",
			         c < channels ? "channel" : c < channels + 2 ? "send bus" : "master side", c < channels ? c : c - channels, last.channelPeak[c]);
			expect(0, round, "panic", text);
		}
}

int main(int argc, char **argv)
{
	const int rounds = argc > 1 ? atoi(argv[1]) : 4;
	const uint32_t seed = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 10) : 1;
	st = om_studio_create(RATE);
	for (int e = 0; e < ENGINES; e++)
		if (om_studio_add_engine(st, engines[e]) != e) { fprintf(stderr, "cannot create engine %d\n", e); return 1; }
	tr = om_tracker_create(st, RATE);
	Health boot = {0};
	render(400, &boot);   // the LXR boots
	for (int round = 1; round <= rounds; round++)
	{
		rng = seed * 2654435761u + (uint32_t)round * 40503u;
		if (!rng) rng = 1;
		const int before = failures;
		phaseParams(round);
		phaseStorm(round);
		phaseTracker(round);
		phaseMidi(round);
		phasePanic(round);
		if (failures == before)
			printf("ok: round %d (seed %u): parameters, a note-on storm, a random song, MIDI, a panic, then silence\n", round, seed);
	}
	om_tracker_destroy(tr);
	om_studio_destroy(st);
	return failures ? 1 : 0;
}
