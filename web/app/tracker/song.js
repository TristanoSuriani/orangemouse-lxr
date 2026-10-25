// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The song, as the page keeps it: the tracker device edits it, the history
// undoes it, the library stores it, and the studio's tracker (studio/tracker.c,
// in the worklet) gets a copy of every change to play.
//
// A cell is null (empty) or { notes: [up to 4 MIDI notes], off: true (a note
// off), vel: 1..127 (0: 100), retrig: 0 or 2..8 }.

import { newMix, mixMessages } from '../mixer/mix.js';

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
];

function defaultTracks() {
  const tracks = [];
  ENGINES.forEach((e, engine) => e.parts.forEach((name, part) => tracks.push({ name, engine, part })));
  while (tracks.length < TRACKS) tracks.push({ name: `Track ${tracks.length + 1}`, engine: null, part: 0 });
  return tracks.map((t) => ({ ...t, mute: false, solo: false }));
}

export function newPattern(rows = 64) {
  return { rows, bpm: 0, cells: {} };   // cells: "row:track" -> cell; bpm 0: the song's
}

export function newSong() {
  return { version: 1, name: 'Untitled', bpm: 120, patterns: [newPattern()], order: [0], tracks: defaultTracks(), mix: newMix() };
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
  return { type: 'tracker', op: 'cell', p, row, track, notes, vel: cell?.vel ?? 0, retrig: cell?.retrig ?? 0 };
}

// Every message that makes the worklet's tracker and mixer a copy of the song.
export function songMessages(song) {
  const msgs = [{ type: 'tracker', op: 'clear' }, { type: 'tracker', op: 'tempo', bpm: song.bpm }];
  song.patterns.forEach((pat, p) => {
    msgs.push({ type: 'tracker', op: 'pattern', p, rows: pat.rows, bpm: pat.bpm });
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
