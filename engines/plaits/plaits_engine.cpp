/*
 * plaits_engine.cpp
 *
 * See plaits_engine.h. Plaits runs at its own rate, kCorrectedSampleRate
 * (47872 Hz), as on the module: its tuning, envelopes, low-pass gate and
 * engines are all timed for it (PLAN.md 7.6: at 44.1 kHz they ran 5-17 %
 * slow). Each part renders whole blocks of kBlockSize (12) frames into a
 * queue at that rate, and a windowed-sinc resampler turns the queue into the
 * studio's rate (resampler.h; a note waits at most 11 frames and the
 * 16-tap half of the filter, 0.6 ms). A voice stops rendering once its note is off and it has
 * been silent for 200 ms.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <cmath>
#include <cstring>
#include <new>

#include "om_plaits_voice.h"
#include "resampler.h"
#include "plaits_engine.h"
#include "om_registry_plaits.h"

namespace
{
using plaits::kBlockSize;

constexpr size_t RAM = 16384;          // each voice's engine memory, as on the module
constexpr float OUTPUT = 0.35f;        // four voices stay below full scale
const float RATE = plaits::kCorrectedSampleRate;       // Plaits' own
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
	om::Resampler<QUEUE> out;   // rendered frames at Plaits' rate
};

struct Plaits
{
	float rate;
	om::SincKernel kernel;   // Plaits' rate to the studio's
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
	float *ql = p.out.left(), *qr = p.out.right();
	for (size_t i = 0; i < kBlockSize; i++)
	{
		ql[i] = l[i] * gain;
		qr[i] = r[i] * gain;
	}
	p.out.pushed(kBlockSize);
}

void *plaitsCreate(float rate)
{
	Plaits *pl = new (std::nothrow) Plaits();   // zeroed first, as the module's RAM starts: renders repeat
	if (!pl)
		return nullptr;
	pl->rate = rate;
	pl->kernel.init(RATE, rate);
	for (Part &p : pl->part)
	{
		p.out.reset();
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
			while (p.out.needs())
				renderBlock(pl, p);
			p.out.pull(pl->kernel, stems[2 * k][i], stems[2 * k + 1][i]);
		}
		p.out.compact();
	}
}
} // namespace

extern "C" const OmEngineClass om_plaits_engine = {
	"Plaits", PLAITS_PARTS, PLAITS_PARTS, 0, om_plaits_params, OM_PLAITS_NUM_PARAMS,
	plaitsCreate, plaitsDestroy, plaitsNoteOn, plaitsNoteOff, plaitsSetParam, plaitsGetParam, plaitsSetTempo,
	nullptr, plaitsRender,
};
