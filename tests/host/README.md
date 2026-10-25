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
