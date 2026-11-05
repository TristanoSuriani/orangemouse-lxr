/*
 * rings_engine.cpp
 *
 * See rings_engine.h. Rings renders blocks of kMaxBlockSize (24) frames at
 * 48 kHz into a queue the resampler turns into the studio's rate, as Plaits
 * does. Each block takes one strum: the next note waiting, if any.
 *
 * The input comes in at the studio's rate, a block at a time, and is
 * resampled to 48 kHz into a FIFO the blocks read from. Rings renders up to
 * a few blocks ahead of the studio, so the FIFO starts PRIME frames full:
 * the input is that much (1.3 ms) late.
 *
 * Once nothing has been strummed or fed for a second and the output is
 * silent, Rings stops processing and outputs exact zeros.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <cmath>
#include <cstring>
#include <new>

#include "rings/dsp/part.h"
#include "resampler.h"
#include "rings_engine.h"
#include "om_registry_rings.h"

namespace
{
using rings::kMaxBlockSize;

const float RATE = rings::kSampleRate;   // Rings' own
constexpr float OUTPUT = 1.f;   // the presets' volumes bring them to Plaits' level
constexpr int QUEUE = 4 * OM_MAX_BLOCK;
constexpr int FIFO = 512;                // the input at 48 kHz
constexpr int PRIME = 64;
constexpr int PENDING = 8;               // notes waiting for their strum
constexpr int TONIC = 36;                // C2: the sympathetic strings' drone (before transpose)
const rings::ResonatorModel MODELS[] = {
	rings::RESONATOR_MODEL_MODAL, rings::RESONATOR_MODEL_SYMPATHETIC_STRING, rings::RESONATOR_MODEL_STRING,
	rings::RESONATOR_MODEL_FM_VOICE, rings::RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED,
	rings::RESONATOR_MODEL_STRING_AND_REVERB,
};
const int POLYPHONY[] = {1, 2, 4};

struct Rings
{
	rings::Part part;
	uint16_t reverb[32768];   // the string-and-reverb model's, as on the module
	uint8_t value[RINGS_NUM_PARAMS];
	float lastGain;   // -1: none yet
	om::SincKernel down;      // 48 kHz to the studio's rate
	om::SincKernel up;        // and back, for the input
	om::Resampler<QUEUE> out;
	om::Resampler<QUEUE> in;
	float fifo[FIFO];
	int fifoCount = 0;
	struct { uint8_t note, velocity; } pending[PENDING];
	int pendingCount = 0;
	float note = 69.f;        // the last strum's, for the blocks between strums
	float velocity = 100.f / 127.f;
	bool fed = false;         // input arrived since the last block
	int quietBlocks = 0;
	bool active = false;
};

inline float unit(uint8_t v) { return v / 127.f; }

void renderBlock(Rings *rn)
{
	float in[kMaxBlockSize] = {}, l[kMaxBlockSize] = {}, r[kMaxBlockSize] = {};
	const uint8_t *v = rn->value;
	const bool external = v[RN_P_INPUT] > 0;
	const int take = rn->fifoCount < (int)kMaxBlockSize ? rn->fifoCount : (int)kMaxBlockSize;
	if (take)
	{
		std::memcpy(in, rn->fifo, take * sizeof(float));
		rn->fifoCount -= take;
		std::memmove(rn->fifo, rn->fifo + take, rn->fifoCount * sizeof(float));
	}
	rings::PerformanceState perf = {};
	if (rn->pendingCount)
	{
		perf.strum = true;
		rn->note = rn->pending[0].note;
		rn->velocity = rn->pending[0].velocity / 127.f;
		rn->pendingCount--;
		std::memmove(rn->pending, rn->pending + 1, rn->pendingCount * sizeof(rn->pending[0]));
		rn->active = true;
	}
	if (external && rn->fed)
		rn->active = true;
	rn->fed = false;
	if (rn->active)
	{
		const float shift = (float)(v[RN_P_TRANSPOSE] - 24);
		perf.internal_exciter = !external;
		perf.tonic = TONIC + shift;
		perf.note = rn->note - TONIC;
		perf.chord = (int32_t)(unit(v[RN_P_STRUCTURE]) * (rings::kNumChords - 1) + 0.5f);
		rings::Patch patch;
		patch.structure = unit(v[RN_P_STRUCTURE]);
		// velocity 100 strikes as set; harder is brighter
		patch.brightness = std::fmin(1.f, unit(v[RN_P_BRIGHTNESS]) * (0.6f + 0.4f * rn->velocity / (100.f / 127.f)));
		patch.damping = unit(v[RN_P_DAMPING]);
		patch.position = unit(v[RN_P_POSITION]);
		rn->part.set_model(MODELS[v[RN_P_MODEL]]);   // both reconfigure the resonators: only on a change
		if (rn->part.polyphony() != POLYPHONY[v[RN_P_POLYPHONY]])
			rn->part.set_polyphony(POLYPHONY[v[RN_P_POLYPHONY]]);
		rn->part.Process(perf, patch, in, l, r, kMaxBlockSize);
		float peak = 0.f;
		for (size_t i = 0; i < kMaxBlockSize; i++)
			peak = std::fmax(peak, std::fmax(std::fabs(l[i]), std::fabs(r[i])));
		if (perf.strum || take || peak > 1e-5f)
			rn->quietBlocks = 0;
		else if (++rn->quietBlocks * (int)kMaxBlockSize > RATE)
			rn->active = false;
	}
	const float x = v[RN_P_VOLUME] / 100.f;   // a fader: 100 is 0 dB
	const float gain = OUTPUT * x * x * x;
	// the level ramps across the block, so a moving volume (a CC) does not step
	const float from = rn->lastGain < 0.f ? gain : rn->lastGain;
	rn->lastGain = gain;
	float *ql = rn->out.left(), *qr = rn->out.right();
	for (size_t i = 0; i < kMaxBlockSize; i++)
	{
		const float g = from + (gain - from) * (float)(i + 1) / (float)kMaxBlockSize;
		ql[i] = rn->active ? l[i] * g : 0.f;
		qr[i] = rn->active ? r[i] * g : 0.f;
	}
	rn->out.pushed(kMaxBlockSize);
}

void *ringsCreate(float rate)
{
	// Rings() zeroes everything first: Part's constructor is empty and Init
	// leaves some of the models' state to their first use (the module's RAM
	// starts at zero), so renders were not repeatable
	Rings *rn = new (std::nothrow) Rings();
	if (!rn)
		return nullptr;
	rn->part.Init(rn->reverb);
	rn->lastGain = -1.f;
	rn->down.init(RATE, rate);
	rn->up.init(rate, RATE);
	rn->out.reset();
	rn->in.reset();
	for (int i = 0; i < RINGS_NUM_PARAMS; i++)
		rn->value[i] = om_rings_params[i].def;
	return rn;
}

void ringsDestroy(void *self)
{
	delete static_cast<Rings *>(self);
}

void ringsNoteOn(void *self, uint8_t part, uint8_t note, uint8_t velocity)
{
	Rings *rn = static_cast<Rings *>(self);
	if (part >= RINGS_PARTS || velocity == 0 || rn->pendingCount == PENDING)
		return;
	rn->pending[rn->pendingCount++] = {note, velocity};
}

void ringsNoteOff(void *, uint8_t, uint8_t) {}   // a resonator rings on

void ringsSetParam(void *self, uint8_t part, uint16_t param, uint8_t value)
{
	if (part >= RINGS_PARTS || param >= RINGS_NUM_PARAMS)
		return;
	const OmParamInfo &info = om_rings_params[param];
	static_cast<Rings *>(self)->value[param] = value < info.min ? info.min : value > info.max ? info.max : value;
}

uint8_t ringsGetParam(void *self, uint8_t part, uint16_t param)
{
	if (part >= RINGS_PARTS || param >= RINGS_NUM_PARAMS)
		return 0;
	return static_cast<Rings *>(self)->value[param];
}

void ringsSetTempo(void *, float) {}

void ringsRender(void *self, float *const *stems, uint32_t frames)
{
	Rings *rn = static_cast<Rings *>(self);
	for (uint32_t i = 0; i < frames; i++)
	{
		while (rn->out.needs())
			renderBlock(rn);
		rn->out.pull(rn->down, stems[0][i], stems[1][i]);
	}
	rn->out.compact();
}

int ringsInputSource(void *self)
{
	return static_cast<Rings *>(self)->value[RN_P_INPUT] - 1;   // 0: off, its own exciter
}

void ringsInput(void *self, const float *left, const float *right)
{
	Rings *rn = static_cast<Rings *>(self);
	if (rn->fifoCount == 0)   // starting, or starved: PRIME frames of headroom
		rn->fifoCount = PRIME, std::memset(rn->fifo, 0, PRIME * sizeof(float));
	for (int i = 0; i < OM_MAX_BLOCK; i++)
		rn->in.push(0.5f * (left[i] + right[i]), 0.f);
	float mono, unused;
	while (!rn->in.needs())
	{
		rn->in.pull(rn->up, mono, unused);
		if (rn->fifoCount == FIFO)   // nothing reads it (cannot happen while Rings renders): start again
			rn->fifoCount = 0;
		rn->fifo[rn->fifoCount++] = mono;
	}
	rn->in.compact();
	rn->fed = true;
}
} // namespace

extern "C" const OmEngineClass om_rings_engine = {
	"Rings", RINGS_PARTS, RINGS_PARTS, 0, om_rings_params, OM_RINGS_NUM_PARAMS,
	ringsCreate, ringsDestroy, ringsNoteOn, ringsNoteOff, ringsSetParam, ringsGetParam, ringsSetTempo,
	nullptr, ringsRender, ringsInputSource, ringsInput,
};
