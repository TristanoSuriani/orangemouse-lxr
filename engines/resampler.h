/*
 * resampler.h
 *
 * The windowed-sinc resampler the Mutable Instruments engines share: each
 * runs at its module's own rate (Plaits 47872 Hz, Rings 48 kHz, Clouds
 * 32 kHz), since their tuning, envelopes and filters are timed for it
 * (PLAN.md 7.6), and this turns that rate into the studio's, or the studio's
 * into theirs.
 *
 * A SincKernel is the filter, a Blackman-Harris windowed sinc of 2 * HALF
 * taps at PHASES fractional positions, cut just below the lower of the two
 * rates' Nyquist frequencies. A Resampler is a stereo queue of frames at the
 * source rate: push frames while needs() says so, then pull() one frame at
 * the destination rate, and compact() once a block is done. It starts on
 * HALF - 1 frames of silence: HALF - 1 frames of delay at the source rate.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef OM_RESAMPLER_H_
#define OM_RESAMPLER_H_

#include <cmath>
#include <cstring>

namespace om
{

constexpr int HALF = 16;      // the filter's taps on each side
constexpr int PHASES = 256;   // its fractional positions

struct SincKernel
{
	double step;                            // source frames per destination frame
	float h[PHASES + 1][2 * HALF];

	// rates as floats, as the engines get theirs (the ratio too: Plaits' golden renders keep it)
	void init(float from, float to)
	{
		step = from / to;
		// each position sums to 1
		const double cut = 0.95 * std::fmin(1.0, 1.0 / step);
		for (int ph = 0; ph <= PHASES; ph++)
		{
			double sum = 0.0, k[2 * HALF];
			for (int t = 0; t < 2 * HALF; t++)
			{
				const double x = (t - HALF + 1) - (double)ph / PHASES;   // taps from -15 to 16, the position in between
				const double sinc = x == 0.0 ? 1.0 : std::sin(M_PI * cut * x) / (M_PI * cut * x);
				const double w = 2.0 * M_PI * (x + HALF) / (2.0 * HALF);
				const double win = 0.35875 - 0.48829 * std::cos(w) + 0.14128 * std::cos(2.0 * w) - 0.01168 * std::cos(3.0 * w);
				k[t] = sinc * win;
				sum += k[t];
			}
			for (int t = 0; t < 2 * HALF; t++)
				h[ph][t] = (float)(k[t] / sum);
		}
	}
};

// CAP: the queue's frames; a block's pushes and pulls must fit in it
template <int CAP>
struct Resampler
{
	float queue[2][CAP];    // frames at the source rate, from HALF - 1 before the next output
	int queued = HALF - 1;  // the first: silence
	double pos = HALF - 1;  // where in the queue the next output frame falls

	void reset()
	{
		std::memset(queue, 0, sizeof queue);
		queued = HALF - 1;
		pos = HALF - 1;
	}

	// another source frame is needed before the next pull
	bool needs() const { return (int)pos + HALF >= queued; }

	// where the next n source frames go (then call pushed(n))
	float *left() { return queue[0] + queued; }
	float *right() { return queue[1] + queued; }
	void pushed(int n) { queued += n; }

	void push(float l, float r)
	{
		queue[0][queued] = l;
		queue[1][queued] = r;
		queued++;
	}

	void pull(const SincKernel &k, float &outL, float &outR)
	{
		const int at = (int)pos;
		const double where = (pos - at) * PHASES;
		const int ph = (int)where;
		const float frac = (float)(where - ph);
		const float *a = k.h[ph], *b = k.h[ph + 1];
		const float *l = queue[0] + at - HALF + 1, *r = queue[1] + at - HALF + 1;
		float sl = 0.f, sr = 0.f;
		for (int t = 0; t < 2 * HALF; t++)
		{
			const float h = a[t] + (b[t] - a[t]) * frac;
			sl += l[t] * h;
			sr += r[t] * h;
		}
		outL = sl;
		outR = sr;
		pos += k.step;
	}

	// keeps HALF - 1 frames before the next output's
	void compact()
	{
		const int drop = (int)pos - (HALF - 1);
		queued -= drop;
		pos -= drop;
		std::memmove(queue[0], queue[0] + drop, queued * sizeof(float));
		std::memmove(queue[1], queue[1] + drop, queued * sizeof(float));
	}
};

} // namespace om

#endif /* OM_RESAMPLER_H_ */
