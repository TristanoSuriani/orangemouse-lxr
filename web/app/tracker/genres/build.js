// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// Helpers for writing songs in code: the genre demos (PLAN.md Phase 6), which
// are also the regression songs (tests/run.sh renders them natively through
// tools/song_stream.mjs). A song made here is an ordinary song: it opens in
// the tracker, and every instrument's sound is in it.

import { newSong, newPattern, cellKey, FX_NAMES } from '../song.js';
import { setGateSteps } from '../../mixer/mix.js';
import lxrRegistry from '../../registry/lxr.js';

// the tracks, as web/port/web_studio.c routes them
export const T = { KICK: 0, TOM: 1, PERC: 2, SNARE: 3, CYM: 4, HAT: 5, OHAT: 6,
  BASS: 7, POLY: 8, LEAD: 9, NOISE: 10, DX1: 11, DX2: 12, PL1: 13, PL2: 14 };
// the LXR's notes as the demos play them
export const DRUM_NOTE = { [T.KICK]: 36, [T.TOM]: 43, [T.PERC]: 50, [T.SNARE]: 38, [T.CYM]: 49, [T.HAT]: 42, [T.OHAT]: 46 };

const NAMES = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };
// 'A1', 'C#4', 'Bb2' -> a MIDI note (C4 = 60)
export function n(name) {
  const m = /^([A-G])([#b]?)(-?\d)$/.exec(name);
  if (!m) throw new Error(`not a note: ${name}`);
  return 12 * (Number(m[3]) + 1) + NAMES[m[1]] + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0);
}
// a chord from note names: chord('A3 C4 E4')
export const chord = (names) => names.split(/\s+/).map(n);

export function song(name, bpm, patterns, rows = 64) {
  const s = newSong();
  s.name = name;
  s.bpm = bpm;
  s.patterns = Array.from({ length: patterns }, () => ({ ...newPattern(rows), bpm }));
  return s;
}

export function put(s, p, row, track, notes, vel = 0, retrig = 0) {
  s.patterns[p].cells[cellKey(row, track)] = { notes: [].concat(notes), vel, retrig };
}
export function off(s, p, row, track) {
  if (row < s.patterns[p].rows) s.patterns[p].cells[cellKey(row, track)] = { off: true };
}

// Drum hits from a string, repeated over the pattern: x 100, X 127, o 70
// (a ghost), r a roll (3 hits), . nothing.
export function hits(s, p, track, pattern, note = DRUM_NOTE[track]) {
  const vel = { x: 100, X: 127, o: 70, r: 90 };
  for (let row = 0; row < s.patterns[p].rows; row++) {
    const c = pattern[row % pattern.length];
    if (c in vel) put(s, p, row, track, note, vel[c], c === 'r' ? 3 : 0);
  }
}

// A line: notes (a number, a chord array, or null for a rest) one every
// `step` rows from `from`, each held `len` rows (then a note off), cycling.
export function seq(s, p, track, { from = 0, step = 1, notes, len = step, vel = 0, until }) {
  const end = until ?? s.patterns[p].rows;
  for (let row = from, i = 0; row < end; row += step, i++) {
    const note = notes[i % notes.length];
    if (note === null) continue;
    put(s, p, row, track, note, vel);
    if (len < step) off(s, p, row + len, track);
  }
}

// The LXR's kit: its registry defaults with some parameters changed, by name
// (lxrKit({ PAR_VELOD1: 70 })), as song.lxr keeps it.
export function lxrKit(changes = {}) {
  const ids = Object.fromEntries(lxrRegistry.params.map((p, i) => [p.id, i]));
  const kit = lxrRegistry.params.map((p) => p.default);
  for (const [id, v] of Object.entries(changes)) {
    if (!(id in ids)) throw new Error(`no LXR parameter ${id}`);
    kit[ids[id]] = v;
  }
  return kit;
}

// An effect on a cell (PLAN.md 10.1), kept with any note there: one of the
// track's mixer controls ('level', 'pan', 'send A', 'send B', 'cutoff',
// 'resonance') set to value at the row; slide: glide to the track's next one.
export function effect(s, p, row, track, control, value, slide = false) {
  const fx = FX_NAMES.findIndex((f) => f?.name === control);
  if (fx < 1) throw new Error(`no effect ${control}`);
  const key = cellKey(row, track);
  const c = s.patterns[p].cells[key] ?? { notes: [] };
  s.patterns[p].cells[key] = { ...c, fx, fxv: value, ...(slide ? { fxs: true } : {}) };
}

// a mixer channel's trance gate (PLAN.md 9.2): depth 0..127, a pattern of
// 16 x and ., smoothing (registry/mix.json)
export function gate(s, key, { depth, pattern, smooth = 47 }) {
  const ch = s.mix.channels[key];
  Object.assign(ch, { gate: depth, gateSmooth: smooth });
  setGateSteps(ch, [...pattern].map((x) => x === 'x'));
}

// a mixer channel's settings (song.mix.channels[key])
export function channel(s, key, fields) {
  Object.assign(s.mix.channels[key], fields);
}

// a send effect's settings by registry id (registry/fx.json), and its type
export function bus(s, index, { type, ret, fx = {} }) {
  const b = s.mix.buses[index];
  if (type !== undefined) b.type = type;
  if (ret !== undefined) b.ret = ret;
  Object.assign(b.fx, fx);
}
