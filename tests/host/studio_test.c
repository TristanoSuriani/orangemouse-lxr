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
#include "fx.h"
#include "filter.h"
#include "om_registry_fx.h"

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
static uint8_t mockGetParam(void *self, uint8_t part, uint16_t param) { (void)self; (void)part; (void)param; return 0; }
static void mockTempo(void *self, float bpm) { ((Mock *)self)->tempo = bpm; }
#define MOCK 0.5f            // what a held mock note outputs (times mockGain)
static float mockGain = 1.f;
static void mockRender(void *self, float *const *stems, uint32_t frames)
{
	for (uint32_t i = 0; i < frames; i++)
		stems[0][i] = stems[1][i] = ((Mock *)self)->held ? MOCK * mockGain : 0.f;
}

static OmEngineClass mockClass(uint16_t block)
{
	return (OmEngineClass){.name = "mock", .parts = 1, .stems = 1, .block = block,
		.create = mockCreate, .destroy = mockDestroy, .note_on = mockNoteOn, .note_off = mockNoteOff,
		.set_param = mockParam, .get_param = mockGetParam, .set_tempo = mockTempo, .render = mockRender};
}

// the first frame at or after `from` where the left channel is (or isn't) a
// held note, counted from when it was made (the output is OM_STUDIO_LATENCY late)
static int firstFrame(const float *x, int n, int from, int on)
{
	for (int i = from + OM_STUDIO_LATENCY; i < n; i++)
		if ((x[i] == MOCK) == on)
			return i - OM_STUDIO_LATENCY;
	return -1;
}

static float peakOf(const float *x, int from, int to)
{
	float p = 0.f;
	for (int i = from; i < to; i++)
		p = fmaxf(p, fabsf(x[i]));
	return p;
}

// a studio for the mock checks: they look at exact values of a constant
// signal, which the master's DC blocker would take away
static OmStudio *mockStudio(void)
{
	OmStudio *st = om_studio_create(48000.f);
	om_studio_dc_blocker(st, 0);
	return st;
}

