// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The song, as the page keeps it: the tracker device edits it, the history
// undoes it, the library stores it, and the studio's tracker (studio/tracker.c,
// in the worklet) gets a copy of every change to play.
//
// A cell is null (empty) or { notes: [up to 4 MIDI notes], off: true (a note
// off), vel: 1..127 (0: 100), retrig: 0 or 2..8 }, and maybe an effect (PLAN.md
// 10.1): fx 1..6 (FX_NAMES: one of the track's mixer controls), fxv 0..127,
// fxs true to slide to the track's next effect on that control. A cell with
// only an effect has no notes ([]).

// the effect column's controls (studio/tracker.h OM_FX_*), as the display shows them
export const FX_NAMES = [null, { key: 'L', name: 'level' }, { key: 'P', name: 'pan' }, { key: 'A', name: 'send A' },
  { key: 'B', name: 'send B' }, { key: 'C', name: 'cutoff' }, { key: 'R', name: 'resonance' }];
export const FX_SLIDE = 0x80;

import { newMix, mixMessages } from '../mixer/mix.js';
import plaitsRegistry from '../registry/plaits.js';

export const TRACKS = 16;
export const PATTERNS = 64;
export const MAX_ROWS = 128;
export const ORDER_LEN = 256;
export const CHORD = 4;
export const EMPTY_NOTE = 0xff;
export const OFF_NOTE = 0xfe;

// the studio's engines (web/port/web_studio.c) and what tracks play by default
export const ENGINES = [
  { name: 'LXR', parts: ['Drum 1', 'Drum 2', 'Drum 3', 'Snare', 'Cymbal', 'Hihat cl', 'Hihat op'] },
  { name: 'VA', parts: ['VA1 Bass', 'VA2 Poly', 'VA3 Lead', 'VA4 Noise'] },
  { name: 'Dexed', parts: ['DX1', 'DX2'] },
  { name: 'Plaits', parts: ['PL1', 'PL2'] },
];

// Dexed's parts, as the song keeps them: bank, program, volume, transpose
// (registry/dexed.json; the studio's engine 2)
export const DEXED = 2;
export const newDexed = () => [0, 1].map(() => ({ bank: 1, program: 0, volume: 100, transpose: 24 }));
const DEXED_FIELDS = ['bank', 'program', 'volume', 'transpose'];
export const dexedMessage = (part, field, value) =>
  ({ type: 'param', engine: DEXED, part, param: DEXED_FIELDS.indexOf(field), value });

// VA's slots: their parameters (enum VaParam order), or null for a slot that
// plays a preset as it is ({ params: null, preset: n }; the first if none);
// the LXR's kit: every parameter (registry/lxr.json), or null until the LXR
// reports it (main.js keeps it up to date).
export const VA = 1;
export const VA_P_PRESET = 44;
export const newVa = () => [0, 1, 2, 3].map(() => ({ params: null }));

// Plaits' parts: their 12 parameters as bytes (registry/plaits.json; engine 3).
// A part starts from a preset (the first: "Supersaw").
export const PLAITS = 3;
export const plaitsPreset = (name) => {
  const i = plaitsRegistry.presets.findIndex((pr) => pr.name === name);
  if (i < 0) throw new Error(`no Plaits preset ${name}`);
  return { p: [...plaitsRegistry.presets[i].params, i] };
};
export const newPlaits = () => [0, 1].map(() => plaitsPreset(plaitsRegistry.presets[0].name));
export const plaitsMessage = (part, param, value) => ({ type: 'param', engine: PLAITS, part, param, value });

function defaultTracks() {
  const tracks = [];
  ENGINES.forEach((e, engine) => e.parts.forEach((name, part) => tracks.push({ name, engine, part })));
  while (tracks.length < TRACKS) tracks.push({ name: `Track ${tracks.length + 1}`, engine: null, part: 0 });
  return tracks.map((t) => ({ ...t, mute: false, solo: false }));
}

export function newPattern(rows = 64) {
  return { rows, bpm: 0, swing: 50, cells: {} };   // cells: "row:track" -> cell; bpm 0: the song's; swing 50..75 %
}
export const patternMessage = (p, pat) => ({ type: 'tracker', op: 'pattern', p, rows: pat.rows, bpm: pat.bpm, swing: pat.swing ?? 50 });

export function newSong() {
  return { version: 1, name: 'Untitled', bpm: 120, patterns: [newPattern()], order: [0], tracks: defaultTracks(), mix: newMix(), dexed: newDexed(), plaits: newPlaits(),
    va: newVa(), lxr: null };
}

