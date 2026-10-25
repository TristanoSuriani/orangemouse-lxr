// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// WebAssembly parity: renders the demo groove with the browser build
// (web/app/lxr.wasm), through the same calls as tests/host/lxr_render.c, and
// compares it sample by sample with the native render.
//
//   node tests/host/parity.mjs <native.f32> [seconds] [rate]
//
// Passes when every sample differs by at most 1 LSB of 16 bits (the firmware
// renders 16-bit samples; see spikes/README.md for where a 1-LSB difference
// can come from). Exit code 0 = pass.

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const [nativePath, seconds = '8', rate = '44100'] = process.argv.slice(2);
if (!nativePath) {
  console.error('usage: node tests/host/parity.mjs <native.f32> [seconds] [rate]');
  process.exit(2);
}

const wasmPath = fileURLToPath(new URL('../../web/app/lxr.wasm', import.meta.url));
let memory;
const cString = (ptr) => {
  const bytes = new Uint8Array(memory.buffer, ptr);
  return new TextDecoder().decode(bytes.subarray(0, bytes.indexOf(0)));
};
const imports = {
  env: {
    web_log: (ptr) => console.error('lxr:', cString(ptr)),
    web_sd_written: () => {},
    web_midi_out: () => {},
    emscripten_notify_memory_growth: () => {},
  },
  wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }),
};
const instance = new WebAssembly.Instance(new WebAssembly.Module(readFileSync(wasmPath)), imports);
const w = instance.exports;
memory = w.memory;
w._initialize();

w.web_sd_alloc(32768);
w.web_boot(Number(rate), 1);
const n = w.web_block_size();
const blocks = Math.floor(Number(seconds) * Number(rate) / n);
const wasmOut = new Float32Array(blocks * 4 * n);
for (let b = 0; b < blocks; b++) {
  w.web_test_demo_block(b, Number(rate));
  const ptr = w.web_render();
  wasmOut.set(new Float32Array(memory.buffer, ptr, 4 * n), b * 4 * n);
}

const native = new Float32Array(readFileSync(nativePath).buffer.slice(0));
if (native.length !== wasmOut.length) {
  console.error(`length differs: native ${native.length} samples, wasm ${wasmOut.length}`);
  process.exit(1);
}
let worst = 0, worstAt = -1, differing = 0;
for (let i = 0; i < native.length; i++) {
  const d = Math.abs(native[i] - wasmOut[i]) * 32768;
  if (d > 0) differing++;
  if (d > worst) { worst = d; worstAt = i; }
}
const ok = worst <= 1;
console.log(`${ok ? 'PASS' : 'FAIL'}: ${differing} of ${native.length} samples differ, the largest by ${worst.toFixed(2)} LSB` +
  (worstAt >= 0 ? ` (block ${Math.floor(worstAt / (4 * n))}, channel ${Math.floor(worstAt / n) % 4})` : ''));
process.exit(ok ? 0 : 1);
