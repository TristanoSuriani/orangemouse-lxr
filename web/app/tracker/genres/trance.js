// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The trance demo, version 3 (PLAN.md 10.6), 138 BPM in A minor, Am F C G a
// bar each, with the tools trance needs, which versions 1 and 2 had to fake:
//   - supersaw chords held a bar each, chopped by the mixer's trance gate on
//     VA2 (a 16th pattern in time with the song), not by short notes;
//   - the pump: the pads, the bass and the string pad duck under the kick;
//   - the build opens the pads' filter with the effect column (a cutoff
//     slide over the 4 bars), the breakdown sweeps the pad's;
//   - the anthem (VA3's Super Lead) glides into some of its notes;
//   - conditions vary the open hats and add ghost snares, so the drops change.
// Patterns: intro, build, breakdown, drop. Also a regression song (tests/run.sh).

import { T, n, chord, song, put, off, hits, lxrKit, channel, bus, effect, gate } from './build.js';
import { plaitsPreset } from '../song.js';
import { cellKey } from '../song.js';

export const VERSION = 1;   // of genre-trance-3

const ROOTS = ['A1', 'F1', 'C2', 'G1'].map(n);   // a bar each
const CHORDS = ['A3 C4 E4 A4', 'F3 A3 C4 F4', 'G3 C4 E4 G4', 'G3 B3 D4 G4'].map(chord);
// the anthem: [row in the 4 bars, note, rows held, glides in]
const ANTHEM = [
  [0, 'E5', 6], [6, 'D5', 2, true], [8, 'E5', 4, true], [12, 'A5', 4],
  [16, 'G5', 6], [22, 'F5', 2, true], [24, 'E5', 4, true], [28, 'C5', 4],
  [32, 'E5', 6], [38, 'D5', 2, true], [40, 'C5', 4, true], [44, 'G4', 4],
  [48, 'B4', 4], [52, 'C5', 4, true], [56, 'D5', 8, true],
];

function drums(s, p, { snare = true } = {}) {
  hits(s, p, T.KICK, 'X...x...x...x...');
  hits(s, p, T.HAT, 'o.o.o.o.o.o.o.oo');
  hits(s, p, T.OHAT, '..x...x...x...x.');
  // every other pass, the last open hat of each bar is left out: the loop breathes
  for (let bar = 0; bar < 4; bar++) s.patterns[p].cells[cellKey(bar * 16 + 14, T.OHAT)].cond = 10;   // 1:2
  if (snare) {
    hits(s, p, T.SNARE, '....x.......x...');
    for (let bar = 0; bar < 4; bar++) {   // a ghost before the backbeat, half the time
      put(s, p, bar * 16 + 11, T.SNARE, 38, 45);
      s.patterns[p].cells[cellKey(bar * 16 + 11, T.SNARE)].cond = 5;
    }
  }
}

// the rolling bass: the three 16ths after each beat, short
function bass(s, p) {
  for (let bar = 0; bar < 4; bar++)
    for (let beat = 0; beat < 4; beat++)
      for (let k = 1; k < 4; k++) {
        put(s, p, bar * 16 + beat * 4 + k, T.BASS, ROOTS[bar], k === 1 ? 115 : 92);
        s.patterns[p].cells[cellKey(bar * 16 + beat * 4 + k, T.BASS)].len = 3;   // three quarters of a row
      }
}

// the supersaw chords, a bar each (the gate chops them)
function pads(s, p, vel = 100) {
  for (let bar = 0; bar < 4; bar++) put(s, p, bar * 16, T.POLY, CHORDS[bar], vel);
  off(s, p, 63, T.POLY);
}

function anthem(s, p, track) {
  for (const [row, note, len, glide] of ANTHEM) {
    put(s, p, row, track, n(note), 105);
    const c = s.patterns[p].cells[cellKey(row, track)];
    c.len = len * 4;
    if (glide) c.glide = true;
  }
}

// the arpeggio: chord tones up an octave, a 16th each, plucked short
function arp(s, p) {
  for (let bar = 0; bar < 4; bar++)
    for (let i = 0; i < 16; i++) {
      put(s, p, bar * 16 + i, T.PL1, CHORDS[bar][[0, 2, 1, 3][i % 4]] + 12, i % 4 === 0 ? 100 : 80);
      s.patterns[p].cells[cellKey(bar * 16 + i, T.PL1)].len = 2;
    }
}

