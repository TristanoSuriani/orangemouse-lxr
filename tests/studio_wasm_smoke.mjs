// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// Smoke test of web/app/studio.wasm, the module the app runs: instantiated as
// the AudioWorklet does (no Emscripten runtime), it boots, plays an LXR hit
// and a VA note, reads VA's presets and parameters, and renders sound.
//   node tests/studio_wasm_smoke.mjs

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const wasm = readFileSync(fileURLToPath(new URL('../web/app/studio.wasm', import.meta.url)));
let memory;
const instance = new WebAssembly.Instance(new WebAssembly.Module(wasm), {
  env: { web_log: () => {}, web_sd_written: () => {}, web_midi_out: () => {}, emscripten_notify_memory_growth: () => {} },
  wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }),
});
const w = instance.exports;
memory = w.memory;
w._initialize();

let failures = 0;
const check = (ok, msg) => { if (!ok) { failures++; console.log('FAIL ' + msg); } };
const cString = (ptr) => {
  const b = new Uint8Array(memory.buffer, ptr);
  return new TextDecoder().decode(b.subarray(0, b.indexOf(0)));
};

w.web_sd_alloc(32768);
check(w.om_web_init(44100, 1) === 0, 'om_web_init failed');
const n = w.om_web_block_size();
check(n === 64, `block size ${n}`);

const presets = w.om_web_va_presets(0);
check(presets > 0, 'VA1 has no presets');
check(cString(w.om_web_va_preset_name(0, 0)).length > 0, 'VA1 preset 0 has no name');

w.om_web_set_param(1, 0, 16, 100);   // VA1 cutoff
w.om_web_note_on(0, 0, 36, 120);     // LXR drum 1
w.om_web_note_on(1, 0, 45, 100);     // VA1
let peak = 0;
for (let b = 0; b < 200; b++) {
  const ptr = w.om_web_render();
  const block = new Float32Array(memory.buffer, ptr, 2 * n);
  for (const v of block) {
    check(Number.isFinite(v), 'a sample is NaN or infinite');
    peak = Math.max(peak, Math.abs(v));
  }
}
check(peak > 0.05, `the studio is silent (peak ${peak})`);
check(w.om_web_get_param(1, 0, 16) === 100, `VA1 cutoff reads ${w.om_web_get_param(1, 0, 16)}, set 100`);

if (failures) process.exit(1);
console.log(`studio.wasm: boots, plays (peak ${peak.toFixed(2)}), ${presets} VA1 presets ("${cString(w.om_web_va_preset_name(0, 0))}")`);
