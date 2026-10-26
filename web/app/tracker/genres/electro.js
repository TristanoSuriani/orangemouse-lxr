// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The electro demo (PLAN.md Phase 6), 125 BPM in F minor: a syncopated
// 808-style kick, a clap on 2 and 4, busy 16th hats and a cowbell-ish drum 3;
// Plaits plays the FM bass and the robot voice, Dexed stabs a clavinet, VA
// leads with a square. Patterns: beat, stabs and voice, lead.
// Also a regression song (tests/run.sh).

import { T, n, song, put, off, hits, seq, lxrKit, channel, bus } from './build.js';
import { plaitsPreset } from '../song.js';

export default function electro() {
  const s = song('Electro: Signal Lost', 125, 3);
  const [BEAT, STABS, LEAD] = [0, 1, 2];

  for (const p of [BEAT, STABS, LEAD]) {
    hits(s, p, T.KICK, 'X.....x...x.....X.....x..x..x...');
    hits(s, p, T.SNARE, '....x.......x...');
    hits(s, p, T.HAT, 'xoxoxoxoxoxoxrxo');
    hits(s, p, T.PERC, '...x......x..x.....x......x...x.');
    // the bass: F minor, syncopated, a note off before each next note
    const bass = [['F1', 0], ['F1', 3], ['Ab1', 6], ['F1', 10], ['C2', 12], ['Eb1', 14]];
    for (let bar = 0; bar < 4; bar++)
      for (const [note, step] of bass) {
        const row = bar * 16 + step;
        put(s, p, row, T.PL1, n(note) + (bar === 3 && step >= 12 ? 2 : 0), step === 0 ? 120 : 95);
        off(s, p, row + 2, T.PL1);
      }
  }
  for (const p of [STABS, LEAD]) {
    // Dexed: off-beat clavinet stabs of Fm7
    for (let bar = 0; bar < 4; bar++)
      for (const step of [2, 7, 10]) {
        put(s, p, bar * 16 + step, T.DX1, ['F3', 'Ab3', 'C4', 'Eb4'].map(n), 90);
        off(s, p, bar * 16 + step + 1, T.DX1);
      }
    // the robot voice, every two bars
    put(s, p, 4, T.PL2, n('F3'), 110);
    off(s, p, 12, T.PL2);
    put(s, p, 36, T.PL2, n('Ab3'), 110);
    off(s, p, 44, T.PL2);
  }
  const lead = ['C5', null, 'C5', 'Eb5', null, 'F5', null, 'Eb5', 'C5', null, 'Bb4', null, 'C5', null, null, null];
  seq(s, LEAD, T.LEAD, { step: 2, notes: lead.map((x) => x && n(x)), len: 1, vel: 100 });
  hits(s, LEAD, T.CYM, 'X' + '.'.repeat(63));

  s.order = [BEAT, STABS, STABS, LEAD, LEAD];

  s.va = [{ params: null, preset: 0 }, { params: null, preset: 0 }, { params: null, preset: 1 }, { params: null, preset: 3 }];
  s.plaits = [plaitsPreset('FM bass'), plaitsPreset('Robot vox')];
  s.dexed[0] = { bank: 2, program: 28, volume: 90, transpose: 24 };   // SynprezFM 02: POWERCLAV
  // an 808-ish kick: lower, longer, less click; a drier, snappier snare
  s.lxr = lxrKit({ PAR_COARSE1: 24, PAR_VELOD1: 72, PAR_MODAMNT1: 28, PAR_MOD_EG1: 40, PAR_VELOD4: 14, PAR_COARSE4: 52,
    PAR_COARSE3: 70, PAR_VELOD3: 10 });

  channel(s, 'pl1', { level: 96, filter: 2, cutoff: 98, reso: 20 });
  channel(s, 'pl2', { level: 90, sendB: 60, sendA: 40 });
  channel(s, 'dx1', { level: 80, pan: 44, sendB: 45 });
  channel(s, 'va3', { level: 74, pan: 80, sendB: 50 });
  channel(s, 'lxr3', { level: 80, pan: 84 });
  channel(s, 'lxr4', { sendA: 30 });
  bus(s, 0, { type: 2, fx: { OM_FX_ROOM_DECAY: 50 } });   // a room
  bus(s, 1, { fx: { OM_FX_DELAY_LEFT: 6, OM_FX_DELAY_RIGHT: 8, OM_FX_DELAY_FEEDBACK: 40, OM_FX_DELAY_CROSS: 40 } });
  return s;
}
