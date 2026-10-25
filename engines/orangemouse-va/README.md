# Orange Mouse VA

A virtual-analog synthesizer with four slots, each with its own preset bank: VA1 mono bass, VA2
polyphonic, VA3 mono lead and VA4 noises. It is a device of its own in the Orange Mouse studio
(`docs/PLAN.md`), separate from the LXR.

## Status

**Not built right now.** The engine was written inside the LXR firmware. It was taken out on 2026-10-25,
when Orange Mouse VA became a separate device, and it still depends on LXR internals:

| Dependency | Where | Replaced by (PLAN.md Phase 0, step 5) |
|---|---|---|
| Parameters in LXR's `activeParameters` from `PAR_VA1_FIRST` | `VaEngine.c`, `va_slotParams` | The engine's own parameter block, declared in the registry |
| Tempo from `seq_getBpm()` | `VaEngine.c` (LFO sync) | The tempo passed in by the host |
| `REAL_FS`, `OUTPUT_DMA_SIZE` from `Soundengine/config.h` | `VaEngine.c`, `VaEngine.h` | Sample rate and block size passed to `init` |
| `Sequencer/sequencer.h`, `Parameters.h`, `menu/menu.h` | `VaEngine.c` includes | Nothing; they go |

`lxr-integration.patch` is the uncommitted firmware change that wired the engine into the LXR as tracks
8–11: the sequencer, MIDI channels, buttons and LEDs, the menu pages, the kit file and the mixer. It is kept
only for reference (the menu pages and parameter ranges are worth reusing for the device's panel); it is
not meant to be applied again.

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
