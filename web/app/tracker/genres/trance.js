// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The trance demo (PLAN.md Phase 6), 138 BPM in A minor, Am F C G a bar each:
// a rolling off-beat bass, four-on-the-floor, off-beat open hats; Plaits
// plucks an arpeggio and holds the pad, VA leads, Dexed rings glass bells and
// VA4 rises in the breakdown. Patterns: intro, main, breakdown, drive.
// Also a regression song (tests/run.sh).

import { T, n, chord, song, put, off, hits, seq, lxrKit, channel, bus } from './build.js';
import { plaitsPreset } from '../song.js';

const ROOTS = ['A1', 'F1', 'C2', 'G1'].map(n);   // a bar each
const CHORDS = ['A3 C4 E4 A4', 'F3 A3 C4 F4', 'C4 E4 G4 C5', 'G3 B3 D4 G4'].map(chord);
const ARP = [[0, 1, 2, 1], [0, 1, 2, 3]];           // chord tones, per 16th

export default function trance() {
  const s = song('Trance: Dawn Run', 138, 4);
  const [INTRO, MAIN, BREAK, DRIVE] = [0, 1, 2, 3];

  for (const p of [INTRO, MAIN, DRIVE]) {
    hits(s, p, T.KICK, 'X...x...x...x...');
    hits(s, p, T.OHAT, '..x...x...x...x.');
    hits(s, p, T.HAT, 'o.o.o.o.o.o.o.oo');
    // the rolling bass: the three 16ths after each beat
    for (let bar = 0; bar < 4; bar++)
      for (let beat = 0; beat < 4; beat++)
        for (let k = 1; k < 4; k++) {
          const row = bar * 16 + beat * 4 + k;
          put(s, p, row, T.BASS, ROOTS[bar], k === 1 ? 110 : 90);
          off(s, p, row + 1, T.BASS);
        }
  }
  for (const p of [MAIN, DRIVE]) {
    hits(s, p, T.SNARE, '....x.......x...');
    for (let bar = 0; bar < 4; bar++) {
      // Plaits pluck: an arpeggio an octave up; the pad holds the chord
      for (let i = 0; i < 16; i++) {
        const tones = CHORDS[bar];
        put(s, p, bar * 16 + i, T.PL1, tones[ARP[bar % 2][i % 4]] + 12, i % 4 === 0 ? 110 : 85);
      }
      put(s, p, bar * 16, T.PL2, CHORDS[bar], 90);
    }
    off(s, p, 63, T.PL2);
  }
  hits(s, INTRO, T.CYM, 'X' + '.'.repeat(63));
  hits(s, DRIVE, T.CYM, 'X' + '.'.repeat(31));

  // the breakdown: the pad alone, bells, and a riser into the drive
  for (let bar = 0; bar < 4; bar++) put(s, BREAK, bar * 16, T.PL2, CHORDS[bar], 100);
  off(s, BREAK, 63, T.PL2);
  seq(s, BREAK, T.DX1, { step: 6, notes: ['E5', 'A5', 'C6', 'B5', 'G5', 'E5', null, 'A5', 'C6', 'E6'].map((x) => x && n(x)), len: 5, vel: 90 });
  put(s, BREAK, 32, T.NOISE, n('C4'), 100);
  off(s, BREAK, 63, T.NOISE);

  // the drive: a lead over everything
  const lead = ['A4', null, 'C5', 'E5', 'D5', null, 'C5', 'B4', 'C5', null, 'A4', 'G4', 'A4', null, null, null];
  seq(s, DRIVE, T.LEAD, { step: 2, notes: lead.map((x) => x && n(x)), len: 2, vel: 100 });

  s.order = [INTRO, MAIN, BREAK, DRIVE, DRIVE];

  // the sounds
  s.va = [{ params: null, preset: 0 }, { params: null, preset: 1 }, { params: null, preset: 3 }, { params: null, preset: 5 }];
  s.plaits = [plaitsPreset('Pluck'), plaitsPreset('Trance pad')];
  s.dexed[0] = { bank: 1, program: 8, volume: 95, transpose: 24 };   // SynprezFM 01: glassbell
  s.lxr = lxrKit({ PAR_VELOD1: 30, PAR_MODAMNT1: 60, PAR_COARSE1: 30, PAR_FILTER_FREQ_6: 115, PAR_VELOD4: 18 });

  // the mix: the pluck into the delay, the pad, bells and lead into the hall
  channel(s, 'pl1', { level: 88, pan: 56, sendB: 70 });
  channel(s, 'pl2', { level: 84, sendA: 80, filter: 1, cutoff: 105 });
  channel(s, 'dx1', { level: 86, pan: 76, sendA: 90, sendB: 50 });
  channel(s, 'va1', { level: 82, filter: 2, cutoff: 92, reso: 30 });
  channel(s, 'va3', { level: 76, sendA: 60, sendB: 55 });
  channel(s, 'va4', { level: 70, sendA: 70 });
  channel(s, 'lxr4', { sendA: 40 });
  bus(s, 0, { fx: { OM_FX_HALL_DECAY: 80 } });
  bus(s, 1, { fx: { OM_FX_DELAY_LEFT: 8, OM_FX_DELAY_RIGHT: 9, OM_FX_DELAY_FEEDBACK: 50 } });
  return s;
}
