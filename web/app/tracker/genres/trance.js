// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The trance demo (PLAN.md Phase 6), 138 BPM in A minor, Am F C G a bar
// each. What makes it trance: supersaw chords chopped by a 16th-note gate
// (VA2's Supersaw), an anthem lead (VA3's Super Lead) heard first in the
// breakdown and then over the drop, a rolling off-beat bass, and a build: the
// breakdown's snare roll speeds up under a riser into the drop.
// Patterns: intro, build, breakdown, drop. Also a regression song (tests/run.sh).
// Version 2 (2026-10-26): version 1 had a pluck arpeggio and a thin pad, and
// did not sound like trance.

import { T, n, chord, song, put, off, hits, lxrKit, channel, bus } from './build.js';
import { plaitsPreset } from '../song.js';

export const VERSION = 2;

const ROOTS = ['A1', 'F1', 'C2', 'G1'].map(n);   // a bar each
const CHORDS = ['A3 C4 E4 A4', 'F3 A3 C4 F4', 'G3 C4 E4 G4', 'G3 B3 D4 G4'].map(chord);
const GATE = 'x.xx.xx.x.xx.x.x';                // the trance gate, a bar of 16ths
// the anthem: [row in the 4 bars, note, rows held]
const ANTHEM = [
  [0, 'E5', 6], [6, 'D5', 2], [8, 'E5', 4], [12, 'A5', 4],
  [16, 'G5', 6], [22, 'F5', 2], [24, 'E5', 4], [28, 'C5', 4],
  [32, 'E5', 6], [38, 'D5', 2], [40, 'C5', 4], [44, 'G4', 4],
  [48, 'B4', 4], [52, 'C5', 4], [56, 'D5', 8],
];

function drums(s, p, { snare = true } = {}) {
  hits(s, p, T.KICK, 'X...x...x...x...');
  hits(s, p, T.OHAT, '..x...x...x...x.');
  hits(s, p, T.HAT, 'o.o.o.o.o.o.o.oo');
  if (snare) hits(s, p, T.SNARE, '....x.......x...');
}

// the rolling bass: the three 16ths after each beat
function bass(s, p) {
  for (let bar = 0; bar < 4; bar++)
    for (let beat = 0; beat < 4; beat++)
      for (let k = 1; k < 4; k++) {
        const row = bar * 16 + beat * 4 + k;
        put(s, p, row, T.BASS, ROOTS[bar], k === 1 ? 115 : 92);
        off(s, p, row + 1, T.BASS);
      }
}

// the supersaw chords, through the gate
function gate(s, p, vel = 100) {
  for (let bar = 0; bar < 4; bar++)
    for (let i = 0; i < 16; i++)
      if (GATE[i] === 'x') {
        put(s, p, bar * 16 + i, T.POLY, CHORDS[bar], i % 4 === 0 ? vel : vel - 15);
        off(s, p, bar * 16 + i + 1, T.POLY);
      }
}

function anthem(s, p, track, octave = 0) {
  for (const [row, note, len] of ANTHEM) {
    put(s, p, row, track, n(note) + 12 * octave, 105);
    off(s, p, row + len, track);
  }
}

// the arpeggio: chord tones up an octave, a 16th each
function arp(s, p) {
  for (let bar = 0; bar < 4; bar++)
    for (let i = 0; i < 16; i++) {
      const tones = CHORDS[bar];
      put(s, p, bar * 16 + i, T.PL1, tones[[0, 2, 1, 3][i % 4]] + 12, i % 4 === 0 ? 100 : 80);
    }
}

