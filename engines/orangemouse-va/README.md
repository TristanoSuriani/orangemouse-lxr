# Orange Mouse VA

A virtual-analog synthesizer with four slots, each with its own preset bank: VA1 mono bass, VA2
polyphonic, VA3 mono lead and VA4 noises. It is a device of its own in the Orange Mouse studio
(`docs/PLAN.md`), separate from the LXR.

## Status

**Standalone since 2026-10-25** (PLAN.md Phase 0, step 5). The engine no longer depends on the LXR:
- It keeps its own parameters (`va_setParam`, `va_getParam`).
- It takes the sample rate in `va_init()` and the tempo in `va_setTempo()`.
- It renders float output, any number of frames up to `VA_BLOCK`, so notes start on their exact sample.
- Besides its GATE-timed `va_noteOn`, it has held notes with a note off (`va_noteOnHeld`, `va_noteOff`).

`va_engine.c` plugs it into the studio (`engines/engine.h`): four parts (the slots) and one stem per slot.
`tests/host/studio_test.c` checks that a held note sounds until its note off and then releases to silence.
`tests/host/studio_render.c` plays it in the studio's demo song.

`lxr-integration.patch` is the firmware change that once wired the engine into the LXR as tracks 8–11. It is
kept only for reference: its menu pages and parameter ranges are worth reusing for the device's panel.

## The sound

Every note runs at 4× the sample rate. The oscillators are windowed-sinc BLEP/BLAMP, including hard sync.
The filters are a nonlinear zero-delay-feedback ladder and a state-variable filter with an anti-aliased
input stage. Kaiser halfband filters decimate back to the sample rate.

Measured with the filter open and DRIVE at 0, aliasing of saw and square stays at or below -100 dB up to
C8 (4.2 kHz).

## Parameters (as on the LXR pages)

| Page | Parameters |
|------|------------|
| 1 OSC    | WV1/WV2 waves (saw, square, triangle, sine), SEM/FIN oscillator 2 tuning, PW pulse width, MIX osc 1/2, SYN hard sync, OCT octave |
| 2 MIXER  | OSC level, SUB level/wave/octave, NOI noise level and NTY type (white, pink, brown), DRV drive, SLP analogue slop |
| 3 FILTER | CUT, RES, TYP (L24 ladder, L12/B12/H12 state-variable), ENV amount, KEY tracking, VEL to cutoff, HPF |
| 4 F.ENV  | Filter envelope ATK DEC SUS REL |
| 5 A.ENV  | Amp envelope ATK DEC SUS REL, VEL to level |
| 6 LFO    | RAT, WAV (sine, triangle, saw, square, S&H), PIT vibrato, CUT, PWM, DLY fade-in |
| 7 VOICE  | MOD (mono, legato, poly), GAT gate length, GLD glide, UNI unison 1-7, SPR detune, WID stereo width |
| 8 MIX    | PRE preset, VOL, PAN |

The presets are in `VaPresets.c`.
