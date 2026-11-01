/*
 * shaper.cpp
 *
 * See shaper.h. sst-waveshapers works on four lanes at once (SSE, through
 * simde): the left and right channels are lanes 0 and 1.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <cmath>
#include <new>

#include "sst/basic-blocks/simd/setup.h"
#include "sst/waveshapers.h"

#include "shaper.h"

namespace
{
namespace sw = sst::waveshapers;
using WT = sw::WaveshaperType;

const WT TYPES[OM_SHAPE_TYPES] = {
	WT::wst_none, WT::wst_soft, WT::wst_zamsat, WT::wst_ojd, WT::wst_hard, WT::wst_asym, WT::wst_singlefold,
	WT::wst_dualfold, WT::wst_westfold, WT::wst_fwrectify, WT::wst_fuzz, WT::wst_fuzzheavy,
	WT::wst_cheby2, WT::wst_cheby3, WT::wst_sine, WT::wst_digital,
};
constexpr float MAX_GAIN = 31.6228f;   // +30 dB

// above full scale, a soft knee up to 2: Surge's asymmetric shape grows
// without bound on its negative side (22 times its input at the table's end).
// Rational, not tanh: no libm call, so native and WebAssembly agree exactly.
inline float guard(float x)
{
	const float a = std::fabs(x) - 1.f;
	return a <= 0.f ? x : std::copysign(1.f + a / (1.f + a), x);
}
} // namespace

struct OmShaper
{
	uint8_t type = OM_SHAPE_OFF;
	float drive = 1.f;      // where the gain into the shape goes
	float nowDrive = 1.f;   // and where it is
	sw::QuadWaveshaperPtr shape = nullptr;
	sw::QuadWaveshaperState state;
};

static void reset(OmShaper *s)
{
	float R[sw::n_waveshaper_registers];
	sw::initializeWaveshaperRegister(TYPES[s->type], R);
	for (int i = 0; i < sw::n_waveshaper_registers; i++)
		s->state.R[i] = SIMD_MM(set1_ps)(R[i]);
	s->state.init = SIMD_MM(cmpeq_ps)(SIMD_MM(setzero_ps)(), SIMD_MM(setzero_ps)());   // all lanes: first sample
}

extern "C" {

OmShaper *om_shaper_create(void)
{
	return new (std::nothrow) OmShaper;
}

void om_shaper_destroy(OmShaper *s)
{
	delete s;
}

void om_shaper_set(OmShaper *s, uint8_t type, float drive)
{
	if (type >= OM_SHAPE_TYPES)
		type = OM_SHAPE_OFF;
	s->drive = drive < 1.f ? 1.f : drive > MAX_GAIN ? MAX_GAIN : drive;
	if (type != s->type)
	{
		s->type = type;
		s->shape = type == OM_SHAPE_OFF ? nullptr : sw::GetQuadWaveshaper(TYPES[type]);
		if (!s->shape)
			s->type = OM_SHAPE_OFF;
		else
			reset(s);
		s->nowDrive = s->drive;   // a new shape starts where it is set
	}
}

void om_shaper_process(OmShaper *s, float *left, float *right, uint32_t frames)
{
	if (s->type == OM_SHAPE_OFF || frames == 0)
		return;
	const float step = (s->drive - s->nowDrive) / frames;
	float out alignas(16)[4];
	for (uint32_t i = 0; i < frames; i++)
	{
		const float d = s->nowDrive + step * (i + 1);
		const auto in = SIMD_MM(set_ps)(0.f, 0.f, right[i], left[i]);
		SIMD_MM(store_ps)(out, s->shape(&s->state, in, SIMD_MM(set1_ps)(d)));
		const float trim = 1.f / std::sqrt(d);   // +30 dB of drive comes out about 15 dB down
		left[i] = guard(out[0] * trim);
		right[i] = guard(out[1] * trim);
	}
	s->nowDrive = s->drive;
}

} // extern "C"
