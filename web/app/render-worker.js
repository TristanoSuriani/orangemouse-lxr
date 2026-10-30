// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// Renders a song offline in WebAssembly, in a worker of its own: the browser's
// copy of tests/host/song_render.c, for WAV export (PLAN.md Phase 8.1) and for
// timing the worst case in the browser (Phase 7.2). It does what song_render
// does, in the same order, so the two renders are the same, sample for
// sample: an empty SD card, seed 1, the song's messages, 400 blocks for the
// engines to take them and the LXR to boot, then the song from the start.
//
//   new Worker('render-worker.js', { type: 'module' }).postMessage({
//     wasm,          // studio.wasm's bytes (ArrayBuffer)
//     messages,      // songMessages(song)
//     seconds,       // how long to render (or blocks: how many 64-frame blocks)
//     samples,       // true: send back the left and right channels
//     sdImage,       // optional: the LXR's SD card (its samples), else an empty one
//   })
// It answers { type: 'progress', done } (0..1) now and then, and at the end
// { type: 'done', blocks, usPerBlock, hash, peak, nonFinite, left, right }, or
// { type: 'error', message }.

import { studioImports, applyMessage } from './studio-wasm.js';

const RATE = 44100;
const SD_SECTORS = 32768;   // as the LXR's own default card (engines/lxr/lxr_engine.c)
const WARMUP_BLOCKS = 400;

// the 64-bit FNV-1a hash of song_render.c, on 32-bit halves. Its start is
// song_render's 1469598103934665603 (FNV's offset without its last digit):
// the golden hashes are made with it.
function hasher() {
  let hi = 0x14650fb0, lo = 0x739d0383;
  return {
    add(bytes) {
      for (let i = 0; i < bytes.length; i++) {
        lo = (lo ^ bytes[i]) >>> 0;
        // times 0x100000001b3: lo * 0x1b3, and lo << 40 into the high half
        const low = lo * 0x1b3;
        const carry = Math.floor(low / 0x100000000);
        hi = (hi * 0x1b3 + carry + ((lo << 8) >>> 0)) >>> 0;
        lo = low >>> 0;
      }
    },
    hex: () => hi.toString(16).padStart(8, '0') + lo.toString(16).padStart(8, '0'),
  };
}

self.onmessage = ({ data }) => {
  try {
    const log = [];
    let memory = null;
    const imports = studioImports({ memory: () => memory, log: (t) => log.push(t), sdWritten: () => {}, midiOut: () => {} });
    const w = new WebAssembly.Instance(new WebAssembly.Module(data.wasm), imports).exports;
    memory = w.memory;
    w._initialize();
    const card = data.sdImage ? new Uint8Array(data.sdImage) : null;
    const sectors = card ? card.length / 512 : SD_SECTORS;
    const sd = w.web_sd_alloc(sectors);
    if (card) new Uint8Array(w.memory.buffer).set(card, sd);
    else new Uint8Array(w.memory.buffer).fill(0, sd, sd + SD_SECTORS * 512);   // empty: formatted on boot
    if (w.om_web_init(RATE, 1) !== 0) throw new Error('the studio did not start');
    for (const m of data.messages) applyMessage(w, m);
    const n = w.om_web_block_size();
    for (let b = 0; b < WARMUP_BLOCKS; b++) w.om_web_render();
    w.om_web_tracker_play_song(0);

    const blocks = data.blocks ?? Math.floor((data.seconds * RATE) / n);
    const left = data.samples ? new Float32Array(blocks * n) : null;
    const right = data.samples ? new Float32Array(blocks * n) : null;
    const hash = hasher();
    // the time of the whole loop (the hash and checks cost little): a worker's
    // clock is too coarse to time single blocks
    const start = performance.now();
    let peak = 0, nonFinite = 0, lastNote = start;
    for (let b = 0; b < blocks; b++) {
      const ptr = w.om_web_render();
      const out = new Float32Array(w.memory.buffer, ptr, n * 2);   // left block, right block
      hash.add(new Uint8Array(w.memory.buffer, ptr, n * 8));
      for (let i = 0; i < n * 2; i++) {
        const x = out[i];
        if (!Number.isFinite(x)) nonFinite++;
        else if (Math.abs(x) > peak) peak = Math.abs(x);
      }
      if (left) { left.set(out.subarray(0, n), b * n); right.set(out.subarray(n), b * n); }
      if ((b & 63) === 0 && performance.now() - lastNote > 250) { lastNote = performance.now(); self.postMessage({ type: 'progress', done: b / blocks }); }
    }
    const result = { type: 'done', blocks, usPerBlock: (1000 * (performance.now() - start)) / blocks, hash: hash.hex(), peak, nonFinite, left, right, log };
    self.postMessage(result, left ? [left.buffer, right.buffer] : []);
  } catch (e) {
    self.postMessage({ type: 'error', message: String(e?.message ?? e) });
  }
};