export default function trance() {
  const s = song('Trance: Dawn Run', 138, 4);
  const [INTRO, BUILD, BREAK, DROP] = [0, 1, 2, 3];

  // intro: drums and bass
  drums(s, INTRO, { snare: false });
  bass(s, INTRO);
  hits(s, INTRO, T.CYM, 'X' + '.'.repeat(63));

  // build: the gate comes in quietly, the arpeggio joins
  drums(s, BUILD);
  bass(s, BUILD);
  gate(s, BUILD, 80);
  arp(s, BUILD);

  // breakdown: no kick, no bass; the pad, the anthem alone, bells; then the roll and the riser
  for (let bar = 0; bar < 4; bar++) put(s, BREAK, bar * 16, T.PL2, CHORDS[bar], 95);
  off(s, BREAK, 63, T.PL2);
  anthem(s, BREAK, T.LEAD);
  for (const [row, note] of [[4, 'E6'], [20, 'C6'], [36, 'G5'], [44, 'B5']]) {
    put(s, BREAK, row, T.DX1, n(note), 85);
    off(s, BREAK, row + 8, T.DX1);
  }
  put(s, BREAK, 32, T.NOISE, n('C4'), 100);      // the riser, over the last two bars
  off(s, BREAK, 63, T.NOISE);
  // the roll: quarters, eighths, sixteenths, then a 32nd roll in the last bar
  for (let row = 32; row < 40; row += 4) put(s, BREAK, row, T.SNARE, 38, 80);
  for (let row = 40; row < 48; row += 2) put(s, BREAK, row, T.SNARE, 38, 90);
  for (let row = 48; row < 56; row++) put(s, BREAK, row, T.SNARE, 38, 100);
  for (let row = 56; row < 64; row++) put(s, BREAK, row, T.SNARE, 38, 110 + (row - 56) * 2, 2);

  // the drop: everything; the anthem on top
  drums(s, DROP);
  bass(s, DROP);
  gate(s, DROP, 110);
  arp(s, DROP);
  anthem(s, DROP, T.LEAD);
  for (let bar = 0; bar < 4; bar++) put(s, DROP, bar * 16, T.PL2, CHORDS[bar], 80);
  off(s, DROP, 63, T.PL2);
  hits(s, DROP, T.CYM, 'X' + '.'.repeat(31));

  s.order = [INTRO, BUILD, BREAK, DROP, DROP];

  // the sounds: VA's Classic bass, Supersaw, Super Lead, Riser
  s.va = [{ params: null, preset: 0 }, { params: null, preset: 1 }, { params: null, preset: 3 }, { params: null, preset: 5 }];
  s.plaits = [plaitsPreset('Pluck'), plaitsPreset('Trance pad')];
  s.dexed[0] = { bank: 1, program: 8, volume: 95, transpose: 24 };   // SynprezFM 01: glassbell
  // a short, punchy kick with a click; a brighter snare for the roll
  s.lxr = lxrKit({ PAR_VELOD1: 28, PAR_MODAMNT1: 66, PAR_MOD_EG1: 22, PAR_COARSE1: 30, PAR_FILTER_FREQ_6: 118,
    PAR_VELOD4: 16, PAR_COARSE4: 50 });

  // the mix: supersaws wide in the hall and the delay; the lead out front, delayed;
  // the arp in the delay under them; the bass tight under a low-pass
  channel(s, 'va2', { level: 92, sendA: 70, sendB: 45 });
  channel(s, 'va3', { level: 90, sendA: 65, sendB: 70 });
  channel(s, 'va1', { level: 84, filter: 2, cutoff: 90, reso: 25 });
  channel(s, 'va4', { level: 76, sendA: 70 });
  channel(s, 'pl1', { level: 70, pan: 44, sendB: 80, filter: 1, cutoff: 100 });
  channel(s, 'pl2', { level: 80, sendA: 85 });
  channel(s, 'dx1', { level: 80, pan: 84, sendA: 90, sendB: 60 });
  channel(s, 'lxr4', { level: 92, sendA: 45 });
  bus(s, 0, { fx: { OM_FX_HALL_DECAY: 84, OM_FX_HALL_SIZE: 96, OM_FX_HALL_LFD: 60 } });
  bus(s, 1, { fx: { OM_FX_DELAY_LEFT: 8, OM_FX_DELAY_RIGHT: 9, OM_FX_DELAY_FEEDBACK: 55, OM_FX_DELAY_CROSS: 30 } });
  return s;
}
