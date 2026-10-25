/*
 * fx.cpp
 *
 * See fx.h. Surge's effects (sst-effects) run on their own configuration
 * (OmConfig): like sst's ConcreteConfig, but its random numbers come from a
 * seeded generator, so a render is the same every time and on every platform.
 * Each byte parameter becomes the effect's value through registry/fx.json's
 * map; a few of each effect's parameters are fixed here (FIXED): the mix is
 * all wet, as a send effect is.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "sst/basic-blocks/simd/setup.h"
#include "sst/effects/Delay.h"
#include "sst/effects/Flanger.h"
#include "sst/effects/Phaser.h"
#include "sst/effects/Reverb1.h"
#include "sst/effects/Reverb2.h"

#include "fx.h"
#include "om_registry_fx.h"

namespace
{
namespace sfx = sst::effects;

struct OmConfig
{
	struct BC
	{
		static constexpr uint16_t maxParamCount{20};
		float paramStorage[maxParamCount];
		template <typename... Types> BC(Types...) {}
	};
	struct GS
	{
		double sampleRate;
		uint32_t seed{0x12345678u};
		GS(double sr) : sampleRate(sr) {}
	};
	struct ES
	{
	};

	using BaseClass = BC;
	using GlobalStorage = GS;
	using EffectStorage = ES;
	using ValueStorage = float *;
	using BiquadAdapter = OmConfig;

	static constexpr int blockSize{OM_FX_BLOCK};

	static inline float floatValueAt(const BaseClass *const e, const ValueStorage *const, int idx) { return e->paramStorage[idx]; }
	static inline int intValueAt(const BaseClass *const e, const ValueStorage *const, int idx) { return (int)std::round(e->paramStorage[idx]); }
	static inline float envelopeRateLinear(GlobalStorage *s, float f) { return std::pow(2.f, -f) * (blockSize / (s->sampleRate)); }
	static inline float temposyncRatio(GlobalStorage *, EffectStorage *, int) { return 1; }
	static inline bool isDeactivated(EffectStorage *, int) { return false; }
	static inline bool isTemposynced(EffectStorage *, int) { return false; }
	static inline bool isExtended(EffectStorage *, int) { return false; }
	static inline float rand01(GlobalStorage *s)
	{
		s->seed ^= s->seed << 13;   // xorshift32
		s->seed ^= s->seed >> 17;
		s->seed ^= s->seed << 5;
		return (s->seed >> 8) * (1.f / 16777216.f);
	}
	static inline double sampleRate(GlobalStorage *s) { return s->sampleRate; }
	static inline double sampleRateInv(GlobalStorage *s) { return 1.0 / s->sampleRate; }
	static inline float noteToPitch(GlobalStorage *, float p) { return std::pow(2.0, p / 12); }
	static inline float noteToPitchIgnoringTuning(GlobalStorage *s, float p) { return noteToPitch(s, p); }
	static inline float noteToPitchInv(GlobalStorage *s, float p) { return 1.0 / noteToPitch(s, p); }
	static inline float dbToLinear(GlobalStorage *, float f) { return std::pow(10.f, f / 20.f); }
};

// Each type's parameters: OM_FX_PARAMS is in registry order, grouped by type.
constexpr uint16_t FIRST[OM_FX_TYPES + 1] = {
	0, OM_FX_HALL_PRE, OM_FX_ROOM_PRE, OM_FX_DELAY_LEFT, OM_FX_CHORUS_RATE, OM_FX_PHASER_CENTER, OM_FX_FLANGER_MODE, OM_FX_PARAMS};

uint8_t typeOf(uint16_t param)
{
	uint8_t t = OM_FX_HALL;
	while (t + 1 < OM_FX_TYPES && param >= FIRST[t + 1])
		t++;
	return t;
}

// The delay's tempo divisions (registry/fx.json OM_FX_DELAY_LEFT), in beats.
constexpr float DIVISIONS[] = {0.125f, 1.f / 6, 0.1875f, 0.25f, 1.f / 3, 0.375f, 0.5f, 2.f / 3, 0.75f, 1.f, 4.f / 3, 1.5f, 2.f, 3.f, 4.f};

struct Fixed
{
	int index;
	float value;
};

struct Slot
{
	virtual ~Slot() = default;
	virtual void set(int index, float value) = 0;
	virtual void reset() = 0;
	virtual void process(float *l, float *r) = 0;   // one OM_FX_BLOCK, in place
};

template <typename FX> struct SstSlot : Slot
{
	OmConfig::GS gs;
	OmConfig::ES es;
	std::unique_ptr<FX> fx;

	SstSlot(float rate, const Fixed *fixed, int count) : gs(rate), fx(new (std::nothrow) FX(&gs, &es, nullptr))
	{
		if (!fx)
			return;
		for (int i = 0; i < FX::numParams; i++)
			fx->paramStorage[i] = fx->paramAt(i).defaultVal;
		for (int i = 0; i < count; i++)
			fx->paramStorage[fixed[i].index] = fixed[i].value;
	}
	void set(int index, float value) override { fx->paramStorage[index] = value; }
	void reset() override
	{
		gs.seed = 0x12345678u;
		fx->initialize();
	}
	void process(float *l, float *r) override { fx->processBlock(l, r); }
};

// A stereo chorus: one modulated delay per side, the right side's sweep
// shifted by up to half a cycle (width). Triangle sweep, linear interpolation.
struct Chorus : Slot
{
	static constexpr int SIZE = 8192;   // 170 ms at 48 kHz: the longest delay plus the sweep
	float rate;
	float buf[2][SIZE];
	uint32_t pos;
	double phase;
	float hz = 0.6f, depth = 0.5f, delay = 0.008f, feedback = 0.f, width = 1.f;

	explicit Chorus(float sampleRate) : rate(sampleRate) { reset(); }
	void set(int index, float value) override
	{
		switch (index)
		{
		case OM_FX_CHORUS_RATE:     hz = 0.05f * std::exp2(value * 6.643856f / 127.f); break;
		case OM_FX_CHORUS_DEPTH:    depth = value / 127.f; break;
		case OM_FX_CHORUS_DELAY:    delay = 0.002f * std::exp2(value * 3.906891f / 127.f); break;
		case OM_FX_CHORUS_FEEDBACK: feedback = value * (0.9f / 127.f); break;
		case OM_FX_CHORUS_WIDTH:    width = value / 127.f; break;
		}
	}
	void reset() override
	{
		std::memset(buf, 0, sizeof(buf));
		pos = 0;
		phase = 0.0;
	}
	static float triangle(double p)
	{
		p -= std::floor(p);
		return (float)(p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
	}
	void process(float *l, float *r) override
	{
		float *io[2] = {l, r};
		const float swing = depth * std::fmin(delay * 0.9f, 0.005f);   // seconds either way
		for (int i = 0; i < OM_FX_BLOCK; i++)
		{
			for (int c = 0; c < 2; c++)
			{
				const float lfo = triangle(phase + c * 0.5 * width);
				const float d = (delay + swing * lfo) * rate;   // samples, >= 1
				const float back = (float)pos - d + SIZE;
				const uint32_t i0 = (uint32_t)back;
				const float f = back - (float)i0;
				const float a = buf[c][i0 % SIZE], b = buf[c][(i0 + 1) % SIZE];
				const float y = a + (b - a) * f;
				buf[c][pos] = io[c][i] + feedback * y;
				io[c][i] = y;
			}
			pos = (pos + 1) % SIZE;
			phase += hz / rate;
			if (phase >= 1.0)
				phase -= 1.0;
		}
	}
};

// Fixed values: the mix all wet, and the parameters no knob reaches.
constexpr Fixed HALL_FIXED[] = {{sfx::reverb2::Reverb2<OmConfig>::rev2_buildup, 1.f}, {sfx::reverb2::Reverb2<OmConfig>::rev2_mix, 1.f}};
constexpr Fixed ROOM_FIXED[] = {{sfx::reverb1::Reverb1<OmConfig>::rev1_mix, 1.f}, {sfx::reverb1::Reverb1<OmConfig>::rev1_gain1, 0.f}};
constexpr Fixed DELAY_FIXED[] = {{sfx::delay::Delay<OmConfig>::dly_mix, 1.f}, {sfx::delay::Delay<OmConfig>::dly_input_channel, 0.f}};
constexpr Fixed PHASER_FIXED[] = {{sfx::phaser::Phaser<OmConfig>::ph_mix, 1.f}, {sfx::phaser::Phaser<OmConfig>::ph_width, 0.f}};
constexpr Fixed FLANGER_FIXED[] = {{sfx::flanger::Flanger<OmConfig>::fl_mix, 1.f}, {sfx::flanger::Flanger<OmConfig>::fl_width, 0.f}};

Slot *makeSlot(uint8_t type, float rate)
{
	Slot *s = nullptr;
	auto sst = [&](auto *made) -> Slot * {
		if (made && !made->fx)
		{
			delete made;
			return nullptr;
		}
		return made;
	};
	switch (type)
	{
	case OM_FX_HALL:    s = sst(new (std::nothrow) SstSlot<sfx::reverb2::Reverb2<OmConfig>>(rate, HALL_FIXED, 2)); break;
	case OM_FX_ROOM:    s = sst(new (std::nothrow) SstSlot<sfx::reverb1::Reverb1<OmConfig>>(rate, ROOM_FIXED, 2)); break;
	case OM_FX_DELAY:   s = sst(new (std::nothrow) SstSlot<sfx::delay::Delay<OmConfig>>(rate, DELAY_FIXED, 2)); break;
	case OM_FX_CHORUS:  s = new (std::nothrow) Chorus(rate); break;
	case OM_FX_PHASER:  s = sst(new (std::nothrow) SstSlot<sfx::phaser::Phaser<OmConfig>>(rate, PHASER_FIXED, 2)); break;
	case OM_FX_FLANGER: s = sst(new (std::nothrow) SstSlot<sfx::flanger::Flanger<OmConfig>>(rate, FLANGER_FIXED, 2)); break;
	}
	return s;
}
} // namespace

struct OmFx
{
	float rate, bpm = 120.f;
	uint8_t type = OM_FX_OFF;
	uint8_t value[OM_FX_PARAMS];
	Slot *slot[OM_FX_TYPES] = {};
};

static float linear(const OmFx *fx, uint16_t p)
{
	const OmParamInfo &info = om_fx_params[p];
	const OmParamMap &m = om_fx_map[p];
	return m.lo + (m.hi - m.lo) * (float)(fx->value[p] - info.min) / (float)(info.max - info.min);
}

// The effect's own value for a byte, sent to its slot (if it has one yet).
static void apply(OmFx *fx, uint16_t p)
{
	Slot *s = fx->slot[typeOf(p)];
	if (!s)
		return;
	const OmParamMap &m = om_fx_map[p];
	const uint8_t v = fx->value[p];
	if (p == OM_FX_DELAY_FEEDBACK || p == OM_FX_DELAY_CROSS)
	{
		// together they would feed back up to 200 %: kept just under endless
		float fb = linear(fx, OM_FX_DELAY_FEEDBACK), cross = linear(fx, OM_FX_DELAY_CROSS);
		const float total = fb + cross;
		if (total > 0.98f)
		{
			fb *= 0.98f / total;
			cross *= 0.98f / total;
		}
		s->set(om_fx_map[OM_FX_DELAY_FEEDBACK].index, fb);
		s->set(om_fx_map[OM_FX_DELAY_CROSS].index, cross);
		return;
	}
	switch (m.kind)
	{
	case OM_MAP_LINEAR:
		s->set(m.index, linear(fx, p));
		break;
	case OM_MAP_SYNC:
		s->set(m.index, std::log2(DIVISIONS[v < sizeof(DIVISIONS) / sizeof(*DIVISIONS) ? v : 0] * 60.f / fx->bpm));
		break;
	case OM_MAP_OWN:
		s->set(p, v);
		break;
	}
}

extern "C" {

OmFx *om_fx_create(float sampleRate)
{
	OmFx *fx = new (std::nothrow) OmFx;
	if (!fx)
		return nullptr;
	fx->rate = sampleRate;
	for (int p = 0; p < OM_FX_PARAMS; p++)
		fx->value[p] = om_fx_params[p].def;
	return fx;
}

void om_fx_destroy(OmFx *fx)
{
	if (!fx)
		return;
	for (Slot *s : fx->slot)
		delete s;
	delete fx;
}

int om_fx_set_type(OmFx *fx, uint8_t type)
{
	if (type >= OM_FX_TYPES)
		return -1;
	fx->type = OM_FX_OFF;
	if (type == OM_FX_OFF)
		return 0;
	if (!fx->slot[type])
	{
		fx->slot[type] = makeSlot(type, fx->rate);
		if (!fx->slot[type])
			return -1;
	}
	for (uint16_t p = FIRST[type]; p < FIRST[type + 1]; p++)
		apply(fx, p);
	fx->slot[type]->reset();
	fx->type = type;
	return 0;
}

uint8_t om_fx_type(const OmFx *fx)
{
	return fx->type;
}

void om_fx_set_param(OmFx *fx, uint16_t param, uint8_t value)
{
	if (param >= OM_FX_PARAMS)
		return;
	const OmParamInfo &info = om_fx_params[param];
	fx->value[param] = value < info.min ? info.min : value > info.max ? info.max : value;
	apply(fx, param);
}

uint8_t om_fx_param(const OmFx *fx, uint16_t param)
{
	return param < OM_FX_PARAMS ? fx->value[param] : 0;
}

void om_fx_set_tempo(OmFx *fx, float bpm)
{
	if (bpm <= 0.f || bpm == fx->bpm)
		return;
	fx->bpm = bpm;
	apply(fx, OM_FX_DELAY_LEFT);
	apply(fx, OM_FX_DELAY_RIGHT);
}

void om_fx_process(OmFx *fx, float *left, float *right, uint32_t frames)
{
	Slot *s = fx->type != OM_FX_OFF ? fx->slot[fx->type] : nullptr;
	if (!s)
	{
		std::memset(left, 0, frames * sizeof(float));
		std::memset(right, 0, frames * sizeof(float));
		return;
	}
	for (uint32_t i = 0; i + OM_FX_BLOCK <= frames; i += OM_FX_BLOCK)
		s->process(left + i, right + i);
}

} // extern "C"
