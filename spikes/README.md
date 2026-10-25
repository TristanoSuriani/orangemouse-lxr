# Phase 0 spikes: foreign code in WebAssembly

PLAN.md Phase 0, step 2. Each spike builds a piece of third-party DSP natively (clang, `-O2`) and as
WebAssembly (`em++ -O3 -msimd128`, run under node, whose WebAssembly engine is Chrome's). Each one renders
10 s of music, times it and writes WAVs to `spikes/out/{native,wasm}/`.

```sh
third_party/fetch.sh              # once: the sources, at pinned commits
source ~/emsdk/emsdk_env.sh
spikes/build.sh                   # all spikes; or: spikes/build.sh plaits dexed sst
```

## Results (2026-10-25, Apple Silicon, node 24)

"% core" is the share of one core the code needs to keep up in real time at 48 kHz.

| Spike | Builds unchanged? | Cost in WebAssembly | Native vs WebAssembly output |
|---|---|---|---|
| Plaits, 24 engines, one voice | Yes, with `-DTEST` | 0.1–0.8% per voice (FM and particle are the heaviest) | Bit for bit, with `-ffp-contract=off` natively |
| Dexed (msfa + EngineMkI), 16 voices, chords + bass line | With 3 small shims (below) | 1.0–1.6% for the whole part | Within 1 LSB |
| sst-effects: Reverb2, Reverb1, Delay, Phaser, Flanger | Yes; SIMDE maps SSE to WebAssembly SIMD | 0.2–0.4% per effect | Within 1 LSB |

**Verdict: all three go ahead.** Even a busy song needs only a few percent of the audio thread for these
three. The LXR and Orange Mouse VA are measured in step 3, with the native host build.

## What the spikes taught

1. **Turn off FMA contraction natively.** Clang on ARM fuses multiply-adds by default. That changed
   Plaits' chaotic engines (bass drum, wave terrain) completely between the Mac and the browser.
   With `-ffp-contract=off`, Plaits matches bit for bit. So golden hashes taken on the Mac also hold for the
   browser build, and the WebAssembly parity test can mostly be exact.
2. **The remaining 1-LSB differences come from the math library.** Dexed and the Surge effects build tables
   at start-up with `pow`/`exp`, and macOS libm and Emscripten's musl round a few results differently. The
   parity test therefore allows 1 LSB. Shipping one math library for both targets would remove it.
3. **Plaits' output must be taken before it is clipped to 16 bits.** `plaits::Voice::Render` writes
   saturated `short`s, and several engines hit full scale at our test level. The real wrapper calls each
   engine's float `Render` and applies the LPG itself, or scales before the conversion.
4. **Plaits also contains a six-operator DX7 engine** (`six_op`), which reads DX7 patches. Its third bank
   (user data) is empty without a loaded cartridge.
5. **Dexed's engine needs three shims, now in `engines/dexed/shim/`.** They replace standard tuning
   (Dexed's `tuning.cc` needs JUCE), MTS-ESP (no microtuning master, ever) and the JUCE memory logger.
   No Dexed file is modified.
6. **sst-effects has no chorus yet.** `Chorus.h` is an empty placeholder (Surge's chorus hasn't been moved
   into the library). Phase 3 needs one, especially for dub techno: port Surge's chorus, or write one.
   The library does have Delay, FloatyDelay, Reverb1/2, Phaser, Flanger, RotarySpeaker, Bonsai,
   TreeMonster and Nimbus.
7. **Effect defaults aren't musical settings.** With defaults, Delay and Phaser come out mono, and Delay
   left nothing audible in the last 1.5 s. Phase 3 sets real parameters for each effect.

## Licences

Plaits, stmlib, SIMDE and fmt are MIT. Dexed and the sst libraries are GPL-3.0. Everything fits the LXR
firmware's GPL-3.0.
