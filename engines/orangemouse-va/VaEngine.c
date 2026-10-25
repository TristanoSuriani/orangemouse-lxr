/*
 * VaEngine.c
 *
 * The virtual analogue engine of the VA tracks. Signal flow of a note:
 *
 *   osc 1 --+ (unison: up to 7 detuned copies of both,
 *   osc 2 --+  spread across the stereo field)
 *   sub   --+
 *   noise --+--> drive --> filter (L, R) --> amp envelope --> slot sum
 *
 * and of a slot: sum of its notes --> halfband decimators --> high-pass --> out.
 *
 * Quality measures:
 *  - Every note runs at VA_OS (4) times the sample rate. Aliasing that the
 *    oscillators and the filter's saturation still produce lands mostly above
 *    20 kHz in the oversampled band, where the decimators remove it.
 *  - The oscillators are band-limited with windowed-sinc BLEP (steps) and
 *    BLAMP (slope changes) corrections placed at the exact sub-sample time of
 *    each discontinuity: saw and pulse edges, triangle corners and hard-sync
 *    resets of any waveform. Together with the oversampling, aliasing stays
 *    100 dB down.
 *  - The ladder is a zero-delay-feedback model with a saturating stage in
 *    every pole and in the feedback path, solved per sample with the
 *    nonlinearities linearised around the previous state (the "cheap
 *    non-linear ZDF" method). It self-oscillates with a stable, saturated
 *    amplitude. The 12 dB filter is a trapezoidal state-variable filter
 *    (low-, band- and high-pass) with a saturating input and band-pass state.
 *  - Decimation: two Kaiser-windowed halfband FIR stages (4x -> 2x -> 1x),
 *    passband flat to 20 kHz within 0.001 dB, at least 100 dB of rejection
 *    of everything that would fold below 20 kHz.
 *  - Envelopes and modulation are computed at the sample rate and
 *    interpolated across the oversampled samples: no zipper noise.
 *  - Analogue variance: each oscillator and filter gets a small fixed offset
 *    per note and a slow random drift, scaled by SLOP.
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */
#include "Soundengine/DSPAudio/VaEngine.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Parameters.h"
#include "menu/menu.h"
#include <math.h>
#include <string.h>

#define VA_OS          4                       // oversampling factor
#define VA_FS          ((float)REAL_FS)
#define VA_FS_OS       ((float)REAL_FS * VA_OS)
#define VA_BLOCK       OUTPUT_DMA_SIZE
#define VA_MAX_NOTES   6                       // polyphony of a slot
#define VA_MAX_UNISON  7
#define VA_OUT_SCALE   16000.f                 // full-scale note level in the int16 output

#define VA_PI          3.14159265358979f
#define VA_TWO_PI      6.28318530717959f

// ---- small math helpers ----------------------------------------------------

static inline float va_clampf(float x, float lo, float hi)
{
	return x < lo ? lo : (x > hi ? hi : x);
}

// 2^x for the pitch and cutoff domains (|x| < ~20)
static inline float va_exp2(float x)
{
	return exp2f(x);
}

// tan(x) for 0 <= x < 0.5 (cutoff up to ~28 kHz at 4x): [3/2] Pade, error < 1e-6
static inline float va_tan(float x)
{
	const float x2 = x * x;
	return x * (15.f - x2) / (15.f - 6.f * x2);
}

// tanh(x)/x: [6/6] Pade of tanh in x^2, accurate to ~1e-7 for |x| < 4.9;
// beyond that tanh(x) = +-1 to float precision, so 1/|x|.
static inline float va_tanhXdX(float x)
{
	const float a = x * x;
	if (a > 24.f)
		return 1.f / fabsf(x);
	return (135135.f + a * (17325.f + a * (378.f + a))) /
	       (135135.f + a * (62370.f + a * (3150.f + a * 28.f)));
}

static inline float va_tanh(float x)
{
	return x * va_tanhXdX(x);
}

