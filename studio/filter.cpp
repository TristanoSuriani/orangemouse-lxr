/*
 * filter.cpp
 *
 * See filter.h. Each type is one of sst-filters' models with one of its
 * configurations, found by name (its own toString), so a change in how the
 * library spells its configurations fails loudly here (om_filter_set turns
 * the filter off) and in tests/host/studio_test.c, not silently.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <cmath>
#include <cstring>
#include <new>
#include <string>

#include "sst/basic-blocks/simd/setup.h"
#include "sst/filters++.h"

#include "filter.h"

namespace
{
namespace sfpp = sst::filtersplusplus;
constexpr size_t BLOCK = 16;

struct Kind
{
	sfpp::FilterModel model;
	const char *config;
};

const Kind KINDS[OM_FILTER_TYPES] = {
	{sfpp::FilterModel::None, ""},
	{sfpp::FilterModel::VemberClassic, "Lowpass 12 dB Standard"},
	{sfpp::FilterModel::VemberClassic, "Lowpass 24 dB Standard"},
	{sfpp::FilterModel::VemberLadder, "Lowpass 24 dB"},
	{sfpp::FilterModel::VintageLadder, "Lowpass Runge-Kutta Compensated"},
	{sfpp::FilterModel::OBXD_4Pole, "Lowpass 24 dB"},
	{sfpp::FilterModel::K35, "Lowpass Moderate"},
	{sfpp::FilterModel::DiodeLadder, "Lowpass 24 dB"},
	{sfpp::FilterModel::VemberClassic, "Highpass 12 dB Standard"},
	{sfpp::FilterModel::VemberClassic, "Highpass 24 dB Standard"},
	{sfpp::FilterModel::K35, "Highpass Moderate"},
	{sfpp::FilterModel::VemberClassic, "Bandpass 12 dB Standard"},
	{sfpp::FilterModel::VemberClassic, "Notch 12 dB Standard"},
	{sfpp::FilterModel::OBXD_2Pole, "Bandpass Standard"},
};
} // namespace

struct OmFilter
{
	float rate;
	uint8_t type = OM_FILTER_OFF;
	float cutoff = 70.f, resonance = 0.f;   // where they go
	float nowCutoff = 70.f, nowResonance = 0.f;
	sfpp::Filter filter;
};

static bool prepare(OmFilter *f, uint8_t type)
{
	const Kind &k = KINDS[type];
	for (const auto &c : sfpp::Filter::availableModelConfigurations(k.model))
		if (c.toString() == k.config)
		{
			f->filter = sfpp::Filter();
			f->filter.setFilterModel(k.model);
			f->filter.setModelConfiguration(c);
			f->filter.setSampleRateAndBlockSize(f->rate, BLOCK);
			f->filter.setStereo();
			if (!f->filter.prepareInstance())
				return false;
			f->filter.reset();
			return true;
		}
	return false;
}

extern "C" {

OmFilter *om_filter_create(float sampleRate)
{
	OmFilter *f = new (std::nothrow) OmFilter;
	if (f)
		f->rate = sampleRate;
	return f;
}

void om_filter_destroy(OmFilter *f)
{
	delete f;
}

void om_filter_set(OmFilter *f, uint8_t type, float cutoff, float resonance)
{
	if (type >= OM_FILTER_TYPES)
		type = OM_FILTER_OFF;
	if (type != f->type)
	{
		f->type = type != OM_FILTER_OFF && prepare(f, type) ? type : OM_FILTER_OFF;
		f->nowCutoff = cutoff;   // a new filter starts where it is set
		f->nowResonance = resonance;
	}
	f->cutoff = cutoff;
	f->resonance = resonance;
}

void om_filter_process(OmFilter *f, float *left, float *right, uint32_t frames)
{
	if (f->type == OM_FILTER_OFF)
		return;
	for (uint32_t b = 0; b + BLOCK <= frames; b += BLOCK)
	{
		// a quarter of the way there each block: about 1.5 ms
		f->nowCutoff += (f->cutoff - f->nowCutoff) * 0.25f;
		f->nowResonance += (f->resonance - f->nowResonance) * 0.25f;
		for (int v = 0; v < 2; v++)
			f->filter.makeCoefficients(v, f->nowCutoff, f->nowResonance);
		f->filter.prepareBlock();
		for (size_t i = b; i < b + BLOCK; i++)
			f->filter.processStereoSample(left[i], right[i], left[i], right[i]);
		f->filter.concludeBlock();
	}
}

} // extern "C"
