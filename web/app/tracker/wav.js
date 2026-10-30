// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// WAV export (PLAN.md Phase 8.1): a song rendered offline in WebAssembly, in
// a worker of its own (render-worker.js), as tests/host/song_render.c renders
// it natively: the whole order list at each pattern's tempo and 2 s of tail.
// The live studio keeps playing meanwhile. The file is 24-bit stereo PCM at
// the studio's 44.1 kHz.

import { songMessages } from './song.js';

const RATE = 44100;
const TAIL = 2;   // seconds after the last row: releases and the effects' tails

// how long a song plays: its order list, each pattern at its own tempo
export function songSeconds(song) {
  let seconds = 0;
  for (const p of song.order) {
    const pat = song.patterns[p];
    if (pat) seconds += (pat.rows * 60) / ((pat.bpm || song.bpm) * 4);
  }
  return seconds + TAIL;
}

let wasmBytes = null;
async function wasm() {
  if (!wasmBytes) {
    const r = await fetch('studio.wasm');
    if (!r.ok) throw new Error(`studio.wasm: ${r.status}`);
    wasmBytes = await r.arrayBuffer();
  }
  return wasmBytes.slice(0);   // a copy: the worker gets its own
}

// Renders the song: { left, right, rate, seconds, peak }. onProgress(0..1)
// now and then. sdImage: the LXR's SD card, as the live studio has it (its
// samples), or none for an empty card.
export async function renderSong(song, { sdImage = null, onProgress = () => {} } = {}) {
  const worker = new Worker(new URL('../render-worker.js', import.meta.url), { type: 'module' });
  const seconds = songSeconds(song);
  try {
    const result = await new Promise((resolve, reject) => {
      worker.onmessage = ({ data }) => {
        if (data.type === 'progress') onProgress(data.done);
        else if (data.type === 'done') resolve(data);
        else reject(new Error(data.message));
      };
      worker.onerror = (e) => reject(new Error(e.message || 'the render worker failed'));
      wasm().then((bytes) => worker.postMessage({ wasm: bytes, messages: songMessages(song), seconds, samples: true, sdImage },
        sdImage ? [bytes, sdImage] : [bytes]), reject);
    });
    if (result.nonFinite) throw new Error(`${result.nonFinite} samples were not finite`);
    return { left: result.left, right: result.right, rate: RATE, seconds, peak: result.peak };
  } finally {
    worker.terminate();
  }
}

// 24-bit stereo PCM, as a Blob for a download
export function wavFile(left, right, rate = RATE) {
  const frames = left.length;
  const data = frames * 6;
  const out = new DataView(new ArrayBuffer(44 + data));
  const text = (at, s) => [...s].forEach((c, i) => out.setUint8(at + i, c.charCodeAt(0)));
  text(0, 'RIFF'); out.setUint32(4, 36 + data, true); text(8, 'WAVEfmt ');
  out.setUint32(16, 16, true); out.setUint16(20, 1, true); out.setUint16(22, 2, true);
  out.setUint32(24, rate, true); out.setUint32(28, rate * 6, true); out.setUint16(32, 6, true); out.setUint16(34, 24, true);
  text(36, 'data'); out.setUint32(40, data, true);
  let at = 44;
  for (let i = 0; i < frames; i++) {
    for (const x of [left[i], right[i]]) {
      const v = Math.round(Math.max(-1, Math.min(1, x)) * 8388607);
      out.setUint8(at, v & 0xff); out.setUint8(at + 1, (v >> 8) & 0xff); out.setUint8(at + 2, (v >> 16) & 0xff);
      at += 3;
    }
  }
  return new Blob([out.buffer], { type: 'audio/wav' });
}