export default function trance() {
  const s = song('Trance: Dawn Run 3', 138, 4);
  const [INTRO, BUILD, BREAK, DROP] = [0, 1, 2, 3];

  // intro: drums and bass, the pads far back behind a closed filter
  drums(s, INTRO, { snare: false });
  bass(s, INTRO);
  pads(s, INTRO, 70);
  effect(s, INTRO, 0, T.POLY, 'cutoff', 30);
  hits(s, INTRO, T.CYM, 'X' + '.'.repeat(63));

  // build: the gated pads open up over the 4 bars, the arpeggio joins
  drums(s, BUILD);
  bass(s, BUILD);
  pads(s, BUILD, 90);
  effect(s, BUILD, 0, T.POLY, 'cutoff', 30, true);
  effect(s, BUILD, 63, T.POLY, 'cutoff', 127);
  arp(s, BUILD);

  // breakdown: no kick, no bass; the string pad swept, the anthem alone, bells; then the roll and the riser
  for (let bar = 0; bar < 4; bar++) put(s, BREAK, bar * 16, T.PL2, CHORDS[bar], 95);
  off(s, BREAK, 63, T.PL2);
  effect(s, BREAK, 0, T.PL2, 'cutoff', 50, true);
  effect(s, BREAK, 32, T.PL2, 'cutoff', 120, true);
  effect(s, BREAK, 63, T.PL2, 'cutoff', 127);
  anthem(s, BREAK, T.LEAD);
  for (const [row, note] of [[4, 'E6'], [20, 'C6'], [36, 'G5'], [44, 'B5']]) {
    put(s, BREAK, row, T.DX1, n(note), 85);
    off(s, BREAK, row + 8, T.DX1);
  }
  put(s, BREAK, 32, T.NOISE, n('C4'), 100);      // the riser, over the last two bars
  off(s, BREAK, 63, T.NOISE);
  for (let row = 32; row < 40; row += 4) put(s, BREAK, row, T.SNARE, 38, 80);
  for (let row = 40; row < 48; row += 2) put(s, BREAK, row, T.SNARE, 38, 90);
  for (let row = 48; row < 56; row++) put(s, BREAK, row, T.SNARE, 38, 100);
  for (let row = 56; row < 64; row++) put(s, BREAK, row, T.SNARE, 38, 110 + (row - 56) * 2, 2);

  // the drop: everything; the anthem on top, the pads open and pumping
  drums(s, DROP);
  bass(s, DROP);
  pads(s, DROP, 110);
  effect(s, DROP, 0, T.POLY, 'cutoff', 127);
  arp(s, DROP);
  anthem(s, DROP, T.LEAD);
  for (let bar = 0; bar < 4; bar++) put(s, DROP, bar * 16, T.PL2, CHORDS[bar], 80);
  off(s, DROP, 63, T.PL2);
  hits(s, DROP, T.CYM, 'X' + '.'.repeat(31));

  s.order = [INTRO, BUILD, BREAK, DROP, DROP];

  // the sounds: VA's Classic bass, Supersaw, Super Lead (legato), Riser
  s.va = [{ params: null, preset: 0 }, { params: null, preset: 1 }, { params: null, preset: 3 }, { params: null, preset: 5 }];
  s.plaits = [plaitsPreset('Pluck'), plaitsPreset('Trance pad')];
  s.dexed[0] = { bank: 1, program: 8, volume: 95, transpose: 24 };   // SynprezFM 01: glassbell
  s.lxr = lxrKit({ PAR_VELOD1: 28, PAR_MODAMNT1: 66, PAR_MOD_EG1: 22, PAR_COARSE1: 30, PAR_FILTER_FREQ_6: 118,
    PAR_VELOD4: 16, PAR_COARSE4: 50 });

  // the mix. The pads: a 24 dB low-pass (the effects move it), the gate, the pump from the kick
  channel(s, 'va2', { level: 92, sendA: 70, sendB: 45, filter: 2, cutoff: 127, reso: 20, duck: 80, duckKey: 0, duckRel: 55 });
  gate(s, 'va2', { depth: 120, pattern: 'x.xx.xx.x.xx.xx.', smooth: 30 });
  channel(s, 'va1', { level: 84, filter: 2, cutoff: 90, reso: 25, duck: 55, duckKey: 0, duckRel: 40 });
  channel(s, 'va3', { level: 90, sendA: 65, sendB: 70 });
  channel(s, 'va4', { level: 76, sendA: 70 });
  channel(s, 'pl1', { level: 70, pan: 44, sendB: 80, filter: 1, cutoff: 100 });
  channel(s, 'pl2', { level: 80, sendA: 85, filter: 2, cutoff: 127, duck: 70, duckKey: 0, duckRel: 62 });
  channel(s, 'dx1', { level: 80, pan: 84, sendA: 90, sendB: 60 });
  channel(s, 'lxr4', { level: 92, sendA: 45 });
  channel(s, 'lxr6', { eqHigh: 72 });   // the hats a touch brighter
  bus(s, 0, { fx: { OM_FX_HALL_DECAY: 84, OM_FX_HALL_SIZE: 96, OM_FX_HALL_LFD: 60 } });
  bus(s, 1, { fx: { OM_FX_DELAY_LEFT: 8, OM_FX_DELAY_RIGHT: 9, OM_FX_DELAY_FEEDBACK: 55, OM_FX_DELAY_CROSS: 30 } });
  return s;
}
