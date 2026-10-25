// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The demo song every new studio starts with, so the tracker opens with
// something to play and look at (PLAN.md: "A tool opens in a working state").
// Two patterns at 128 BPM: the groove alone, then with VA's chords and lead.

import { newSong, newPattern, cellKey } from './song.js';

export function demoSong() {
  const s = newSong();
  s.name = 'First steps';
  s.bpm = 128;
  const put = (p, row, track, notes, vel = 0, retrig = 0) => {
    s.patterns[p].cells[cellKey(row, track)] = { notes: [].concat(notes), vel, retrig };
  };
  const off = (p, row, track) => { s.patterns[p].cells[cellKey(row, track)] = { off: true }; };
  s.patterns = [newPattern(32), newPattern(32)];
  s.patterns.forEach((pat) => { pat.bpm = 128; });
  for (const p of [0, 1]) {
    for (let r = 0; r < 32; r += 4) put(p, r, 0, 36, r % 16 === 0 ? 120 : 100);   // kick on the beat
    for (const r of [4, 12, 20, 28]) put(p, r, 3, 38);                             // snare on 2 and 4
    for (let r = 2; r < 32; r += 4) put(p, r, 5, 42, 80);                          // offbeat hats
    put(p, 30, 5, 42, 70, 3);                                                      // a hat roll into the next bar
    put(p, 14, 6, 46, 90);                                                         // open hat
    const bass = [33, 0, 45, 33, 0, 33, 45, 0, 31, 0, 43, 31, 0, 31, 43, 36];
    for (let r = 0; r < 32; r += 2) {
      const n = bass[(r / 2) % 16];
      if (n) put(p, r, 7, n, 100);                                                 // VA1 bass, eighths
    }
  }
  // the second pattern: chords on VA2, a lead on VA3
  put(1, 0, 8, [57, 60, 64], 90);
  off(1, 12, 8);
  put(1, 16, 8, [53, 57, 60], 90);
  off(1, 28, 8);
  [[0, 76], [3, 74], [6, 72], [8, 74], [12, 69], [16, 72], [19, 71], [22, 69], [24, 67]].forEach(([r, n]) => put(1, r, 9, n, 95));
  off(1, 30, 9);
  s.order = [0, 1, 0, 1];
  return s;
}
