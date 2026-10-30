/*
 * dexed_engine.cpp
 *
 * See dexed_engine.h. What Dexed's PluginProcessor does around msfa, without
 * JUCE: voice allocation (its stealing score), the patch's own transpose,
 * one LFO per part that restarts when a key goes down with none held, and
 * the per-voice clipping of its output. msfa renders in blocks of N = 64
 * frames, the class's block. Its tables are global, so there is one instance.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <cstring>
#include <memory>
#include <new>

#include "msfa/controllers.h"
#include "msfa/dx7note.h"
#include "msfa/env.h"
#include "msfa/exp2.h"
#include "msfa/freqlut.h"
#include "msfa/lfo.h"
#include "msfa/pitchenv.h"
#include "msfa/porta.h"
#include "msfa/sin.h"
#include "msfa/synth.h"
#include "msfa/tuning.h"
#include "engine/EngineMkI.h"

#include "dexed_engine.h"
#include "om_registry_dexed.h"

static_assert(N == OM_MAX_BLOCK, "msfa's block is the studio's");

extern "C" {
extern const char *const om_dexed_bank_names[];
extern const uint8_t om_dexed_carts[][32 * 128];
}

namespace
{
constexpr int VOICES = 16;
constexpr float OUTPUT = 0.25f;   // a chord of several voices stays below full scale

// A 128-byte packed voice of a bulk dump into msfa's 156-byte form (Dexed's
// Cartridge::unpackProgram).
void unpack(const uint8_t *bulk, uint8_t *p)
{
	for (int op = 0; op < 6; op++)
	{
		const uint8_t *b = bulk + op * 17;
		uint8_t *o = p + op * 21;
		for (int i = 0; i < 11; i++)
			o[i] = b[i] & 0x7F;
		o[11] = b[11] & 3;
		o[12] = (b[11] >> 2) & 3;
		o[13] = b[12] & 7;
		o[20] = (b[12] >> 3) & 0x0F;
		o[14] = b[13] & 3;
		o[15] = (b[13] >> 2) & 7;
		o[16] = b[14] & 0x7F;
		o[17] = b[15] & 1;
		o[18] = (b[15] >> 1) & 0x1F;
		o[19] = b[16] & 0x7F;
	}
	for (int i = 0; i < 8; i++)
		p[126 + i] = bulk[102 + i] & 0x7F;
	p[134] = bulk[110] & 0x1F;
	p[135] = bulk[111] & 7;
	p[136] = (bulk[111] >> 3) & 1;
	for (int i = 0; i < 4; i++)
		p[137 + i] = bulk[112 + i] & 0x7F;
	p[141] = bulk[116] & 1;
	p[142] = (bulk[116] >> 1) & 7;
	p[143] = (bulk[116] >> 4) & 7;
	p[144] = bulk[117] & 0x7F;
	for (int i = 0; i < 10; i++)
		p[145 + i] = bulk[118 + i] & 0x7F;
	p[155] = 0x3F;   // all six operators on
}

struct Voice
{
	std::unique_ptr<Dx7Note> note;
	int midi = -1;          // the key that started it
	bool keydown = false;
	bool live = false;
	uint32_t seq = 0;       // when its key went down
};

struct Part
{
	uint8_t value[DEXED_NUM_PARAMS];
	uint8_t patch[156];
	Voice voice[VOICES];
	Lfo lfo;
	Controllers ctrls;
	int next = 0;
};

struct Dexed
{
	std::shared_ptr<TuningState> tuning;
	EngineMkI core;
	Part part[DEXED_PARTS];
	uint32_t seq = 0;
};

bool created;

void loadPatch(Part &p)
{
	unpack(om_dexed_carts[p.value[DEXED_P_BANK]] + 128 * p.value[DEXED_P_PROGRAM], p.patch);
	p.lfo.reset(p.patch + 137);
}

// Dx7Note's constructor leaves fb_buf_ (the feedback operator's last two
// samples) unset, so a voice's first note started its feedback from whatever
// the memory held (tests/host/fuzz.c under AddressSanitizer). The note is
// built in zeroed memory instead.
Dx7Note *newNote(std::shared_ptr<TuningState> tuning)
{
	void *mem = ::operator new(sizeof(Dx7Note));
	memset(mem, 0, sizeof(Dx7Note));
	return new (mem) Dx7Note(tuning, nullptr);
}

void *dexedCreate(float rate)
{
	if (created)
		return nullptr;
	Dexed *d = new (std::nothrow) Dexed;
	if (!d)
		return nullptr;
	created = true;
	Exp2::init();
	Tanh::init();
	Sin::init();
	Freqlut::init(rate);
	Lfo::init(rate);
	PitchEnv::init(rate);
	Env::init_sr(rate);
	Porta::init_sr(rate);
	d->tuning = createStandardTuning();
	for (Part &p : d->part)
	{
		Controllers &c = p.ctrls;
		c.core = &d->core;
		c.masterTune = 0;
		c.pitch_range_up = c.pitch_range_dn = 2;
		c.pitch_step = 0;
		c.pitch_cc = 0x2000;
		c.modwheel_cc = c.foot_cc = c.breath_cc = c.aftertouch_cc = 0;
		c.portamento_enable_cc = false;
		c.portamento_cc = 0;
		c.portamento_gliss_cc = false;
		c.mpeEnabled = false;
		c.refresh();
		for (Voice &v : p.voice)
			v.note.reset(newNote(d->tuning));
		for (int i = 0; i < DEXED_NUM_PARAMS; i++)
			p.value[i] = om_dexed_params[i].def;
		loadPatch(p);
	}
	return d;
}

void dexedDestroy(void *self)
{
	delete static_cast<Dexed *>(self);
	created = false;
}

void dexedNoteOn(void *self, uint8_t part, uint8_t note, uint8_t velocity)
{
	Dexed *d = static_cast<Dexed *>(self);
	Part &p = d->part[part];
	int pitch = note + (p.value[DEXED_P_TRANSPOSE] - 24) + (p.patch[144] - 24);
	if (pitch < 0 || pitch > 127 || velocity == 0)
		return;
	// Dexed's choice: a silent voice, then one whose key is up, then the same
	// key; among equals the oldest
	bool anyDown = false;
	int best = 0, bestScore = -1;
	for (int i = 0; i < VOICES; i++)
	{
		const Voice &v = p.voice[i];
		anyDown |= v.keydown;
		const int score = (!v.note->isPlaying() ? 4 : 0) + (!v.keydown ? 2 : 0) + (v.midi == pitch ? 1 : 0);
		if (score > bestScore || (score == bestScore && v.seq < p.voice[best].seq))
		{
			best = i;
			bestScore = score;
		}
	}
	if (!anyDown)
		p.lfo.keydown();
	Voice &v = p.voice[best];
	v.note->init(p.patch, pitch, velocity, 1, &p.ctrls);
	if (p.patch[136])   // oscillator key sync
		v.note->oscSync();
	v.midi = pitch;
	v.keydown = v.live = true;
	v.seq = ++d->seq;
}

void dexedNoteOff(void *self, uint8_t part, uint8_t note)
{
	Part &p = static_cast<Dexed *>(self)->part[part];
	const int pitch = note + (p.value[DEXED_P_TRANSPOSE] - 24) + (p.patch[144] - 24);
	for (Voice &v : p.voice)
		if (v.keydown && v.midi == pitch)
		{
			v.note->keyup();
			v.keydown = false;
		}
}

void dexedSetParam(void *self, uint8_t part, uint16_t param, uint8_t value)
{
	if (part >= DEXED_PARTS || param >= DEXED_NUM_PARAMS)
		return;
	Part &p = static_cast<Dexed *>(self)->part[part];
	const OmParamInfo &info = om_dexed_params[param];
	if (param == DEXED_P_BANK && value >= om_dexed_banks)
		value = (uint8_t)(om_dexed_banks - 1);
	p.value[param] = value < info.min ? info.min : value > info.max ? info.max : value;
	if (param == DEXED_P_BANK || param == DEXED_P_PROGRAM)
		loadPatch(p);   // the notes that sound keep theirs; the next ones play the new one
}

uint8_t dexedGetParam(void *self, uint8_t part, uint16_t param)
{
	if (part >= DEXED_PARTS || param >= DEXED_NUM_PARAMS)
		return 0;
	return static_cast<Dexed *>(self)->part[part].value[param];
}

void dexedSetTempo(void *, float) {}

void dexedRender(void *self, float *const *stems, uint32_t frames)
{
	(void)frames;   // always N (the class's block)
	Dexed *d = static_cast<Dexed *>(self);
	for (int k = 0; k < DEXED_PARTS; k++)
	{
		Part &p = d->part[k];
		float sum[N] = {};
		const int32_t lfoValue = p.lfo.getsample(), lfoDelay = p.lfo.getdelay();
		AlignedBuf<int32_t, N> buf;
		for (Voice &v : p.voice)
		{
			if (!v.live)
				continue;
			std::memset(buf.get(), 0, sizeof(int32_t) * N);
			v.note->compute(buf.get(), lfoValue, lfoDelay, &p.ctrls);
			for (int j = 0; j < N; j++)
			{
				const int32_t val = buf.get()[j] >> 4;
				const int clip = val < -(1 << 24) ? -0x8000 : val >= (1 << 24) ? 0x7fff : val >> 9;
				sum[j] += clip * (1.f / 32768.f);
			}
			if (!v.keydown && !v.note->isPlaying())
				v.live = false;
		}
		const float x = p.value[DEXED_P_VOLUME] / 100.f;   // a fader: 100 is 0 dB
		const float gain = OUTPUT * x * x * x;
		for (int j = 0; j < N; j++)
			stems[2 * k][j] = stems[2 * k + 1][j] = sum[j] * gain;
	}
}
} // namespace

extern "C" {

const OmEngineClass om_dexed_engine = {
	"Dexed", DEXED_PARTS, DEXED_PARTS, N, om_dexed_params, OM_DEXED_NUM_PARAMS,
	dexedCreate, dexedDestroy, dexedNoteOn, dexedNoteOff, dexedSetParam, dexedGetParam, dexedSetTempo,
	nullptr, dexedRender,
};

const char *om_dexed_bank_name(int bank)
{
	return bank >= 0 && bank < om_dexed_banks ? om_dexed_bank_names[bank] : "";
}

void om_dexed_program_name(int bank, int program, char out[11])
{
	std::memset(out, 0, 11);
	if (bank < 0 || bank >= om_dexed_banks || program < 0 || program >= 32)
		return;
	const uint8_t *name = om_dexed_carts[bank] + 128 * program + 118;
	for (int i = 0; i < 10; i++)
		out[i] = name[i] >= 32 && name[i] < 127 ? (char)name[i] : ' ';
}

} // extern "C"
