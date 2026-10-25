# The LXR on the host

The LXR firmware built natively (macOS, Linux) from the same sources and port as the browser build
(`web/lxr_sources.cmake`), so tests can render audio without a browser. PLAN.md Phase 0, step 3.

```sh
cmake -S tests/host -B tests/host/build -G Ninja && cmake --build tests/host/build
tests/host/build/lxr_render --wav out.wav            # 8 s of the demo groove
node tests/host/parity.mjs out.f32                   # after --raw out.f32: compare with web/app/lxr.wasm
```

Or run everything with `tests/run.sh`.

## What it does

`lxr_render` boots the firmware on an empty 16 MB SD card, exactly as the browser does on first use. It
then plays the demo groove (`web_test_demo_block` in `web/port/web_test.c`): one bar of 16ths on all seven
voices at 120 BPM. It prints the time per block and a hash of the output.

`parity.mjs` runs the same calls on the browser build (`web/app/lxr.wasm`) under node and compares every
sample. The test hooks are C, so both builds play exactly the same notes.

## Golden renders and health (`lxr_sweep`)

PLAN.md Phase 0, step 4. Every sound parameter is rendered at its minimum, middle and maximum value
(1,354 renders of 2 s each), in two kits:
- **init**: the built-in init kit.
- **busy**: drum 2 and the snare go to the FX bus with the delay, every LFO modulates its voice, and
  velocity modulates every voice (`web_test_busy_kit`).

Each case starts from a freshly booted machine (one fork per case) and loads the kit exactly as loading one
from the SD card does (`preset_sendDrumsetParametersImmediate`). Each line records a hash of the output,
the peak, the DC offset, the full-scale samples and any NaN or Inf. `tests/run.sh` compares the result with
`tests/golden/lxr_params.txt`, and runs the whole sweep in about 4 s on 8 cores.

**Coverage.** 182 of the 247 sound parameters change the sound in at least one case. The other 65 are:
- 7 UNUSED slots;
- the NRPN and bank-change bytes;
- parameters that only act in a mode neither kit uses: the mod oscillator waves, FM frequency (FM mode),
  pulse width (pulse wave), transients, resonance on filter types without it, LFO offsets and
  voice-switching, and the other FX types' settings.

The parameter registry (PLAN.md step 0.7) can give each of these the context it needs.

**Found so far.** Parameters 58 and 66 at their maximum reach full scale (2 samples each). Nothing else
clips, and nothing renders NaN or Inf.

**Proof that it catches changes.** Lowering the snare's noise level from 0.9 to 0.89 in `Snare.c` failed
the demo hash and 1,346 of the 1,354 sweep cases.

## CPU budget

`tests/run.sh` takes the best of three demo renders and fails if it is more than 25 % slower than
`tests/golden/lxr_cpu.txt`.

## Updating the golden files

When a change to the sound is intended, accept it with `UPDATE_GOLDEN=1 tests/run.sh`, and commit the new
golden files in their own commit so the diff shows exactly which renders changed.

## The same samples in both builds

The two builds produce bit-for-bit identical output. Four things were needed for that:

| Difference | Fix |
|---|---|
| clang fuses multiply-adds on ARM; WebAssembly has no FMA | `-ffp-contract=off` in the host build |
| `-ffast-math` lets the ARM and WebAssembly backends rewrite float code differently (up to 2 LSB, 0.4 % of samples) | `LXR_FAST_MATH` is off by default in both builds. The firmware's hardware build keeps it; this one never runs on the hardware. |
| macOS libm and Emscripten's musl can round `sinf`, `cosf`, `expf`, `exp`, `logf` differently | The host build compiles those five from Emscripten's own musl sources (`musl_rename.h`) |
| Address 0 is readable on the MCU and in WebAssembly, not on the host | One missing NULL check added in `modulationNode.c`, matching the two checks already in that file |

Two more host-only details:
- Mach-O rejects the firmware's TCM section names (`host_port.h`).
- Apple clang predefines `__weak`, so the build redefines it (`CMakeLists.txt`).

## Cost

About 1 % of one Apple Silicon core for seven voices, effects included.