export const cellKey = (row, track) => `${row}:${track}`;
export const getCell = (song, p, row, track) => song.patterns[p]?.cells[cellKey(row, track)] ?? null;

export function sameCell(a, b) {
  return JSON.stringify(a ?? null) === JSON.stringify(b ?? null);
}

// ---- to the worklet ----

function cellMessage(p, row, track, cell) {
  const notes = [EMPTY_NOTE, EMPTY_NOTE, EMPTY_NOTE, EMPTY_NOTE];
  if (cell?.off) notes[0] = OFF_NOTE;
  else if (cell) cell.notes.slice(0, CHORD).forEach((n, i) => { notes[i] = n; });
  return { type: 'tracker', op: 'cell', p, row, track, notes, vel: cell?.vel ?? 0, retrig: cell?.retrig ?? 0,
    fx: cell?.fx ? cell.fx | (cell.fxs ? FX_SLIDE : 0) : 0, fxv: cell?.fxv ?? 0 };
}

// Every message that makes the worklet's tracker and mixer a copy of the song.
export function songMessages(song) {
  const msgs = [{ type: 'tracker', op: 'clear' }, { type: 'tracker', op: 'tempo', bpm: song.bpm }];
  song.patterns.forEach((pat, p) => {
    msgs.push(patternMessage(p, pat));
    for (const [key, cell] of Object.entries(pat.cells)) {
      const [row, track] = key.split(':').map(Number);
      msgs.push(cellMessage(p, row, track, cell));
    }
  });
  for (let i = 0; i < ORDER_LEN; i++) {
    msgs.push({ type: 'tracker', op: 'order', position: i, p: i < song.order.length ? song.order[i] : 0xff });
    if (i >= song.order.length) break;
  }
  song.tracks.forEach((t, k) => {
    if (t.engine !== null) msgs.push({ type: 'tracker', op: 'route', track: k, engine: t.engine, part: t.part });
    msgs.push({ type: 'tracker', op: 'mute', track: k, on: t.mute }, { type: 'tracker', op: 'solo', track: k, on: t.solo });
  });
  song.dexed.forEach((part, k) => DEXED_FIELDS.forEach((f) => msgs.push(dexedMessage(k, f, part[f]))));
  song.plaits.forEach((part, k) => part.p.forEach((v, i) => msgs.push(plaitsMessage(k, i, v))));
  // VA: the preset first (it loads all of the slot's sound), then the changes
  song.va.forEach((slot, k) => {
    msgs.push({ type: 'param', engine: VA, part: k, param: VA_P_PRESET, value: slot.params?.[VA_P_PRESET] ?? slot.preset ?? 0 });
    slot.params?.forEach((v, i) => { if (i !== VA_P_PRESET) msgs.push({ type: 'param', engine: VA, part: k, param: i, value: v }); });
  });
  song.lxr?.forEach((v, i) => msgs.push({ type: 'param', engine: 0, part: 0, param: i, value: v }));
  return msgs.concat(mixMessages(song.mix));
}

// ---- editing, with undo ----

// Cell changes go through an Editor: it applies them, sends them to the worklet
// and keeps the history. One action (typing a note, pasting a block) is one undo step.
export class Editor {
  constructor(song, send) {
    this.song = song;
    this.send = send;
    this.undoStack = [];
    this.redoStack = [];
  }

  // changes: [{ p, row, track, cell }]
  apply(changes) {
    const step = [];
    for (const { p, row, track, cell } of changes) {
      const before = getCell(this.song, p, row, track);
      if (sameCell(before, cell)) continue;
      step.push({ p, row, track, before, after: cell });
      this.write(p, row, track, cell);
    }
    if (step.length) {
      this.undoStack.push(step);
      if (this.undoStack.length > 500) this.undoStack.shift();
      this.redoStack = [];
    }
    return step.length > 0;
  }

  write(p, row, track, cell) {
    const cells = this.song.patterns[p].cells;
    if (cell) cells[cellKey(row, track)] = cell;
    else delete cells[cellKey(row, track)];
    this.send(cellMessage(p, row, track, cell));
  }

  undo() {
    const step = this.undoStack.pop();
    if (!step) return null;
    for (const c of [...step].reverse()) this.write(c.p, c.row, c.track, c.before);
    this.redoStack.push(step);
    return step;
  }

  redo() {
    const step = this.redoStack.pop();
    if (!step) return null;
    for (const c of step) this.write(c.p, c.row, c.track, c.after);
    this.undoStack.push(step);
    return step;
  }
}
