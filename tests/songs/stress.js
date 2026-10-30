// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The worst case for the audio thread (PLAN.md Phase 7.2): not music, a load.
// Every part plays at its voice limit, all the time:
//   - the LXR: all seven drums on every row, rolled (4 hits a row);
//   - VA: the mono slots on every row; VA2 (6 voices) gets 8 notes a row from
//     two tracks (track 16 is routed to it too), so it steals;
//   - Dexed: both parts, a long pad, a new 4-note chord every row from four
//     chords of different notes, so all 16 voices of each part sound;
//   - Plaits: both parts, a 4-note chord every row (4 voices each);
//   - both sends on reverbs (hall and room), every channel sending to both,
//     and every channel's filter on (the costliest type, see FILTER).
// tests/run.sh renders it natively and in the browser (WebAssembly, the
// render worker) against CPU budgets, with the health checks.

import { T, song, put, lxrKit, channel, bus, chord } from '../../web/app/tracker/genres/build.js';
import { plaitsPreset } from '../../web/app/tracker/song.js';
import { CHANNELS } from '../../web/app/mixer/mix.js';

const FILTER = 4;   // the vintage ladder: measured the costliest of Surge's filters here
const CHORDS = ['C3 E3 G3 B3', 'D3 F3 A3 C4', 'E3 G3 B3 D4', 'F3 A3 C4 Eb4'].map(chord);
const DRUMS = [T.KICK, T.TOM, T.PERC, T.SNARE, T.CYM, T.HAT, T.OHAT];
const NOTES = { [T.KICK]: 36, [T.TOM]: 43, [T.PERC]: 50, [T.SNARE]: 38, [T.CYM]: 49, [T.HAT]: 42, [T.OHAT]: 46 };

export default function stress() {
  const s = song('Stress: every voice', 140, 1);
  const EXTRA = 15;   // track 16: a second track on VA2
  s.tracks[EXTRA] = { ...s.tracks[EXTRA], name: 'VA2 more', engine: 1, part: 1 };
  for (let row = 0; row < 64; row++) {
    const c = CHORDS[row % 4], next = CHORDS[(row + 2) % 4];
    for (const k of DRUMS) put(s, 0, row, k, NOTES[k], 110, 4);
    put(s, 0, row, T.BASS, 36 + (row % 12), 100, 2);
    put(s, 0, row, T.LEAD, 72 + (row % 7), 100, 2);
    put(s, 0, row, T.NOISE, 60, 100, 2);
    put(s, 0, row, T.POLY, c.map((x) => x + 12), 100);
    put(s, 0, row, EXTRA, next.map((x) => x + 24), 100);
    put(s, 0, row, T.DX1, c, 100);
    put(s, 0, row, T.DX2, next.map((x) => x + 12), 100);
    put(s, 0, row, T.PL1, c, 100);
    put(s, 0, row, T.PL2, next, 100);
  }
  s.order = [0, 0, 0];

  s.va = [{ params: null, preset: 1 }, { params: null, preset: 1 }, { params: null, preset: 3 }, { params: null, preset: 4 }];   // Acid, Supersaw, Super Lead, Wind
  s.dexed = [{ bank: 2, program: 4, volume: 90, transpose: 24 }, { bank: 2, program: 4, volume: 90, transpose: 24 }];   // OB8 PAD
  s.plaits = [plaitsPreset('Stringer'), plaitsPreset('Chord mem')];
  s.lxr = lxrKit();
  for (const { key } of CHANNELS) channel(s, key, { level: 60, sendA: 60, sendB: 60, filter: FILTER, cutoff: 90, reso: 40 });
  bus(s, 0, { type: 1 });   // hall
  bus(s, 1, { type: 2 });   // room
  s.mix.master = 70;
  return s;
}
