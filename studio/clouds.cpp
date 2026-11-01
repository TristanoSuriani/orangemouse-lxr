/*
 * clouds.cpp
 *
 * See clouds.h. The send bus's frames are resampled to 32 kHz into a FIFO;
 * every 32 frames (the module's block) Clouds processes them, as 16-bit
 * frames as on the module, and its output is resampled back. The output
 * queue starts PRIME frames of silence ahead, so it never waits for a block.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <cmath>
#include <cstring>
#include <memory>
#include <new>

#include "clouds/dsp/granular_processor.h"
#include "resampler.h"
#include "clouds.h"

namespace
{
const float RATE = 32000.f;   // Clouds' own
constexpr size_t BLOCK = clouds::kMaxBlockSize;
constexpr int PRIME = 48;
constexpr int QUEUE = 256;

inline short toShort(float x)
{
	x *= 32767.f;
	return (short)(x > 32767.f ? 32767.f : x < -32768.f ? -32768.f : x);
}

// zeroed on creation (State()): Init leaves some of the processor to its
// first use, as on the module, whose RAM starts at zero
struct State
{
	clouds::GranularProcessor proc;
	uint8_t large[118784];        // the module's buffers
	uint8_t small[65536 - 128];
	om::Resampler<QUEUE> in;      // the studio's rate, to 32 kHz
	om::Resampler<QUEUE> out;     // and back
	clouds::ShortFrame fifo[BLOCK];
	size_t fifoCount;
};
} // namespace

struct OmClouds
{
	om::SincKernel down, up;   // the studio's rate to 32 kHz, and back
	std::unique_ptr<State> s;
	uint8_t value[OM_CLOUDS_SETTINGS];
};

static float unit(uint8_t v) { return v / 127.f; }

// the processor's parameters from the bytes
static void apply(OmClouds *c)
{
	const uint8_t *v = c->value;
	clouds::GranularProcessor &p = c->s->proc;
	p.set_playback_mode((clouds::PlaybackMode)(v[OM_CLOUDS_MODE] < clouds::PLAYBACK_MODE_LAST ? v[OM_CLOUDS_MODE] : 0));
	clouds::Parameters *q = p.mutable_parameters();
	q->position = unit(v[OM_CLOUDS_POSITION]);
	q->size = unit(v[OM_CLOUDS_SIZE]);
	q->pitch = (float)v[OM_CLOUDS_PITCH] - 24.f;   // semitones
	q->density = unit(v[OM_CLOUDS_DENSITY]);
	q->texture = unit(v[OM_CLOUDS_TEXTURE]);
	q->feedback = unit(v[OM_CLOUDS_FEEDBACK]);
	q->stereo_spread = unit(v[OM_CLOUDS_SPREAD]);
	q->reverb = unit(v[OM_CLOUDS_REVERB]);
	q->freeze = v[OM_CLOUDS_FREEZE] != 0;
	q->dry_wet = 1.f;
	q->trigger = q->gate = false;
}

extern "C" {

OmClouds *om_clouds_create(float sampleRate)
{
	OmClouds *c = new (std::nothrow) OmClouds;
	if (!c)
		return nullptr;
	c->s.reset(new (std::nothrow) State());
	if (!c->s)
	{
		delete c;
		return nullptr;
	}
	c->down.init(sampleRate, RATE);
	c->up.init(RATE, sampleRate);
	std::memset(c->value, 0, sizeof c->value);
	om_clouds_reset(c);
	return c;
}

void om_clouds_destroy(OmClouds *c)
{
	delete c;
}

void om_clouds_set(OmClouds *c, int setting, uint8_t value)
{
	if (setting < 0 || setting >= OM_CLOUDS_SETTINGS)
		return;
	c->value[setting] = value;
	apply(c);
}

void om_clouds_reset(OmClouds *c)
{
	State *s = c->s.get();
	std::memset(s->large, 0, sizeof s->large);
	std::memset(s->small, 0, sizeof s->small);
	s->proc.Init(s->large, sizeof s->large, s->small, sizeof s->small);
	s->proc.set_quality(0);   // 16-bit stereo, as the module starts
	apply(c);
	s->proc.Prepare();
	s->in.reset();
	s->out.reset();
	for (int i = 0; i < PRIME; i++)
		s->out.push(0.f, 0.f);
	s->fifoCount = 0;
}

void om_clouds_process(OmClouds *c, float *left, float *right, uint32_t frames)
{
	State *s = c->s.get();
	for (uint32_t i = 0; i < frames; i++)
		s->in.push(left[i], right[i]);
	while (!s->in.needs())
	{
		float l, r;
		s->in.pull(c->down, l, r);
		s->fifo[s->fifoCount++] = {toShort(l), toShort(r)};
		if (s->fifoCount == BLOCK)
		{
			clouds::ShortFrame o[BLOCK];
			s->proc.Prepare();
			s->proc.Process(s->fifo, o, BLOCK);
			for (size_t k = 0; k < BLOCK; k++)
				s->out.push(o[k].l / 32768.f, o[k].r / 32768.f);
			s->fifoCount = 0;
		}
	}
	s->in.compact();
	for (uint32_t i = 0; i < frames; i++)
	{
		if (s->out.needs())   // cannot happen once primed; silence rather than read past the queue
			left[i] = right[i] = 0.f;
		else
			s->out.pull(c->up, left[i], right[i]);
	}
	s->out.compact();
}

} // extern "C"
