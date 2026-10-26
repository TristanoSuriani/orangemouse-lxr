// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The dub techno demo (PLAN.md Phase 6), 122 BPM in D minor: a deep
// four-on-the-floor, off-beat hats, a rim now and then; Plaits stabs its
// chord engine through a low-pass into a long dotted delay and a hall, and
// plays the sub bass; VA4 hisses static under it all, Dexed adds a pad at the
// end. Patterns: groove, chords, sub, variation. Also a regression song.

import { T, n, chord, song, put, off, hits, lxrKit, channel, bus } from './build.js';
import { plaitsPreset } from '../song.js';

export default function dubtechno() {
  const s = song('Dub Techno: Grey Water', 122, 4);
  const [GROOVE, CHORDS, SUB, VARIATION] = [0, 1, 2, 3];

  for (const p of [GROOVE, CHORDS, SUB, VARIATION]) {
    hits(s, p, T.KICK, 'x...x...x...x...');
    hits(s, p, T.HAT, '..x...x...x...x.');
    hits(s, p, T.PERC, '.............o..' + '.'.repeat(16) + '...o............' + '.'.repeat(16));
    put(s, p, 0, T.NOISE, n('C4'), 60);   // the static under it all
  }
  for (const p of [CHORDS, SUB, VARIATION]) {
    // the chord stab: one note, the chord engine plays the chord; the delay does the rest
    const stab = p === VARIATION ? [2, 11, 26, 42, 51] : [2, 26, 42];
    for (let i = 0; i < stab.length; i++) {
      put(s, p, stab[i], T.PL1, n(i % 3 === 2 ? 'F3' : 'D3'), 100);
      off(s, p, stab[i] + 1, T.PL1);
    }
  }
  for (const p of [SUB, VARIATION]) {
    // the sub: off-beat D, a step down to C at the end of each two bars
    for (let beat = 0; beat < 16; beat++) {
      const row = beat * 4 + 2;
      put(s, p, row, T.PL2, n(beat % 8 >= 6 ? 'C1' : 'D1'), 100);
      off(s, p, row + 2, T.PL2);
    }
  }
  put(s, VARIATION, 0, T.DX1, chord('D3 F3 A3 C4'), 70);
  off(s, VARIATION, 63, T.DX1);

  s.order = [GROOVE, CHORDS, SUB, VARIATION, SUB, VARIATION];

  s.va = [{ params: null, preset: 3 }, { params: null, preset: 0 }, { params: null, preset: 0 }, { params: null, preset: 8 }];
  s.plaits = [plaitsPreset('Dub chord'), plaitsPreset('Sub bass')];
  s.dexed[0] = { bank: 2, program: 4, volume: 80, transpose: 24 };   // SynprezFM 02: OB8 PAD
  // a deep, soft kick; dark short hats
  s.lxr = lxrKit({ PAR_COARSE1: 26, PAR_VELOD1: 48, PAR_MODAMNT1: 34, PAR_FILTER_FREQ_6: 84, PAR_COARSE3: 64, PAR_VELOD3: 6 });

  // the dub chain: the chord through a low-pass, into a long dotted delay and the hall
  channel(s, 'pl1', { level: 90, filter: 2, cutoff: 80, reso: 40, sendA: 60, sendB: 100 });
  channel(s, 'pl2', { level: 96 });
  channel(s, 'va4', { level: 52, filter: 8, cutoff: 70 });
  channel(s, 'dx1', { level: 74, sendA: 80, filter: 1, cutoff: 86 });
  channel(s, 'lxr3', { pan: 40, sendB: 80 });
  channel(s, 'lxr6', { level: 88, sendB: 30 });
  bus(s, 0, { fx: { OM_FX_HALL_DECAY: 100, OM_FX_HALL_HFD: 70 } });
  bus(s, 1, { ret: 96, fx: { OM_FX_DELAY_LEFT: 8, OM_FX_DELAY_RIGHT: 11, OM_FX_DELAY_FEEDBACK: 80, OM_FX_DELAY_CROSS: 30,
    OM_FX_DELAY_HICUT: 55, OM_FX_DELAY_LOCUT: 70 } });
  return s;
}