int main(void)
{
	enum { N = 4096 };
	static float l[N], r[N];

	{   // events land on their exact frame in an engine that renders any length
		const OmEngineClass cls = mockClass(0);
		OmStudio *st = mockStudio();
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
		OmStudio *st = mockStudio();
		om_studio_add_engine(st, &cls);
		om_studio_note_on(st, 1000, 0, 0, 60, 100);
		om_studio_render(st, l, r, N);
		CHECK(firstFrame(l, N, 0, 1) == 960, "block engine note on at frame %d, expected 960", firstFrame(l, N, 0, 1));
		om_studio_destroy(st);
	}
	{   // events scheduled out of order are played in order; late events play at once
		const OmEngineClass cls = mockClass(0);
		OmStudio *st = mockStudio();
		om_studio_add_engine(st, &cls);
		om_studio_note_off(st, 700, 0, 0, 60);
		om_studio_note_on(st, 300, 0, 0, 60, 100);
		om_studio_render(st, l, r, 1024);
		CHECK(firstFrame(l, 1024, 0, 1) == 300 && firstFrame(l, 1024, 300, 0) == 700,
		      "out-of-order events: on at %d, off at %d", firstFrame(l, 1024, 0, 1), firstFrame(l, 1024, 300, 0));
		om_studio_note_on(st, 10, 0, 0, 60, 100);   // already in the past
		om_studio_render(st, l, r, 64 + OM_STUDIO_LATENCY);
		CHECK(l[OM_STUDIO_LATENCY] == MOCK, "a late event did not play at the start of the next render");
		om_studio_destroy(st);
	}
	{   // the queue refuses events beyond OM_MAX_EVENTS, and unknown engines and parts
		const OmEngineClass cls = mockClass(0);
		OmStudio *st = mockStudio();
		om_studio_add_engine(st, &cls);
		int ok = 0;
		for (int i = 0; i < OM_MAX_EVENTS + 10; i++)
			ok += om_studio_note_on(st, (uint64_t)i, 0, 0, 60, 100) == 0;
		CHECK(ok == OM_MAX_EVENTS, "queue took %d events, expected %d", ok, OM_MAX_EVENTS);
		CHECK(om_studio_note_on(st, 0, 1, 0, 60, 100) == -1, "an unknown engine was accepted");
		CHECK(om_studio_note_on(st, 0, 0, 1, 60, 100) == -1, "an unknown part was accepted");
		om_studio_destroy(st);
	}
	{   // the mixer: pan; the tempo reaches the engines
		const OmEngineClass cls = mockClass(0);
		OmStudio *st = mockStudio();
		om_studio_add_engine(st, &cls);
		CHECK(om_studio_channels(st) == 1 && om_studio_channel(st, 0, 0) == 0, "one engine, one stem: one channel");
		om_studio_mix(st, 0, OM_MIX_PAN, 127);
		om_studio_set_tempo(st, 133.f);
		om_studio_note_on(st, 0, 0, 0, 60, 100);
		om_studio_render(st, l, r, 256);
		CHECK(l[200] == 0.f && r[200] == MOCK, "panned right: L %g R %g", l[200], r[200]);
		CHECK(om_studio_tempo(st) == 133.f, "tempo %g", om_studio_tempo(st));
		om_studio_destroy(st);
	}
	{   // level: 100 is 0 dB, a change glides over one block; mute and solo
		const OmEngineClass cls = mockClass(0);
		OmStudio *st = mockStudio();
		om_studio_add_engine(st, &cls);
		om_studio_add_engine(st, &cls);
		om_studio_note_on(st, 0, 0, 0, 60, 100);
		om_studio_render(st, l, r, 256);
		CHECK(l[200] == MOCK, "level 100 is not 0 dB: %g", l[200]);
		om_studio_mix(st, 0, OM_MIX_LEVEL, 50);   // (0.5)^3
		om_studio_render(st, l, r, 256);
		int jumps = 0;
		for (int i = 1; i < 256; i++)
			jumps += fabsf(l[i] - l[i - 1]) > MOCK / 32.f;
		CHECK(jumps == 0 && fabsf(l[200] - MOCK * 0.125f) < 1e-6f, "the level glides to 1/8: %d jumps, then %g", jumps, l[200]);
		om_studio_note_on(st, om_studio_frame(st), 1, 0, 60, 100);
		om_studio_mix(st, 0, OM_MIX_LEVEL, 100);
		om_studio_mix(st, 1, OM_MIX_MUTE, 1);
		om_studio_render(st, l, r, 256);
		CHECK(l[200] == MOCK, "a muted channel sounds: %g", l[200]);
		om_studio_mix(st, 1, OM_MIX_MUTE, 0);
		om_studio_mix(st, 1, OM_MIX_SOLO, 1);
		om_studio_render(st, l, r, 256);
		CHECK(l[200] == MOCK, "with channel 2 soloed, channel 1 still sounds: %g", l[200]);
		om_studio_mix(st, 1, OM_MIX_SOLO, 0);
		om_studio_render(st, l, r, 256);
		CHECK(fabsf(l[200] - 0.977f) < 1e-3f, "both channels (1.0) are limited to -0.2 dBFS: %g", l[200]);
		om_studio_destroy(st);
	}
	{   // the limiter: a hot mix never goes over -0.2 dBFS, and the meters say it worked
		const OmEngineClass cls = mockClass(0);
		OmStudio *st = mockStudio();
		om_studio_add_engine(st, &cls);
		om_studio_mix(st, 0, OM_MIX_LEVEL, 127);   // +6 dB
		mockGain = 6.f;                             // 3.0, +15 dB more
		om_studio_note_on(st, 0, 0, 0, 60, 100);
		om_studio_render(st, l, r, N);
		const float top = fmaxf(peakOf(l, 0, N), peakOf(r, 0, N));
		CHECK(top <= 0.977f, "the limiter let %g through", top);
		CHECK(fabsf(l[N - 1] - 0.977f) < 0.001f, "the limited level settles at %g", l[N - 1]);
		float m[8];
		const uint32_t n = om_studio_meters(st, m, 8);
		CHECK(n == 6 && m[0] > 6.f && m[3] > 6.f && m[5] < 0.16f, "meters: %u values, channel %g, master %g, limiter %g", n, m[0], m[3], m[5]);
		mockGain = 1.f;
		om_studio_destroy(st);
	}
	{   // every effect, at its defaults and with every knob at its minimum and its
		// maximum: a burst of noise in, then silence; the output must be finite,
		// must not run away, and must sound
		const char *names[OM_FX_TYPES] = {"off", "hall", "room", "delay", "chorus", "phaser", "flanger"};
		static float fl[OM_FX_BLOCK], fr[OM_FX_BLOCK];
		for (uint8_t type = OM_FX_HALL; type < OM_FX_TYPES; type++)
			for (int setting = 0; setting < 3; setting++)
			{
				OmFx *fx = om_fx_create(44100.f);
				for (uint16_t p = 0; p < OM_FX_PARAMS && setting; p++)
					om_fx_set_param(fx, p, setting == 1 ? om_fx_params[p].min : om_fx_params[p].max);
				CHECK(om_fx_set_type(fx, type) == 0, "%s cannot be created", names[type]);
				uint32_t seed = 1;
				float peak = 0.f, energy = 0.f;
				int bad = 0;
				for (int b = 0; b < 44100 * 3 / OM_FX_BLOCK; b++)
				{
					for (int i = 0; i < OM_FX_BLOCK; i++)
					{
						seed = seed * 1664525u + 1013904223u;
						const float x = b < 44100 / 10 / OM_FX_BLOCK ? ((seed >> 9) * (1.f / 4194304.f) - 1.f) * 0.5f : 0.f;
						seed = seed * 1664525u + 1013904223u;   // the right side: its own noise
						fl[i] = x;
						fr[i] = x ? ((seed >> 9) * (1.f / 4194304.f) - 1.f) * 0.5f : 0.f;
					}
					om_fx_process(fx, fl, fr, OM_FX_BLOCK);
					for (int i = 0; i < OM_FX_BLOCK; i++)
					{
						bad += !isfinite(fl[i]) || !isfinite(fr[i]);
						peak = fmaxf(peak, fmaxf(fabsf(fl[i]), fabsf(fr[i])));
						energy += fl[i] * fl[i] + fr[i] * fr[i];
					}
				}
				const char *at[3] = {"defaults", "all minimum", "all maximum"};
				CHECK(bad == 0 && peak < 8.f, "%s at %s: %d bad samples, peak %g", names[type], at[setting], bad, peak);
				CHECK(energy > 1e-3f, "%s at %s is silent (energy %g)", names[type], at[setting], energy);
				om_fx_destroy(fx);
			}
	}
	{   // ducking (PLAN.md 9.1): a held pad dips while its key plays, and comes back with the release
		enum { LONG = 96000 };   // 2 s at 48 kHz
		static float dl[LONG], dr[LONG];
		const OmEngineClass cls = mockClass(0);
		// the pad's level at a time after the key's note on (frames, the output is OM_STUDIO_LATENCY late)
		#define PAD(at) dl[(at) + OM_STUDIO_LATENCY]
		for (int amount = 0; amount <= 2; amount++)
		{
			OmStudio *st = mockStudio();
			om_studio_add_engine(st, &cls);   // channel 0: the key (a kick)
			om_studio_add_engine(st, &cls);   // channel 1: a pad, held
			om_studio_mix(st, 0, OM_MIX_MUTE, 1);   // a muted key still keys
			om_studio_mix(st, 1, OM_MIX_DUCK, amount == 0 ? 0 : amount == 1 ? 127 : 64);
			om_studio_note_on(st, 0, 1, 0, 60, 100);
			om_studio_note_on(st, 4800, 0, 0, 36, 100);    // the key: 100 ms in, held 200 ms
			om_studio_note_off(st, 14400, 0, 0, 36);
			om_studio_render(st, dl, dr, LONG - OM_STUDIO_LATENCY);
			const float before = PAD(4000), during = PAD(14000);
			const float tau = PAD(14400 + 7200), late = PAD(14400 + 48000);   // 150 ms (the default release) and 1 s after
			if (amount == 0)
				CHECK(before == MOCK && during == MOCK, "no duck: the pad stays at %g (%g, %g)", MOCK, before, during);
			else if (amount == 1)
			{
				CHECK(before == MOCK && during < 0.001f, "full duck: the pad goes from %g to %g while the key plays", before, during);
				CHECK(tau > 0.25f && tau < 0.38f && late > 0.49f, "it comes back with the release: %g after 150 ms, %g after 1 s", tau, late);
			}
			else
				CHECK(fabsf(during - MOCK * (1.f - 64.f / 127.f)) < 0.002f, "half a duck halves the pad: %g", during);
			om_studio_destroy(st);
		}
		{   // a channel is never its own key
			OmStudio *st = mockStudio();
			om_studio_add_engine(st, &cls);
			om_studio_mix(st, 0, OM_MIX_DUCK, 127);
			om_studio_mix(st, 0, OM_MIX_DUCK_KEY, 0);
			om_studio_note_on(st, 0, 0, 0, 60, 100);
			om_studio_render(st, dl, dr, 4096);
			CHECK(PAD(3000) == MOCK, "a channel keyed on itself dipped to %g", PAD(3000));
			om_studio_destroy(st);
		}
		#undef PAD
	}
	{   // the trance gate (PLAN.md 9.2): the 8ths pattern chops a held pad on the tempo, or on the clock
		enum { LONG = 96000 };
		static float gl[LONG], gr[LONG];
		const OmEngineClass cls = mockClass(0);
		#define AT(f) gl[(f) + OM_STUDIO_LATENCY]
		for (int mode = 0; mode < 3; mode++)
		{
			OmStudio *st = mockStudio();
			om_studio_add_engine(st, &cls);
			om_studio_set_tempo(st, 120.f);   // a 16th: 6000 frames at 48 kHz
			om_studio_mix(st, 0, OM_MIX_GATE, mode == 0 ? 0 : 127);
			om_studio_mix(st, 0, OM_MIX_GATE_SMOOTH, 0);
			om_studio_note_on(st, 0, 0, 0, 60, 100);
			if (mode == 2)   // the tracker's clock: a start at 1000, then a tick every 1000 frames (a 16th: 6000)
			{
				om_studio_clock(st, 1000, OM_CLOCK_START);
				for (int t = 0; t < 40; t++) om_studio_clock(st, 1000 + 1000 * (uint64_t)t, OM_CLOCK_TICK);
			}
			om_studio_render(st, gl, gr, 48000);
			if (mode == 0)
				CHECK(AT(3000) == MOCK && AT(9000) == MOCK, "gate 0 leaves the pad alone: %g %g", AT(3000), AT(9000));
			else if (mode == 1)
				CHECK(fabsf(AT(3000) - MOCK) < 0.001f && AT(9000) < 0.001f && fabsf(AT(15000) - MOCK) < 0.001f,
				      "on the tempo: step 1 on (%g), 2 off (%g), 3 on (%g)", AT(3000), AT(9000), AT(15000));
			else
				CHECK(fabsf(AT(1000 + 3000) - MOCK) < 0.001f && AT(1000 + 9000) < 0.001f && fabsf(AT(1000 + 15000) - MOCK) < 0.001f,
				      "on the clock: on (%g), off (%g), on (%g) from its start", AT(4000), AT(10000), AT(16000));
			om_studio_destroy(st);
		}
		{   // smoothing: a long one rounds the step's edges
			OmStudio *st = mockStudio();
			om_studio_add_engine(st, &cls);
			om_studio_set_tempo(st, 120.f);
			om_studio_mix(st, 0, OM_MIX_GATE, 127);
			om_studio_mix(st, 0, OM_MIX_GATE_SMOOTH, 127);   // 64 ms
			om_studio_note_on(st, 0, 0, 0, 60, 100);
			om_studio_render(st, gl, gr, 24000);
			CHECK(AT(6000 + 480) > 0.2f && AT(6000 + 480) < 0.5f, "64 ms of smoothing: 10 ms into an off step the pad is still at %g", AT(6480));
			om_studio_destroy(st);
		}
		#undef AT
	}
	{   // every channel filter filters: low-passes cut a high sine, high-passes a low one,
		// band-passes both, the notch its own frequency; with full resonance and
		// noise they stay finite and bounded
		const char *names[OM_FILTER_TYPES] = {"off", "lp12", "lp24", "ladder", "vintage", "obxd", "k35", "diode",
			"hp12", "hp24", "k35hp", "bp", "notch", "obxbp"};
		static float fl[4096], fr[4096];
		// the level of a sine after the filter, in dB against the sine (after 2048 frames to settle)
		#define FILTERED(type, cutoff, hz, out) do { \
			OmFilter *f = om_filter_create(44100.f); \
			om_filter_set(f, type, cutoff, 0.f); \
			for (int i = 0; i < 4096; i++) fl[i] = fr[i] = 0.5f * sinf(6.2831853f * (hz) * i / 44100.f); \
			om_filter_process(f, fl, fr, 4096); \
			double e = 0; for (int i = 2048; i < 4096; i++) e += fl[i] * fl[i]; \
			out = 10.0 * log10(e / 2048 / 0.125 + 1e-12); \
			om_filter_destroy(f); } while (0)
		for (uint8_t t = OM_FILTER_LP12; t < OM_FILTER_TYPES; t++)
		{
			const int lp = t <= OM_FILTER_DIODE, hp = t >= OM_FILTER_HP12 && t <= OM_FILTER_K35_HP;
			const int bp = t == OM_FILTER_BP || t == OM_FILTER_OBXD_BP;
			double low, high, mid;
			FILTERED(t, -24.f, 5000.f, high);   // cutoff 220 Hz (semitones from A4)
			FILTERED(t, 24.f, 55.f, low);       // cutoff 3.5 kHz
			FILTERED(t, 0.f, 440.f, mid);       // cutoff at the sine
			if (lp) CHECK(high < -20.0, "%s: a 5 kHz sine through a 220 Hz low-pass is only %.1f dB down", names[t], high);
			if (hp) CHECK(low < -20.0, "%s: a 55 Hz sine through a 3.5 kHz high-pass is only %.1f dB down", names[t], low);
			if (bp) CHECK(high < -10.0 && low < -10.0, "%s: a band-pass lets through %.1f dB above, %.1f dB below", names[t], high, low);
			if (t == OM_FILTER_NOTCH) CHECK(mid < -10.0, "notch: its own frequency is only %.1f dB down", mid);
			// full resonance, noise
			OmFilter *f = om_filter_create(44100.f);
			om_filter_set(f, t, 10.f, 1.f);
			uint32_t seed = 7;
			float peak = 0.f;
			int bad = 0;
			for (int rep = 0; rep < 20; rep++)
			{
				for (int i = 0; i < 4096; i++) { seed = seed * 1664525u + 1013904223u; fl[i] = fr[i] = ((seed >> 9) * (1.f / 4194304.f) - 1.f) * 0.5f; }
				om_filter_process(f, fl, fr, 4096);
				for (int i = 0; i < 4096; i++) { bad += !isfinite(fl[i]); peak = fmaxf(peak, fabsf(fl[i])); }
			}
			CHECK(bad == 0 && peak < 16.f, "%s at full resonance: %d bad samples, peak %g", names[t], bad, peak);
			om_filter_destroy(f);
		}
	}
	{   // the master's DC blocker: a constant falls away within a second, a tone passes
		const OmEngineClass cls = mockClass(0);
		OmStudio *st = om_studio_create(48000.f);
		om_studio_add_engine(st, &cls);
		om_studio_note_on(st, 0, 0, 0, 60, 100);
		static float dl[48000], dr[48000];
		om_studio_render(st, dl, dr, 48000);
		CHECK(fabsf(dl[47999]) < 0.001f && dl[100] > 0.4f, "DC: %g at the start, %g after 1 s", dl[100], dl[47999]);
		om_studio_destroy(st);
	}
	{   // the delay bus: echoes on the tempo (1/8 dotted at 120 BPM is 0.375 s)
		const OmEngineClass cls = mockClass(0);
		const float rate = 48000.f;
		OmStudio *st = mockStudio();   // a burst of constant: no DC blocker tail in the gap
		om_studio_add_engine(st, &cls);
		om_studio_set_tempo(st, 120.f);
		om_studio_fx(st, 1, OM_FX_DELAY_LEFT, 8);   // 1/8 dotted (the default)
		om_studio_fx(st, 1, OM_FX_DELAY_RIGHT, 8);
		om_studio_fx(st, 1, OM_FX_DELAY_FEEDBACK, 0);
		om_studio_fx(st, 1, OM_FX_DELAY_DEPTH, 0);
		om_studio_mix(st, 0, OM_MIX_SEND_B, 100);
		om_studio_note_on(st, 0, 0, 0, 60, 100);
		om_studio_note_off(st, 480, 0, 0, 60);   // a 10 ms burst
		static float el[48000], er[48000];
		om_studio_render(st, el, er, 48000);
		const int echo = (int)(0.375f * rate) + OM_STUDIO_LATENCY;
		const float gap = peakOf(el, 480 + OM_STUDIO_LATENCY + 64, echo - 200), at = peakOf(el, echo, echo + 480);
		CHECK(gap < 0.01f && at > 0.1f, "the echo: %g before 0.375 s, %g at it", gap, at);
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