static inline uint32_t va_rand(uint32_t *state)
{
	uint32_t x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

// uniform in [-1, 1)
static inline float va_randf(uint32_t *state)
{
	return (float)(int32_t)va_rand(state) * (1.f / 2147483648.f);
}

// modified Bessel function of the first kind, order 0 (Kaiser windows)
static double va_besselI0(double x)
{
	double sum = 1.0, term = 1.0;
	for (int k = 1; k < 50; k++)
	{
		term *= (x / (2.0 * k)) * (x / (2.0 * k));
		sum += term;
		if (term < 1e-12 * sum)
			break;
	}
	return sum;
}

// ---- band-limited oscillators ------------------------------------------------
//
// Each discontinuity of a waveform, a step (saw and pulse edges, sync resets)
// or a change of slope (triangle corners, sync resets), is replaced by its
// band-limited version: the residual of an integrated windowed sinc (BLEP) or
// of its integral (BLAMP), placed at the exact sub-sample time of the event.
//
// The kernel is a sinc with its cutoff at half the oversampled rate, Kaiser
// windowed (beta 10) over +-VA_BLEP_HALF samples: flat to ~50 kHz, more than
// 100 dB down from 0.71 times the oversampled rate. At 4x oversampling only
// content near the oversampled sample rate (and its multiples) can fold back
// below 20 kHz, and that is where the kernel is closed; the decimators remove
// whatever folds elsewhere. The corrections reach VA_BLEP_HALF samples into
// the past, so the output of every oscillator is delayed by that much (90 us).
//
// The phase is a double: in float, the timing of the corrections (the
// distance past the discontinuity divided by the increment) would jitter by
// ~1e-5 samples and raise a noise floor around -100 dB.

#define VA_BLEP_HALF  16                      // kernel half length (oversampled samples)
#define VA_BLEP_RES   512                     // table points per sample
#define VA_BLEP_LEN   (2 * VA_BLEP_HALF * VA_BLEP_RES + 1)
#define VA_OSC_BUF    64                      // power of two, > 2 * VA_BLEP_HALF
#define VA_OSC_MASK   (VA_OSC_BUF - 1)

static float va_blepTable[VA_BLEP_LEN];       // band-limited unit step, tau = -HALF .. HALF
static float va_blampTable[VA_BLEP_LEN];      // slope-change residual (band-limited minus naive ramp)

typedef struct
{
	double phase;              // 0..1
	float buf[VA_OSC_BUF];     // the output, VA_BLEP_HALF samples ahead of what is read
	uint8_t pos;               // where the current sample goes
} VaOsc;

static void va_blepInit(void)
{
	// integrate h(tau) = sinc(tau) * kaiser(tau / HALF) on a grid 8 times finer
	// than the table, with the trapezoid rule
	const int fine = 8;
	const int steps = 2 * VA_BLEP_HALF * VA_BLEP_RES * fine;
	const double beta = 10.0;
	const double i0beta = va_besselI0(beta);
	double step = 0.0, ramp = 0.0, prevH = 0.0, prevR = 0.0;
	static double stepTable[VA_BLEP_LEN];
	for (int i = 0; i <= steps; i++)
	{
		const double tau = -VA_BLEP_HALF + (double)i / (VA_BLEP_RES * fine);
		const double r = tau / VA_BLEP_HALF;
		const double w = va_besselI0(beta * sqrt(fmax(0.0, 1.0 - r * r))) / i0beta;
		const double sinc = fabs(tau) < 1e-12 ? 1.0 : sin(M_PI * tau) / (M_PI * tau);
		const double h = sinc * w;
		if (i > 0)
			step += 0.5 * (h + prevH) / (VA_BLEP_RES * fine);
		prevH = h;
		if (i % fine == 0)
			stepTable[i / fine] = step;
	}
	// normalise to a unit step. The step table holds the band-limited step
	// itself: the residual jumps at tau = 0 and must not be interpolated
	// across, so the naive step is subtracted at lookup (va_blep).
	const double total = stepTable[VA_BLEP_LEN - 1];
	for (int i = 0; i < VA_BLEP_LEN; i++)
	{
		const double tau = -VA_BLEP_HALF + (double)i / VA_BLEP_RES;
		const double residual = stepTable[i] / total - (tau >= 0.0 ? 1.0 : 0.0);
		va_blepTable[i] = (float)(stepTable[i] / total);
		if (i > 0)
			ramp += 0.5 * (residual + prevR) / VA_BLEP_RES;
		prevR = residual;
		va_blampTable[i] = (float)ramp;
	}
	// the ramp residual is 0 at both ends; remove the integration's rounding drift
	const float drift = va_blampTable[VA_BLEP_LEN - 1];
	for (int i = 0; i < VA_BLEP_LEN; i++)
		va_blampTable[i] -= drift * (float)i / (VA_BLEP_LEN - 1);
}

static inline float va_tableAt(const float *table, float tau)
{
	const float x = (tau + VA_BLEP_HALF) * VA_BLEP_RES;
	if (x <= 0.f)
		return table[0];
	if (x >= VA_BLEP_LEN - 1)
		return table[VA_BLEP_LEN - 1];
	const int i = (int)x;
	const float f = x - (float)i;
	return table[i] + f * (table[i + 1] - table[i]);
}

// a step of `height` t samples (0 < t <= 1) before the current sample
static inline void va_blep(VaOsc *o, float t, float height)
{
	// samples before the event (j + t < 0, i.e. j < 0 for t <= 1) get the
	// band-limited step; from the event on, minus the naive step
	for (int j = -VA_BLEP_HALF; j < 0; j++)
		o->buf[(o->pos + j) & VA_OSC_MASK] += height * va_tableAt(va_blepTable, (float)j + t);
	for (int j = 0; j < VA_BLEP_HALF; j++)
		o->buf[(o->pos + j) & VA_OSC_MASK] += height * (va_tableAt(va_blepTable, (float)j + t) - 1.f);
}

// a change of slope (per sample) t samples before the current sample
static inline void va_blamp(VaOsc *o, float t, float slopeChange)
{
	for (int j = -VA_BLEP_HALF; j < VA_BLEP_HALF; j++)
		o->buf[(o->pos + j) & VA_OSC_MASK] += slopeChange * va_tableAt(va_blampTable, (float)j + t);
}

// the current sample's naive value in; the delayed band-limited sample out
static inline float va_oscOut(VaOsc *o, float naive)
{
	o->buf[o->pos] += naive;
	const int outPos = (o->pos - VA_BLEP_HALF) & VA_OSC_MASK;
	const float out = o->buf[outPos];
	o->buf[outPos] = 0.f;
	o->pos = (uint8_t)((o->pos + 1) & VA_OSC_MASK);
	return out;
}

static inline void va_oscReset(VaOsc *o, double phase)
{
	memset(o->buf, 0, sizeof(o->buf));
	o->phase = phase;
}

// sin(2 pi p) for p in [0, 1): odd polynomial on a quarter cycle, error < 2e-7
static inline float va_sin2pi(float p)
{
	float x = p - 0.5f;               // sin(2 pi p) = -sin(2 pi x), x in [-0.5, 0.5)
	if (x > 0.25f)
		x = 0.5f - x;
	else if (x < -0.25f)
		x = -0.5f - x;
	const float y = VA_TWO_PI * x;    // |y| <= pi/2
	const float y2 = y * y;
	return -y * (1.f + y2 * (-1.f / 6.f + y2 * (1.f / 120.f + y2 * (-1.f / 5040.f + y2 * (1.f / 362880.f - y2 * (1.f / 39916800.f))))));
}

// naive waveform value at phase p (0..1)
static inline float va_waveValue(uint8_t wave, float p, float pw)
{
	switch (wave)
	{
	default:
	case VA_WAVE_SAW:    return 2.f * p - 1.f;
	case VA_WAVE_SQUARE: return p < pw ? 1.f : -1.f;
	case VA_WAVE_TRI:    return p < 0.5f ? 4.f * p - 1.f : 3.f - 4.f * p;
	case VA_WAVE_SINE:   return va_sin2pi(p);
	}
}

// its slope per cycle
static inline float va_waveSlope(uint8_t wave, float p)
{
	switch (wave)
	{
	default:
	case VA_WAVE_SAW:    return 2.f;
	case VA_WAVE_SQUARE: return 0.f;
	case VA_WAVE_TRI:    return p < 0.5f ? 4.f : -4.f;
	case VA_WAVE_SINE:   return VA_TWO_PI * va_sin2pi(p + 0.25f >= 1.f ? p - 0.75f : p + 0.25f);
	}
}

// The waveform's own discontinuities while the phase moves from a to b
// (b may exceed 1 once, which is the wrap). tEnd: time (samples before the
// current sample) at which the movement ends; dt: phase increment per sample.
static inline void va_naturalEvents(VaOsc *o, uint8_t wave, double a, double b, double dt, double tEnd, float pw)
{
	const double invDt = 1.0 / dt;
	switch (wave)
	{
	case VA_WAVE_SAW:
		if (b >= 1.0)
			va_blep(o, (float)(tEnd + (b - 1.0) * invDt), -2.f);
		break;
	case VA_WAVE_SQUARE:
		if (a < pw && b >= pw)
			va_blep(o, (float)(tEnd + (b - pw) * invDt), -2.f);
		if (b >= 1.0)
		{
			va_blep(o, (float)(tEnd + (b - 1.0) * invDt), 2.f);
			if (b - 1.0 >= pw)  // fell again after the wrap
				va_blep(o, (float)(tEnd + (b - 1.0 - pw) * invDt), -2.f);
		}
		break;
	case VA_WAVE_TRI:
		if (a < 0.5 && b >= 0.5)
			va_blamp(o, (float)(tEnd + (b - 0.5) * invDt), (float)(-8.0 * dt));
		if (b >= 1.0)
		{
			va_blamp(o, (float)(tEnd + (b - 1.0) * invDt), (float)(8.0 * dt));
			if (b - 1.0 >= 0.5)
				va_blamp(o, (float)(tEnd + (b - 1.5) * invDt), (float)(-8.0 * dt));
		}
		break;
	default:
		break;
	}
}

// One sample of a free-running oscillator.
static inline float va_oscStep(VaOsc *o, uint8_t wave, double dt, float pw)
{
	const double a = o->phase;
	double b = a + dt;
	va_naturalEvents(o, wave, a, b, dt, 0.0, pw);
	if (b >= 1.0)
		b -= 1.0;
	o->phase = b;
	return va_oscOut(o, va_waveValue(wave, (float)b, pw));
}

// One sample of an oscillator hard-synced to a master that wrapped `t`
// samples before the current sample (t < 0: no wrap this sample).
static inline float va_oscStepSync(VaOsc *o, uint8_t wave, double dt, float pw, double t)
{
	if (t < 0.0)
		return va_oscStep(o, wave, dt, pw);

	// up to the reset
	const double a = o->phase;
	double b = a + dt * (1.0 - t);
	va_naturalEvents(o, wave, a, b, dt, t, pw);
	if (b >= 1.0)
		b -= 1.0;
	// the reset: a step and a slope change of whatever the waveform had there
	va_blep(o, (float)t, va_waveValue(wave, 0.f, pw) - va_waveValue(wave, (float)b, pw));
	va_blamp(o, (float)t, (va_waveSlope(wave, 0.f) - va_waveSlope(wave, (float)b)) * (float)dt);
	// after the reset
	double ph = dt * t;
	va_naturalEvents(o, wave, 0.0, ph, dt, 0.0, pw);
	if (ph >= 1.0)
		ph -= 1.0;
	o->phase = ph;
	return va_oscOut(o, va_waveValue(wave, (float)ph, pw));
}

// ---- noise -------------------------------------------------------------------
// White noise; pink noise from a bank of one-pole low-passes whose corners are
// spaced 1.5 octaves apart with gains falling as 1/sqrt(f): -3 dB/octave from
// ~10 Hz to the top of the oversampled band; brown from a leaky integrator.

#define VA_PINK_POLES 11

static float va_pinkCoef[VA_PINK_POLES];
static float va_pinkGain[VA_PINK_POLES];
static float va_brownCoef;

typedef struct
{
	uint32_t rng;
	float pink[VA_PINK_POLES];
	float brown;
} VaNoise;

static void va_noiseInit(void)
{
	float f = 10.f;
	float norm = 0.f;
	for (int i = 0; i < VA_PINK_POLES; i++)
	{
		va_pinkCoef[i] = expf(-VA_TWO_PI * f / VA_FS_OS);
		va_pinkGain[i] = 1.f / sqrtf(f);
		norm += va_pinkGain[i] * va_pinkGain[i] * f;
		f *= 2.8284271f;   // 1.5 octaves
	}
	// unit-ish power over the audio band
	const float g = 1.f / sqrtf(norm / 20000.f) * 0.5f;
	for (int i = 0; i < VA_PINK_POLES; i++)
		va_pinkGain[i] *= g;
	va_brownCoef = expf(-VA_TWO_PI * 15.f / VA_FS_OS);
}

static inline float va_noiseStep(VaNoise *n, uint8_t type)
{
	// sum of two uniforms: triangular distribution, closer to analogue noise;
	// scaled to about the level of a saw (std. deviation ~0.8)
	const float w = va_randf(&n->rng) + va_randf(&n->rng);
	switch (type)
	{
	default:
	case VA_NOISE_WHITE:
		return w;
	case VA_NOISE_PINK:
	{
		float sum = 0.f;
		for (int i = 0; i < VA_PINK_POLES; i++)
		{
			n->pink[i] = va_pinkCoef[i] * n->pink[i] + (1.f - va_pinkCoef[i]) * w * va_pinkGain[i];
			sum += n->pink[i];
		}
		return sum * 12.f;
	}
	case VA_NOISE_BROWN:
		n->brown = va_brownCoef * n->brown + (1.f - va_brownCoef) * w;
		return n->brown * 60.f;
	}
}

// ---- filters ----------------------------------------------------------------------

typedef struct
{
	float s[4];     // integrator states
	float zi;       // the previous input (for the half-sample input estimate)
} VaLadder;

// One sample of the nonlinear ladder. f: tan(pi fc / fs), r: feedback 0..~4.4.
static inline float va_ladderStep(VaLadder *l, float in, float f, float r)
{
	// the input half a sample back, where the states are: estimates for the
	// nonlinearities line up in time
	const float ih = 0.5f * (in + l->zi);
	l->zi = in;

	// nonlinear gains tanh(x)/x at the current state
	const float t0 = va_tanhXdX(ih - r * l->s[3]);
	const float t1 = va_tanhXdX(l->s[0]);
	const float t2 = va_tanhXdX(l->s[1]);
	const float t3 = va_tanhXdX(l->s[2]);
	const float t4 = va_tanhXdX(l->s[3]);

	// each stage's solution denominator
	const float g0 = 1.f / (1.f + f * t1);
	const float g1 = 1.f / (1.f + f * t2);
	const float g2 = 1.f / (1.f + f * t3);
	const float g3 = 1.f / (1.f + f * t4);

	// the feedback loop solved for the last stage's output
	const float f3 = f * t3 * g3;
	const float f2 = f * t2 * g2 * f3;
	const float f1 = f * t1 * g1 * f2;
	const float f0 = f * t0 * g0 * f1;
	const float y3 = (g3 * l->s[3] + f3 * g2 * l->s[2] + f2 * g1 * l->s[1] + f1 * g0 * l->s[0] + f0 * in) /
	                 (1.f + r * f0);

	// the other stages, then the trapezoidal state updates
	const float xx = t0 * (in - r * y3);
	const float y0 = t1 * g0 * (l->s[0] + f * xx);
	const float y1 = t2 * g1 * (l->s[1] + f * y0);
	const float y2 = t3 * g2 * (l->s[2] + f * y1);
	l->s[0] += 2.f * f * (xx - y0);
	l->s[1] += 2.f * f * (y0 - y1);
	l->s[2] += 2.f * f * (y1 - y2);
	l->s[3] += 2.f * f * (y2 - t4 * y3);
	return y3;
}

typedef struct
{
	float ic1, ic2;  // integrator states
	float x1, r1;    // the input saturator's previous input and sqrt(1 + x1^2)
} VaSvf;

// The SVF's input saturation: the algebraic sigmoid x / sqrt(1 + x^2), with
// first-order antiderivative anti-aliasing. Its antiderivative is
// sqrt(1 + x^2), so the ADAA output (F(x) - F(x1)) / (x - x1) has the closed
// form (x + x1) / (sqrt(1 + x^2) + sqrt(1 + x1^2)): no division by a small
// difference, one square root per sample, and the aliasing of the saturation
// some 20 dB lower than evaluating it directly.
static inline float va_saturateAdaa(VaSvf *s, float x)
{
	const float r = sqrtf(1.f + x * x);
	const float y = (x + s->x1) / (r + s->r1);
	s->x1 = x;
	s->r1 = r;
	return y;
}

// One sample of the state-variable filter. g: tan(pi fc / fs), k: damping
// (2 = no resonance). Returns the selected output.
static inline float va_svfStep(VaSvf *s, float in, float g, float k, uint8_t type)
{
	// the band-pass state saturates softly, as an OTA would: at normal levels
	// it is linear, at high resonance it stays bounded and grows a little grit
	const float ic1 = va_tanh(s->ic1 * 0.25f) * 4.f;
	const float a1 = 1.f / (1.f + g * (g + k));
	const float v3 = in - s->ic2;
	const float v1 = a1 * ic1 + g * a1 * v3;
	const float v2 = s->ic2 + g * v1;
	s->ic1 = 2.f * v1 - ic1;
	s->ic2 = 2.f * v2 - s->ic2;
	switch (type)
	{
	case VA_FILTER_BP12: return k * v1;
	case VA_FILTER_HP12: return in - k * v1 - v2;
	default:             return v2;
	}
}

// ---- halfband decimators ---------------------------------------------------------
// Kaiser-windowed (beta 10) halfband FIRs. The taps at even offsets from the
// centre are 0, the centre is 1/2: only the odd offsets are stored.

#define VA_HB1_SIDE  8      // 31 taps, 4x -> 2x: transition 24.1 .. 64.1 kHz
#define VA_HB2_SIDE  36     // 143 taps, 2x -> 1x: transition 20 .. 24.1 kHz
#define VA_HB1_LEN   (4 * VA_HB1_SIDE - 1)
#define VA_HB2_LEN   (4 * VA_HB2_SIDE - 1)

static float va_hb1[VA_HB1_SIDE];
static float va_hb2[VA_HB2_SIDE];

typedef struct
{
	float buf[2 * VA_HB2_LEN];  // twice the length: the window never wraps
	int pos;
} VaHalfband;


static void va_designHalfband(float *taps, int side)
{
	const double beta = 10.0;
	const int half = 2 * side - 1;     // the outermost tap's offset
	const double i0beta = va_besselI0(beta);
	double sum = 0.5;
	for (int i = 0; i < side; i++)
	{
		const int k = 2 * i + 1;
		const double sinc = sin(M_PI * k / 2.0) / (M_PI * k);
		const double r = (double)k / (half + 1);
		const double w = va_besselI0(beta * sqrt(1.0 - r * r)) / i0beta;
		taps[i] = (float)(sinc * w);
		sum += 2.0 * taps[i];
	}
	for (int i = 0; i < side; i++)   // unity gain at DC
		taps[i] = (float)(taps[i] / sum);
}

// Feeds two samples, returns one.
static inline float va_halfband(VaHalfband *h, const float *taps, int side, float x0, float x1)
{
	const int len = 4 * side - 1;
	h->buf[h->pos] = h->buf[h->pos + len] = x0;
	h->pos = h->pos + 1 == len ? 0 : h->pos + 1;
	h->buf[h->pos] = h->buf[h->pos + len] = x1;
	h->pos = h->pos + 1 == len ? 0 : h->pos + 1;

	// the window is buf[pos .. pos+len-1], oldest first
	const float *w = &h->buf[h->pos];
	const int centre = 2 * side - 1;
	float acc = 0.5f * w[centre];
	for (int i = 0; i < side; i++)
	{
		const int k = 2 * i + 1;
		acc += taps[i] * (w[centre - k] + w[centre + k]);
	}
	return acc;
}

// ---- envelopes -----------------------------------------------------------------------

enum { VA_ENV_IDLE, VA_ENV_ATTACK, VA_ENV_DECAY, VA_ENV_RELEASE };

typedef struct
{
	float level;
	uint8_t stage;
} VaEnv;

typedef struct
{
	float attack, decay, sustain, release;   // per-sample coefficients, sustain level
} VaEnvCoefs;

// An RC charge towards 1.3, cut at 1: the analogue attack curve.
#define VA_ATTACK_TARGET 1.3f

// time 0..127 -> seconds: 0.5 ms .. 20 s, exponential
static inline float va_envTime(uint8_t v)
{
	return 0.0005f * va_exp2((float)v * (15.287712f / 127.f));
}

static void va_envCoefs(VaEnvCoefs *c, uint8_t a, uint8_t d, uint8_t s, uint8_t r)
{
	// attack: reaches 1 in the given time; decay/release: -60 dB in it
	const float ta = va_envTime(a);
	c->attack = 1.f - expf(-logf(VA_ATTACK_TARGET / (VA_ATTACK_TARGET - 1.f)) / (ta * VA_FS));
	c->decay = expf(-6.9077553f / (va_envTime(d) * VA_FS));
	c->release = expf(-6.9077553f / (va_envTime(r) * VA_FS));
	c->sustain = (float)s / 127.f;
	c->sustain *= c->sustain;  // a perceptual curve
}

static inline float va_envStep(VaEnv *e, const VaEnvCoefs *c)
{
	switch (e->stage)
	{
	case VA_ENV_ATTACK:
		e->level += (VA_ATTACK_TARGET - e->level) * c->attack;
		if (e->level >= 1.f)
		{
			e->level = 1.f;
			e->stage = VA_ENV_DECAY;
		}
		break;
	case VA_ENV_DECAY:
		e->level = c->sustain + (e->level - c->sustain) * c->decay;
		break;
	case VA_ENV_RELEASE:
		e->level *= c->release;
		if (e->level < 1e-5f)
		{
			e->level = 0.f;
			e->stage = VA_ENV_IDLE;
		}
		break;
	default:
		break;
	}
	return e->level;
}

// ---- a note --------------------------------------------------------------------------------

typedef struct
{
	VaOsc osc1[VA_MAX_UNISON];
	VaOsc osc2[VA_MAX_UNISON];
	VaOsc sub;
	VaNoise noise;
	VaLadder ladder[2];
	VaSvf svf[2];
	VaEnv aenv, fenv;

	float pitch;           // semitones (MIDI note numbers), gliding
	float targetPitch;
	float velocity;        // 0..1
	float ampPrev;         // the amp level at the end of the previous sample
	float cutPrev;         // the filter coefficient at the end of the previous sample
	float lfoFade;         // the LFO delay's fade-in, 0..1

	// analogue variance: fixed offsets (cents, octaves) and a slow drift
	float offset1[VA_MAX_UNISON], offset2[VA_MAX_UNISON], offsetCut;
	float drift;
	uint32_t rng;

	uint32_t gateSamples;  // samples until the note is released
	uint32_t age;
	uint8_t note;
	uint8_t active;
} VaVoice;

typedef struct
{
	VaVoice voice[VA_MAX_NOTES];
	VaHalfband hb1[2], hb2[2];
	float hpf[2];          // the output high-pass's states
	float lfoPhase, lfoValue, lfoSh;
	uint32_t rng;
	uint32_t age;
	uint32_t silentBlocks; // blocks without a sounding note (the decimators' tails)
	uint8_t lastNote;      // mono modes: the note held
	uint8_t monoGate;      // mono modes: a note is held
} VaSlot;

static VaSlot va_slot[VA_NUM_SLOTS];

static inline uint8_t *va_params(uint8_t slot)
{
	return &activeParameters[PAR_VA1_FIRST + slot * VA_NUM_PARAMS];
}

// The pan positions of the unison copies, -1..1, and their detune shape
// (a slightly irregular spread sounds richer than an even one).
static const float va_unisonShape[VA_MAX_UNISON][VA_MAX_UNISON] = {
	{0.f},
	{-1.f, 1.f},
	{0.f, -1.f, 1.f},
	{-1.f, -0.33f, 0.36f, 1.f},
	{0.f, -1.f, 1.f, -0.48f, 0.52f},
	{-1.f, -0.62f, -0.21f, 0.19f, 0.6f, 1.f},
	{0.f, -1.f, 1.f, -0.66f, 0.64f, -0.31f, 0.34f},
};

void va_init(void)
{
	va_blepInit();
	va_designHalfband(va_hb1, VA_HB1_SIDE);
	va_designHalfband(va_hb2, VA_HB2_SIDE);
	va_noiseInit();
	memset(va_slot, 0, sizeof(va_slot));
	for (int s = 0; s < VA_NUM_SLOTS; s++)
	{
		va_slot[s].rng = 0x9E3779B9u * (uint32_t)(s + 1);
		for (int v = 0; v < VA_MAX_NOTES; v++)
			va_slot[s].voice[v].rng = 0x85EBCA6Bu * (uint32_t)(s * VA_MAX_NOTES + v + 1);
	}
}

// ---- note handling ---------------------------------------------------------------------

static float va_gateSeconds(uint8_t gate)
{
	// GATE: 16 = one sequencer step; 0 = 1/16 step .. 127 = 8 steps
	const float steps = (float)(gate + 1) / 16.f;
	uint16_t bpm = seq_getBpm();
	if (bpm < 20)
		bpm = 120;
	return steps * 15.f / (float)bpm;  // a step is a sixteenth
}

static void va_startVoice(VaSlot *sl, VaVoice *v, const uint8_t *p, uint8_t note, float velocity, uint8_t retrigger, uint8_t glide)
{
	const uint8_t wasSilent = v->aenv.stage == VA_ENV_IDLE;
	v->note = note;
	v->targetPitch = (float)note;
	if (!glide || wasSilent)
		v->pitch = v->targetPitch;
	v->velocity = velocity;
	v->gateSamples = (uint32_t)(va_gateSeconds(p[VA_P_GATE]) * VA_FS);
	v->age = ++sl->age;
	v->active = 1;

	if (!retrigger)
		return;

	if (wasSilent)
	{
		// a fresh note: oscillator 1 starts at the same point every time (a
		// consistent attack), the rest anywhere; filters and envelopes cleared
		const float slop = (float)p[VA_P_SLOP] / 127.f;
		for (int u = 0; u < VA_MAX_UNISON; u++)
		{
			va_oscReset(&v->osc1[u], u == 0 ? 0.f : 0.5f * (va_randf(&v->rng) + 1.f));
			va_oscReset(&v->osc2[u], 0.5f * (va_randf(&v->rng) + 1.f));
			v->offset1[u] = va_randf(&v->rng) * 6.f * slop;   // cents
			v->offset2[u] = va_randf(&v->rng) * 6.f * slop;
		}
		va_oscReset(&v->sub, 0.f);
		v->offsetCut = va_randf(&v->rng) * 0.15f * slop;    // octaves
		memset(v->ladder, 0, sizeof(v->ladder));
		memset(v->svf, 0, sizeof(v->svf));
		v->svf[0].r1 = v->svf[1].r1 = 1.f;   // sqrt(1 + 0^2)
		v->ampPrev = 0.f;
		v->cutPrev = -1.f;
		v->noise.rng = va_rand(&v->rng) | 1u;
		v->aenv.level = v->fenv.level = 0.f;
	}
	// a retrigger of a sounding note starts the envelopes from where they are
	v->aenv.stage = VA_ENV_ATTACK;
	v->fenv.stage = VA_ENV_ATTACK;
	v->lfoFade = p[VA_P_LDELAY] ? 0.f : 1.f;
	if (p[VA_P_LWAVE] == VA_LFO_SH)
		sl->lfoSh = va_randf(&sl->rng);
}

void va_noteOn(uint8_t slot, uint8_t note, uint8_t velocity)
{
	if (slot >= VA_NUM_SLOTS)
		return;
	VaSlot *sl = &va_slot[slot];
	const uint8_t *p = va_params(slot);
	const float vel = (float)velocity / 127.f;
	sl->silentBlocks = 0;

	if (p[VA_P_MODE] == VA_MODE_POLY)
	{
		// a free voice, else the one sounding the same note, else the quietest
		// releasing one, else the oldest
		VaVoice *pick = 0;
		for (int i = 0; i < VA_MAX_NOTES && !pick; i++)
			if (sl->voice[i].aenv.stage == VA_ENV_IDLE)
				pick = &sl->voice[i];
		for (int i = 0; i < VA_MAX_NOTES && !pick; i++)
			if (sl->voice[i].note == note)
				pick = &sl->voice[i];
		if (!pick)
		{
			float lowest = 2.f;
			for (int i = 0; i < VA_MAX_NOTES; i++)
				if (sl->voice[i].aenv.stage == VA_ENV_RELEASE && sl->voice[i].aenv.level < lowest)
				{
					lowest = sl->voice[i].aenv.level;
					pick = &sl->voice[i];
				}
		}
		if (!pick)
		{
			pick = &sl->voice[0];
			for (int i = 1; i < VA_MAX_NOTES; i++)
				if (sl->voice[i].age < pick->age)
					pick = &sl->voice[i];
		}
		va_startVoice(sl, pick, p, note, vel, 1, 0);
		return;
	}

	// mono and legato: one voice; legato keeps the envelopes running while a
	// note is held and glides to the new one
	VaVoice *v = &sl->voice[0];
	const uint8_t held = v->active && v->aenv.stage != VA_ENV_RELEASE && v->aenv.stage != VA_ENV_IDLE;
	const uint8_t legato = p[VA_P_MODE] == VA_MODE_LEGATO && held;
	va_startVoice(sl, v, p, note, vel, !legato, p[VA_P_GLIDE] > 0);
}

void va_allNotesOff(uint8_t slot)
{
	for (int s = 0; s < VA_NUM_SLOTS; s++)
	{
		if (slot != 0xff && s != slot)
			continue;
		for (int i = 0; i < VA_MAX_NOTES; i++)
		{
			VaVoice *v = &va_slot[s].voice[i];
			if (v->aenv.stage != VA_ENV_IDLE)
			{
				v->aenv.stage = VA_ENV_RELEASE;
				v->fenv.stage = VA_ENV_RELEASE;
			}
			v->active = 0;
		}
	}
}

// ---- rendering -------------------------------------------------------------------------------

// what a block of a slot needs, from its parameters
typedef struct
{
	uint8_t wave1, wave2, subWave, noiseType, ftype, sync, lfoWave, unison, oscOn;
	float semi2, fine2, octave, subOct;
	float pw, lfoPwm;
	float g1, g2, gSub, gNoise;
	float satIn, satMakeup;     // SVF input saturation: intensity, make-up gain
	float ladderIn, ladderOut;  // ladder input and output gains
	float cutoff, reso, fenv, keytrack, fvel;   // cutoff and mods in octaves
	float avel;
	float lfoInc, lfoPitch, lfoCut, lfoFadeInc;
	float glideCoef;
	float spread, width;
	float slop;
	VaEnvCoefs aenv, fenvc;
} VaBlock;

static void va_blockSetup(VaBlock *b, const uint8_t *p)
{
	b->wave1 = p[VA_P_WAVE1] < VA_NUM_WAVES ? p[VA_P_WAVE1] : 0;
	b->wave2 = p[VA_P_WAVE2] < VA_NUM_WAVES ? p[VA_P_WAVE2] : 0;
	b->subWave = p[VA_P_SUB_WAVE] < VA_NUM_WAVES ? p[VA_P_SUB_WAVE] : 0;
	b->noiseType = p[VA_P_NOISE_TYPE] < VA_NUM_NOISES ? p[VA_P_NOISE_TYPE] : 0;
	b->ftype = p[VA_P_FTYPE] < VA_NUM_FILTERS ? p[VA_P_FTYPE] : 0;
	b->sync = p[VA_P_SYNC] != 0;
	b->lfoWave = p[VA_P_LWAVE] < VA_NUM_LFO_WAVES ? p[VA_P_LWAVE] : 0;
	b->unison = (uint8_t)(1 + (p[VA_P_UNISON] < VA_MAX_UNISON ? p[VA_P_UNISON] : VA_MAX_UNISON - 1));

	b->semi2 = va_clampf((float)p[VA_P_SEMI2] - 63.f, -48.f, 48.f);
	b->fine2 = ((float)p[VA_P_FINE2] - 63.f) / 100.f;
	b->octave = 12.f * (float)((p[VA_P_OCT] < 7 ? p[VA_P_OCT] : 3) - 3);
	b->subOct = p[VA_P_SUB_OCT] ? -24.f : -12.f;

	b->pw = 0.5f - 0.46f * (float)p[VA_P_PW] / 127.f;
	b->lfoPwm = 0.45f * (float)p[VA_P_LPWM] / 127.f;

	// equal-power crossfade between the oscillators
	const float m = (float)p[VA_P_MIX] / 127.f * (VA_PI * 0.5f);
	const float oscLevel = (float)p[VA_P_OSC_LEVEL] / 127.f;
	b->g1 = cosf(m) * oscLevel;
	b->g2 = sinf(m) * oscLevel;
	b->oscOn = p[VA_P_OSC_LEVEL] > 0;
	b->gSub = (float)p[VA_P_SUB] / 127.f;
	b->gNoise = (float)p[VA_P_NOISE] / 127.f;
	b->gNoise *= b->gNoise;

	// DRIVE: at 0 the filter input is practically linear; up to 127 the
	// saturation's intensity rises 40-fold (the SVF's input stage) or the
	// level into the ladder's saturating stages 16-fold, with make-up gain
	const float d = (float)p[VA_P_DRIVE] / 127.f;
	b->satIn = 0.06f * powf(40.f, d);
	b->satMakeup = (1.f + 0.5f * b->satIn) / b->satIn;
	b->ladderIn = 0.25f * powf(16.f, d);
	b->ladderOut = 1.f / (0.25f * (1.f + 3.f * d));

	b->cutoff = (float)p[VA_P_CUTOFF] * (10.f / 127.f);       // 20 Hz .. 20.5 kHz
	const float res = (float)p[VA_P_RESO] / 127.f;
	b->reso = res;
	b->fenv = ((float)p[VA_P_FENV_AMT] - 63.f) / 63.f * 8.f; // +-8 octaves
	b->keytrack = (float)p[VA_P_KEYTRACK] / 127.f;
	b->fvel = (float)p[VA_P_FVEL] / 127.f * 4.f;
	b->avel = (float)p[VA_P_AVEL] / 127.f;

	// LFO: 0.03 .. 40 Hz
	const float lfoHz = 0.03f * va_exp2((float)p[VA_P_LRATE] * (10.4f / 127.f));
	b->lfoInc = lfoHz / VA_FS;
	const float lp = (float)p[VA_P_LPITCH] / 127.f;
	b->lfoPitch = 12.f * lp * lp;                             // up to an octave
	b->lfoCut = ((float)p[VA_P_LCUT] - 63.f) / 63.f * 4.f;   // +-4 octaves
	b->lfoFadeInc = p[VA_P_LDELAY] ? 1.f / (va_envTime(p[VA_P_LDELAY]) * VA_FS) : 1.f;

	// glide: exponential, 0 .. ~5 s to arrive
	b->glideCoef = p[VA_P_GLIDE] ? expf(-1.f / (0.0002f * va_exp2((float)p[VA_P_GLIDE] * (14.6f / 127.f)) * VA_FS)) : 0.f;

	b->spread = (float)p[VA_P_SPREAD] / 127.f * 50.f;         // cents at the outermost copy
	b->width = (float)p[VA_P_WIDTH] / 127.f;
	b->slop = (float)p[VA_P_SLOP] / 127.f;

	va_envCoefs(&b->aenv, p[VA_P_AATK], p[VA_P_ADEC], p[VA_P_ASUS], p[VA_P_AREL]);
	va_envCoefs(&b->fenvc, p[VA_P_FATK], p[VA_P_FDEC], p[VA_P_FSUS], p[VA_P_FREL]);
}

static inline float va_lfoShape(uint8_t wave, float phase, float sh)
{
	switch (wave)
	{
	default:
	case VA_LFO_SINE:   return va_sin2pi(phase);
	case VA_LFO_TRI:    return phase < 0.5f ? 4.f * phase - 1.f : 3.f - 4.f * phase;
	case VA_LFO_SAW:    return 1.f - 2.f * phase;
	case VA_LFO_SQUARE: return phase < 0.5f ? 1.f : -1.f;
	case VA_LFO_SH:     return sh;
	}
}

// Renders one voice for a block into the oversampled stereo sums.
static void va_renderVoice(VaSlot *sl, VaVoice *v, const VaBlock *b, const float *lfo, float *sumL, float *sumR)
{
	const uint8_t stereo = b->unison > 1 && b->width > 0.f;
	const uint8_t nu = b->unison;
	const float uniNorm = 1.f / sqrtf((float)nu);
	const float *shape = va_unisonShape[nu - 1];
	float panL[VA_MAX_UNISON], panR[VA_MAX_UNISON];
	for (int u = 0; u < nu; u++)
	{
		const float pan = shape[u] * b->width;           // -1..1
		const float ang = (pan + 1.f) * (VA_PI * 0.25f);
		panL[u] = cosf(ang) * 1.41421356f * uniNorm;
		panR[u] = sinf(ang) * 1.41421356f * uniNorm;
	}

	// the voice's drift: a random walk pulled back to 0, a few cents
	v->drift += va_randf(&v->rng) * 0.08f;
	v->drift *= 0.995f;
	const float drift = v->drift * b->slop * 4.f;   // cents

	for (int i = 0; i < VA_BLOCK; i++)
	{
		// ---- control rate: once per output sample ----
		if (v->gateSamples > 0 && --v->gateSamples == 0)
		{
			v->aenv.stage = v->aenv.stage == VA_ENV_IDLE ? VA_ENV_IDLE : VA_ENV_RELEASE;
			v->fenv.stage = v->fenv.stage == VA_ENV_IDLE ? VA_ENV_IDLE : VA_ENV_RELEASE;
			v->active = 0;
		}
		const float amp = va_envStep(&v->aenv, &b->aenv);
		const float fenv = va_envStep(&v->fenv, &b->fenvc);
		if (v->lfoFade < 1.f)
			v->lfoFade = va_clampf(v->lfoFade + b->lfoFadeInc, 0.f, 1.f);
		const float l = lfo[i] * v->lfoFade;

		v->pitch = v->targetPitch + (v->pitch - v->targetPitch) * b->glideCoef;
		const float basePitch = v->pitch + b->octave + l * b->lfoPitch + drift * 0.01f;

		// cutoff in octaves above 20 Hz
		const float cutOct = b->cutoff + b->fenv * fenv + b->keytrack * (v->pitch - 60.f) / 12.f +
		                     b->fvel * (v->velocity - 0.5f) + b->lfoCut * l + v->offsetCut;
		const float fc = va_clampf(20.f * va_exp2(cutOct), 10.f, 26000.f);
		const float fTarget = va_tan(VA_PI * fc / VA_FS_OS);
		if (v->cutPrev < 0.f)
			v->cutPrev = fTarget;
		const float fStep = (fTarget - v->cutPrev) * (1.f / VA_OS);

		const float velAmp = 1.f - b->avel + b->avel * v->velocity;
		const float ampTarget = amp * velAmp;
		const float ampStep = (ampTarget - v->ampPrev) * (1.f / VA_OS);

		const float pw = va_clampf(b->pw + b->lfoPwm * l, 0.03f, 0.97f);

		// oscillator increments (per oversampled sample)
		double dt1[VA_MAX_UNISON], dt2[VA_MAX_UNISON];
		for (int u = 0; u < nu; u++)
		{
			const float det = shape[u] * b->spread;
			const float p1 = basePitch + (det + v->offset1[u]) * 0.01f;
			const float p2 = basePitch + b->semi2 + b->fine2 + (det + v->offset2[u]) * 0.01f;
			dt1[u] = va_clampf(440.f * va_exp2((p1 - 69.f) * (1.f / 12.f)) / VA_FS_OS, 0.f, 0.45f);
			dt2[u] = va_clampf(440.f * va_exp2((p2 - 69.f) * (1.f / 12.f)) / VA_FS_OS, 0.f, 0.45f);
		}
		const float dtSub = va_clampf(440.f * va_exp2((basePitch + b->subOct - 69.f) * (1.f / 12.f)) / VA_FS_OS, 0.f, 0.45f);

		// filter coefficients for this sample
		const float reso = b->reso;
		const float ladderR = 4.4f * reso;
		const float svfK = 2.f - 1.98f * reso;
		// the ladder loses passband level with resonance; give some of it back
		const float ladderComp = 1.f + 0.6f * ladderR;

		// ---- audio rate: VA_OS samples ----
		for (int k = 0; k < VA_OS; k++)
		{
			v->cutPrev += fStep;
			v->ampPrev += ampStep;
			const float f = v->cutPrev;

			float xl = 0.f, xr = 0.f;
			for (int u = 0; u < nu && b->oscOn; u++)
			{
				// oscillator 1, and the time of its wrap for the sync
				const double a = v->osc1[u].phase;
				const float s1 = va_oscStep(&v->osc1[u], b->wave1, dt1[u], pw);
				const double wrapT = (a + dt1[u] >= 1.0) ? (a + dt1[u] - 1.0) / dt1[u] : -1.0;
				const float s2 = b->sync ? va_oscStepSync(&v->osc2[u], b->wave2, dt2[u], pw, wrapT)
				                         : va_oscStep(&v->osc2[u], b->wave2, dt2[u], pw);
				const float s = b->g1 * s1 + b->g2 * s2;
				xl += s * panL[u];
				xr += s * panR[u];
			}
			float common = 0.f;
			if (b->gSub > 0.f)
				common += b->gSub * va_oscStep(&v->sub, b->subWave, dtSub, 0.5f);
			if (b->gNoise > 0.f)
				common += b->gNoise * va_noiseStep(&v->noise, b->noiseType);

			xl += common;
			xr += common;

			float yl, yr;
			if (b->ftype == VA_FILTER_LP24)
			{
				const float g = b->ladderOut * ladderComp;
				yl = va_ladderStep(&v->ladder[0], xl * b->ladderIn, f, ladderR) * g;
				yr = stereo ? va_ladderStep(&v->ladder[1], xr * b->ladderIn, f, ladderR) * g : yl;
			}
			else
			{
				yl = va_svfStep(&v->svf[0], va_saturateAdaa(&v->svf[0], xl * b->satIn) * b->satMakeup, f, svfK, b->ftype);
				if (stereo)
					yr = va_svfStep(&v->svf[1], va_saturateAdaa(&v->svf[1], xr * b->satIn) * b->satMakeup, f, svfK, b->ftype);
				else
					yr = yl;
			}

			const float g = v->ampPrev;
			sumL[i * VA_OS + k] += yl * g;
			sumR[i * VA_OS + k] += yr * g;
		}
	}
}

uint8_t va_render(uint8_t slot, int16_t *left, int16_t *right)
{
	if (slot >= VA_NUM_SLOTS)
		return 0;
	VaSlot *sl = &va_slot[slot];
	const uint8_t *p = va_params(slot);

	uint8_t sounding = 0;
	for (int i = 0; i < VA_MAX_NOTES; i++)
		if (sl->voice[i].aenv.stage != VA_ENV_IDLE)
			sounding = 1;
	// after the last note the decimators and the high-pass still ring a little
	if (!sounding && ++sl->silentBlocks > 8)
	{
		sl->silentBlocks = 9;
		return 0;
	}

	VaBlock b;
	va_blockSetup(&b, p);

	// the slot's LFO, at the sample rate
	float lfo[VA_BLOCK];
	for (int i = 0; i < VA_BLOCK; i++)
	{
		const float prev = sl->lfoPhase;
		sl->lfoPhase += b.lfoInc;
		if (sl->lfoPhase >= 1.f)
		{
			sl->lfoPhase -= 1.f;
			sl->lfoSh = va_randf(&sl->rng);
		}
		(void)prev;
		lfo[i] = va_lfoShape(b.lfoWave, sl->lfoPhase, sl->lfoSh);
	}

	static float sumL[VA_BLOCK * VA_OS], sumR[VA_BLOCK * VA_OS];
	memset(sumL, 0, sizeof(sumL));
	memset(sumR, 0, sizeof(sumR));
	for (int i = 0; i < VA_MAX_NOTES; i++)
		if (sl->voice[i].aenv.stage != VA_ENV_IDLE)
			va_renderVoice(sl, &sl->voice[i], &b, lfo, sumL, sumR);

	// down to the sample rate, then the output high-pass (one pole, 8 Hz .. 4 kHz)
	const float hpHz = 8.f * va_exp2((float)p[VA_P_HPF] * (9.f / 127.f));
	const float hpCoef = expf(-VA_TWO_PI * hpHz / VA_FS);
	const float volume = (float)p[VA_P_VOLUME] / 127.f;
	const float gain = volume * volume * VA_OUT_SCALE;
	float half[2][VA_BLOCK * 2];
	for (int c = 0; c < 2; c++)
	{
		const float *in = c ? sumR : sumL;
		for (int i = 0; i < VA_BLOCK * 2; i++)
			half[c][i] = va_halfband(&sl->hb1[c], va_hb1, VA_HB1_SIDE, in[2 * i], in[2 * i + 1]);
		int16_t *out = c ? right : left;
		for (int i = 0; i < VA_BLOCK; i++)
		{
			const float x = va_halfband(&sl->hb2[c], va_hb2, VA_HB2_SIDE, half[c][2 * i], half[c][2 * i + 1]);
			// one-pole high-pass: y = x - lowpass(x)
			sl->hpf[c] = hpCoef * sl->hpf[c] + (1.f - hpCoef) * x;
			float y = (x - sl->hpf[c]) * gain;
			// gentle soft limit approaching full scale
			const float lim = 32000.f;
			if (y > 0.75f * lim || y < -0.75f * lim)
				y = lim * va_tanh(y / lim);
			out[i] = (int16_t)y;
		}
	}
	if (sounding)
		sl->silentBlocks = 0;
	return 1;
}

// ---- parameters and presets ----------------------------------------------------------------

void va_paramChanged(uint8_t slot, uint8_t paramIdx)
{
	if (slot >= VA_NUM_SLOTS)
		return;
	if (paramIdx == VA_P_PRESET)
		va_loadPreset(slot, va_params(slot)[VA_P_PRESET]);
	else if (paramIdx == VA_P_MODE)
		va_allNotesOff(slot);
}

void va_loadPreset(uint8_t slot, uint8_t presetNr)
{
	if (slot >= VA_NUM_SLOTS || presetNr >= va_numPresets[slot])
		return;
	uint8_t *p = va_params(slot);
	memcpy(p, va_presets[slot][presetNr].p, VA_NUM_SOUND_PARAMS);
	p[VA_P_PRESET] = presetNr;
}

void va_initKitParameters(uint8_t *params)
{
	for (int s = 0; s < VA_NUM_SLOTS; s++)
	{
		uint8_t *p = &params[s * VA_NUM_PARAMS];
		memcpy(p, va_presets[s][0].p, VA_NUM_SOUND_PARAMS);
		p[VA_P_PRESET] = 0;
		p[VA_P_VOLUME] = 100;
		p[VA_P_PAN] = 63;
		p[VA_P_OUT] = 0;
		p[VA_P_MIDI_CHAN] = (uint8_t)(8 + s);   // channels 8..11
	}
}
