#!/usr/bin/env node
// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// A song as the stream of messages the page sends the studio (songMessages in
// web/app/tracker/song.js), one per line, for tests/host/song_render.c to
// replay natively: the regression songs (PLAN.md Phase 6) render exactly what
// the browser plays.
//
//   node tools/song_stream.mjs web/app/tracker/genres/trance.js > trance.txt
//
// The module's default export is a function returning the song. Lines:
//   T clear | T tempo <bpm> | T pattern <p> <rows> <bpm> <swing> | T order <pos> <p>
//   T cell <p> <row> <track> <n0> <n1> <n2> <n3> <vel> <retrig> <fx> <fxv> <len> <glide> <cond>
//   T route <track> <engine> <part> | T mute <track> <0|1> | T solo <track> <0|1>
//   P <engine> <part> <param> <value>      an engine's parameter
//   M <target> <param> <value>             the mixer
//   F <bus> <param> <value>                a send effect
//   S <seconds>                            how long the song plays (order list, tempos)

import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';
import { songMessages } from '../web/app/tracker/song.js';

const path = process.argv[2];
if (!path) {
  console.error('usage: node tools/song_stream.mjs <song module>');
  process.exit(2);
}
const song = (await import(pathToFileURL(resolve(path)).href)).default();

const lines = [];
for (const m of songMessages(song)) {
  if (m.type === 'tracker') {
    const args = { clear: () => [], tempo: () => [m.bpm], pattern: () => [m.p, m.rows, m.bpm ?? 0, m.swing ?? 50], order: () => [m.position, m.p],
      cell: () => [m.p, m.row, m.track, ...m.notes, m.vel, m.retrig, m.fx, m.fxv, m.len, m.glide, m.cond], route: () => [m.track, m.engine, m.part],
      mute: () => [m.track, m.on ? 1 : 0], solo: () => [m.track, m.on ? 1 : 0] }[m.op];
    if (args) lines.push(['T', m.op, ...args()].join(' '));
  } else if (m.type === 'param') lines.push(`P ${m.engine} ${m.part} ${m.param} ${m.value}`);
  else if (m.type === 'mix') lines.push(`M ${m.target} ${m.param} ${m.value}`);
  else if (m.type === 'fx') lines.push(`F ${m.bus} ${m.param} ${m.value}`);
}
// the song's length: its order list, each pattern at its own tempo
let seconds = 0;
for (const p of song.order) {
  const pat = song.patterns[p];
  seconds += (pat.rows * 60) / ((pat.bpm || song.bpm) * 4);
}
lines.push(`S ${seconds.toFixed(3)}`);
process.stdout.write(lines.join('\n') + '\n');
