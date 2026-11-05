// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The session: the sounds and the mix the devices edit. It is a song of the
// library (tracker/library.js), so everything saved before still loads, its
// patterns included: the archived tracker (docs/TRACKER_ARCHIVED.md) can come
// back to them. Every change goes to the worklet, and saves a moment later.

import * as library from './tracker/library.js';
import { songMessages } from './tracker/song.js';

const SAVED_TYPES = new Set(['mix', 'fx', 'param']);
const SAVE_DELAY_MS = 800;

export function createSession({ send: sendToWorklet }) {
  let song = null;
  let saveTimer = null;
  const listeners = [];

  const saveSoon = () => {
    clearTimeout(saveTimer);
    saveTimer = setTimeout(() => library.save(song), SAVE_DELAY_MS);
  };
  const send = (m) => {
    sendToWorklet(m);
    if (SAVED_TYPES.has(m.type)) saveSoon();
  };

  function load(s) {
    if (song) sendToWorklet({ type: 'tracker', op: 'stop' });
    song = s;
    for (const m of songMessages(s)) sendToWorklet(m);
    saveSoon();   // the library remembers it as the last one used
    listeners.forEach((fn) => fn(s));
  }

  // The LXR's kit as the worklet reports it (its panel or SD card changed it).
  function keepLxrKit(values) {
    if (song.lxr && song.lxr.length === values.length && song.lxr.every((v, i) => v === values[i])) return;
    song.lxr = values;
    saveSoon();
  }

  return { send, load, keepLxrKit, song: () => song, onLoad: (fn) => listeners.push(fn) };
}
