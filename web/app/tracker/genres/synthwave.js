// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The synthwave demo (PLAN.md Phase 6), 100 BPM in A minor, Am F C G: kick
// on 1 and 3, a big reverb snare on 2 and 4, eighth hats; VA pumps an octave
// bass, Plaits holds the string machine and stabs brass in the chorus, Dexed
// plays an FM Rhodes, VA sings a sync lead. Patterns: intro, verse, chorus,
// outro. Also a regression song (tests/run.sh).

import { T, n, chord, song, put, off, hits, seq, lxrKit, channel, bus } from './build.js';
import { plaitsPreset } from '../song.js';

const ROOTS = ['A1', 'F1', 'C2', 'G1'].map(n);
const CHORDS = ['A3 C4 E4', 'F3 A3 C4', 'G3 C4 E4', 'G3 B3 D4'].map(chord);

export default function synthwave() {
  const s = song('Synthwave: Neon Coast', 100, 4);
  const [INTRO, VERSE, CHORUS, OUTRO] = [0, 1, 2, 3];

  for (const p of [INTRO, VERSE, CHORUS, OUTRO]) {
    for (let bar = 0; bar < 4; bar++) {
      put(s, p, bar * 16, T.PL2, CHORDS[bar].map((x) => x + 12), 85);   // the strings
    }
    off(s, p, 63, T.PL2);
  }
  for (const p of [INTRO, VERSE, CHORUS]) {
    // the bass: eighths, root and octave
    for (let bar = 0; bar < 4; bar++)
      for (let e = 0; e < 8; e++) {
        const row = bar * 16 + e * 2;
        put(s, p, row, T.BASS, ROOTS[bar] + (e % 2 ? 12 : 0), e % 2 ? 90 : 110);
        off(s, p, row + 1, T.BASS);
      }
  }
  for (const p of [VERSE, CHORUS]) {
    hits(s, p, T.KICK, 'X.......x.......');
    hits(s, p, T.SNARE, '....X.......X...');
    hits(s, p, T.HAT, 'x.o.x.o.x.o.x.oo');
    // Dexed: the Rhodes on the off-beats
    for (let bar = 0; bar < 4; bar++)
      for (const step of [2, 6, 10, 14]) {
        put(s, p, bar * 16 + step, T.DX1, CHORDS[bar].map((x) => x + 12), 80);
        off(s, p, bar * 16 + step + 2, T.DX1);
      }
  }
  // the chorus: the sync lead and brass stabs
  const lead = ['E5', null, null, 'D5', 'C5', null, 'B4', null, 'C5', null, null, 'A4', null, null, 'G4', 'A4'];
  seq(s, CHORUS, T.LEAD, { step: 4, notes: lead.map((x) => x && n(x)), len: 4, vel: 100 });
  for (let bar = 0; bar < 4; bar++) {
    put(s, CHORUS, bar * 16 + 14, T.PL1, CHORDS[(bar + 1) % 4], 100);
    off(s, CHORUS, bar * 16 + 16 < 64 ? bar * 16 + 16 : 63, T.PL1);
  }
  hits(s, CHORUS, T.CYM, 'X' + '.'.repeat(63));
  hits(s, OUTRO, T.CYM, 'X' + '.'.repeat(63));

  s.order = [INTRO, VERSE, CHORUS, VERSE, CHORUS, OUTRO];

  s.va = [{ params: null, preset: 8 }, { params: null, preset: 0 }, { params: null, preset: 2 }, { params: null, preset: 7 }];
  s.plaits = [plaitsPreset('Poly brass'), plaitsPreset('Stringer')];
  s.dexed[0] = { bank: 2, program: 15, volume: 92, transpose: 24 };   // SynprezFM 02: FmRhodes18
  // a bigger, rounder kick; a fat snare with a long tail
  s.lxr = lxrKit({ PAR_VELOD1: 40, PAR_COARSE1: 28, PAR_VELOD4: 30, PAR_COARSE4: 40, PAR_FILTER_FREQ_6: 96 });

  channel(s, 'lxr4', { sendA: 95 });
  channel(s, 'lxr1', { sendA: 20 });
  channel(s, 'pl2', { level: 82, sendA: 70, filter: 2, cutoff: 100 });
  channel(s, 'pl1', { level: 86, sendA: 50 });
  channel(s, 'dx1', { level: 82, pan: 46, sendA: 40, sendB: 35 });
  channel(s, 'va1', { level: 84 });
  channel(s, 'va3', { level: 78, pan: 70, sendA: 50, sendB: 60 });
  bus(s, 0, { fx: { OM_FX_HALL_DECAY: 96, OM_FX_HALL_SIZE: 100 } });
  bus(s, 1, { fx: { OM_FX_DELAY_LEFT: 9, OM_FX_DELAY_RIGHT: 11, OM_FX_DELAY_FEEDBACK: 45 } });
  return s;
}
