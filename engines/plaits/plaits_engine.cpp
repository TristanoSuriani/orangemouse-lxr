/*
 * plaits_engine.cpp
 *
 * See plaits_engine.h. Plaits runs at its own rate, kCorrectedSampleRate
 * (47872 Hz), as on the module: its tuning, envelopes, low-pass gate and
 * engines are all timed for it (PLAN.md 7.6: at 44.1 kHz they ran 5-17 %
 * slow). Each part renders whole blocks of kBlockSize (12) frames into a
 * queue at that rate, and a windowed-sinc resampler turns the queue into the
 * studio's rate (a note waits at most 11 frames and the 16-tap half of the
 * filter, 0.6 ms). A voice stops rendering once its note is off and it has
 * been silent for 200 ms.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <cmath>
#include <cstring>
#include <new>

#include "om_plaits_voice.h"
#include "plaits_engine.h"
#include "om_registry_plaits.h"

namespace
{
using plaits::kBlockSize;

constexpr size_t RAM = 16384;          // each voice's engine memory, as on the module
constexpr float OUTPUT = 0.35f;        // four voices stay below full scale
const float RATE = plaits::kCorrectedSampleRate;       // Plaits' own
constexpr int HALF = 16;               // the resampler's taps on each side
constexpr int PHASES = 256;            // its filter, at this many fractional positions
constexpr int QUEUE = 4 * OM_MAX_BLOCK;

struct Voice
{
	plaits::OmPlaitsVoice dsp;
	char ram[RAM];
	int note = -1;           // the key, before transpose
	float pitch = 0.f;       // the note Plaits plays
	float velocity = 0.f;
	float env = 0.f;         // the attack envelope, 0..1
	bool gate = false;
	bool active = false;
	bool retrigger = false;  // a stolen voice: its gate goes low for a block first
	int silentBlocks = 0;
	uint32_t age = 0;
};

struct Part
{
	uint8_t value[PLAITS_NUM_PARAMS];
	Voice voice[PLAITS_VOICES];
	float queue[2][QUEUE];   // rendered frames at Plaits' rate, from HALF - 1 before the next output
	int queued = HALF - 1;   // the first: silence
	double pos = HALF - 1;   // where in the queue the next output frame falls
};

struct Plaits
{
	float rate;
	double step;             // Plaits' frames per studio frame
	float kernel[PHASES + 1][2 * HALF];   // the resampler's filter, per fractional position
	Part part[PLAITS_PARTS];
	uint32_t age = 0;
};

inline float unit(uint8_t v) { return v / 127.f; }

void renderBlock(Plaits *pl, Part &p)
{
	float l[kBlockSize] = {}, r[kBlockSize] = {};
	const uint8_t *v = p.value;
	plaits::Patch patch = {};
	patch.engine = v[PL_P_ENGINE];
	patch.harmonics = unit(v[PL_P_HARMONICS]);
	patch.timbre = unit(v[PL_P_TIMBRE]);
	patch.morph = unit(v[PL_P_MORPH]);
	patch.decay = unit(v[PL_P_DECAY]);
	patch.lpg_colour = unit(v[PL_P_COLOUR]);
	patch.timbre_modulation_amount = (v[PL_P_TIMBRE_ENV] - 64) / 63.f;
	patch.morph_modulation_amount = (v[PL_P_MORPH_ENV] - 64) / 63.f;
	const bool stereo = patch.engine == plaits::OM_PL_STRING_MACHINE;
	// the attack: from 1 ms (0) to 4 s (127), a step per block
	const float attack = 0.001f * std::exp2(v[PL_P_ATTACK] * 12.f / 127.f);
	const float step = kBlockSize / (attack * RATE);
	for (Voice &vo : p.voice)
	{
		if (!vo.active)
			continue;
		plaits::Modulations m = {};
		m.trigger_patched = m.level_patched = true;
		m.trigger = vo.gate && !vo.retrigger ? 1.f : 0.f;
		vo.retrigger = false;
		vo.env = vo.gate ? std::fmin(1.f, vo.env + step) : 0.f;
		m.level = vo.env * vo.velocity;
		patch.note = vo.pitch;
		float out[kBlockSize], aux[kBlockSize];
		vo.dsp.Render(patch, m, out, aux, kBlockSize);
		float peak = 0.f;
		for (size_t i = 0; i < kBlockSize; i++)
		{
			l[i] += out[i];
			r[i] += stereo ? aux[i] : out[i];
			peak = std::fmax(peak, std::fabs(out[i]));
		}
		if (vo.gate || peak > 1e-5f)
			vo.silentBlocks = 0;
		else if (++vo.silentBlocks * (int)kBlockSize > 0.2f * RATE)
			vo.active = false;
	}
	const float x = v[PL_P_VOLUME] / 100.f;   // a fader: 100 is 0 dB
	const float gain = OUTPUT * x * x * x;
	for (size_t i = 0; i < kBlockSize; i++)
	{
		p.queue[0][p.queued + i] = l[i] * gain;
		p.queue[1][p.queued + i] = r[i] * gain;
	}
	p.queued += kBlockSize;
}

void *plaitsCreate(float rate)
{
	Plaits *pl = new (std::nothrow) Plaits;
	if (!pl)
		return nullptr;
	pl->rate = rate;
	pl->step = RATE / rate;
	// a windowed sinc (Blackman-Harris), its cutoff just below the lower of
	// the two rates' Nyquist frequencies; each position sums to 1
	const double cut = 0.95 * std::fmin(1.0, 1.0 / pl->step);
	for (int ph = 0; ph <= PHASES; ph++)
	{
		double sum = 0.0, h[2 * HALF];
		for (int k = 0; k < 2 * HALF; k++)
		{
			const double x = (k - HALF + 1) - (double)ph / PHASES;   // taps from -15 to 16, the position in between
			const double sinc = x == 0.0 ? 1.0 : std::sin(M_PI * cut * x) / (M_PI * cut * x);
			const double w = 2.0 * M_PI * (x + HALF) / (2.0 * HALF);
			const double win = 0.35875 - 0.48829 * std::cos(w) + 0.14128 * std::cos(2.0 * w) - 0.01168 * std::cos(3.0 * w);
			h[k] = sinc * win;
			sum += h[k];
		}
		for (int k = 0; k < 2 * HALF; k++)
			pl->kernel[ph][k] = (float)(h[k] / sum);
	}
	for (Part &p : pl->part)
	{
		std::memset(p.queue, 0, sizeof p.queue);   // the resampler starts on silence
		for (int i = 0; i < PLAITS_NUM_PARAMS; i++)
			p.value[i] = om_plaits_params[i].def;
		for (Voice &vo : p.voice)
		{
			stmlib::BufferAllocator allocator(vo.ram, RAM);
			vo.dsp.Init(&allocator);
		}
	}
	return pl;
}

void plaitsDestroy(void *self)
{
	delete static_cast<Plaits *>(self);
}

float pitchOf(const Plaits *pl, const Part &p, int note)
{
	(void)pl;
	return note + (p.value[PL_P_TRANSPOSE] - 24);
}

void plaitsNoteOn(void *self, uint8_t part, uint8_t note, uint8_t velocity)
{
	Plaits *pl = static_cast<Plaits *>(self);
	Part &p = pl->part[part];
	if (velocity == 0)
		return;
	// a silent voice, else the oldest released one, else the oldest
	Voice *best = nullptr;
	int bestScore = -1;
	for (Voice &vo : p.voice)
	{
		const int score = (!vo.active ? 4 : 0) + (!vo.gate ? 2 : 0) + (vo.note == note ? 1 : 0);
		if (score > bestScore || (score == bestScore && vo.age < best->age))
		{
			best = &vo;
			bestScore = score;
		}
	}
	best->retrigger = best->active && best->gate;
	best->note = note;
	best->pitch = pitchOf(pl, p, note);
	best->velocity = velocity / 127.f;
	best->gate = best->active = true;
	best->silentBlocks = 0;
	best->age = ++pl->age;
}

void plaitsNoteOff(void *self, uint8_t part, uint8_t note)
{
	for (Voice &vo : static_cast<Plaits *>(self)->part[part].voice)
		if (vo.gate && vo.note == note)
			vo.gate = false;
}

void plaitsSetParam(void *self, uint8_t part, uint16_t param, uint8_t value)
{
	if (part >= PLAITS_PARTS || param >= PLAITS_NUM_PARAMS)
		return;
	Plaits *pl = static_cast<Plaits *>(self);
	Part &p = pl->part[part];
	const OmParamInfo &info = om_plaits_params[param];
	p.value[param] = value < info.min ? info.min : value > info.max ? info.max : value;
	if (param == PL_P_TRANSPOSE)
		for (Voice &vo : p.voice)
			if (vo.note >= 0)
				vo.pitch = pitchOf(pl, p, vo.note);
}

uint8_t plaitsGetParam(void *self, uint8_t part, uint16_t param)
{
	if (part >= PLAITS_PARTS || param >= PLAITS_NUM_PARAMS)
		return 0;
	return static_cast<Plaits *>(self)->part[part].value[param];
}

void plaitsSetTempo(void *, float) {}

void plaitsRender(void *self, float *const *stems, uint32_t frames)
{
	Plaits *pl = static_cast<Plaits *>(self);
	for (int k = 0; k < PLAITS_PARTS; k++)
	{
		Part &p = pl->part[k];
		for (uint32_t i = 0; i < frames; i++)
		{
			const int at = (int)p.pos;
			while (at + HALF >= p.queued)
				renderBlock(pl, p);
			const double where = (p.pos - at) * PHASES;
			const int ph = (int)where;
			const float frac = (float)(where - ph);
			const float *a = pl->kernel[ph], *b = pl->kernel[ph + 1];
			const float *l = p.queue[0] + at - HALF + 1, *r = p.queue[1] + at - HALF + 1;
			float outL = 0.f, outR = 0.f;
			for (int t = 0; t < 2 * HALF; t++)
			{
				const float h = a[t] + (b[t] - a[t]) * frac;
				outL += l[t] * h;
				outR += r[t] * h;
			}
			stems[2 * k][i] = outL;
			stems[2 * k + 1][i] = outR;
			p.pos += pl->step;
		}
		// keep HALF - 1 frames before the next output's
		const int drop = (int)p.pos - (HALF - 1);
		p.queued -= drop;
		p.pos -= drop;
		std::memmove(p.queue[0], p.queue[0] + drop, p.queued * sizeof(float));
		std::memmove(p.queue[1], p.queue[1] + drop, p.queued * sizeof(float));
	}
}
} // namespace

extern "C" const OmEngineClass om_plaits_engine = {
	"Plaits", PLAITS_PARTS, PLAITS_PARTS, 0, om_plaits_params, OM_PLAITS_NUM_PARAMS,
	plaitsCreate, plaitsDestroy, plaitsNoteOn, plaitsNoteOff, plaitsSetParam, plaitsGetParam, plaitsSetTempo,
	nullptr, plaitsRender,
};
