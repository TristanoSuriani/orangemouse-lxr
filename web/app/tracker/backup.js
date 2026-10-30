// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// Songs can't be lost (PLAN.md Phase 7.1). The library lives in the browser's
// storage, which a browser may clear (Safari does after 7 days without a
// visit), so:
//   - the studio asks for persistent storage at start;
//   - a backup folder, picked once (B in SONGS, where the browser has File
//     System Access), receives every song as .json each time it saves. After a
//     reload the browser asks again before writing there: B resumes it;
//   - without a folder, the tracker says EXPORT? when the songs changed and
//     nothing was exported (A in SONGS: every song in one file) for a week.
// alert() is the short line the tracker's display shows, or '' when the songs
// are safe. createBackup() takes its surroundings so tests can fake them.

import * as library from './library.js';

const WEEK = 7 * 24 * 3600 * 1000;
const EXPORTED_KEY = 'om.exported';   // when every song was last exported (A)
const SINCE_KEY = 'om.since';         // the first start: the reminder counts a week from it
const FILES_KEY = 'om.backupFiles';   // { song id: its file name in the folder }, to rename files

// A song's file in the folder: its name (characters files can't have replaced) and its id.
export const fileName = (song) => `${song.name.replace(/[\\/:*?"<>|\u0000-\u001f]/g, '-').trim() || 'Untitled'} (${song.id}).json`;

export function createBackup({
  storage = null,   // localStorage
  nav = globalThis.navigator,
  picker = globalThis.showDirectoryPicker && ((opts) => globalThis.showDirectoryPicker(opts)),
  kept = { get: library.getKept, set: library.setKept },
  songs = async () => (await Promise.all((await library.list()).map(async ({ id }) => library.load(id)))).filter(Boolean),
  now = Date.now,
} = {}) {
  const state = {
    persisted: null,      // true or false once the browser answered; null: it can't say
    folder: null,         // the backup folder's handle
    writable: false,      // the browser lets us write there now
    error: '',            // the last write that failed
    lastEdit: 0,          // the newest song's save time
  };
  const listeners = [];
  const changed = () => listeners.forEach((fn) => fn());
  const read = (key) => { try { return (storage ?? localStorage).getItem(key); } catch { return null; } };
  const write = (key, value) => { try { (storage ?? localStorage).setItem(key, value); } catch { /* not kept */ } };
  const canPick = () => typeof picker === 'function';
  const files = () => { try { return JSON.parse(read(FILES_KEY)) ?? {}; } catch { return {}; } };

  async function permitted(handle, ask) {
    const opts = { mode: 'readwrite' };
    if (!handle.queryPermission) return true;   // a handle that needs no permission (the origin's own folder)
    if ((await handle.queryPermission(opts)) === 'granted') return true;
    return ask && (await handle.requestPermission(opts)) === 'granted';
  }

  async function writeSong(song) {
    if (!state.folder || !state.writable || !song?.id) return;
    const names = files();
    const name = fileName(song);
    try {
      const file = await state.folder.getFileHandle(name, { create: true });
      const out = await file.createWritable();
      await out.write(JSON.stringify(song, null, 1));
      await out.close();
      if (names[song.id] && names[song.id] !== name) await state.folder.removeEntry(names[song.id]).catch(() => {});
      names[song.id] = name;
      write(FILES_KEY, JSON.stringify(names));
      if (state.error) { state.error = ''; changed(); }
    } catch (e) {
      // the folder was moved or the permission taken back: say so, and stop until B
      state.error = String(e?.name ?? e);
      state.writable = false;
      changed();
    }
  }

  // Use this folder from now on, and copy every song into it at once.
  async function useFolder(handle) {
    state.folder = handle;
    state.writable = await permitted(handle, true);
    state.error = '';
    write(FILES_KEY, '{}');
    await kept.set('backupFolder', handle);
    if (state.writable) for (const s of await songs()) await writeSong(s);
    changed();
    return state.writable;
  }

  return {
    state,
    onChange: (fn) => listeners.push(fn),
    canPick,

    async start() {
      if (!read(SINCE_KEY)) write(SINCE_KEY, String(now()));
      try {
        const storageApi = nav?.storage;
        if (storageApi?.persist) state.persisted = (await storageApi.persisted?.()) || (await storageApi.persist());
      } catch { state.persisted = null; }
      const handle = await kept.get('backupFolder');
      if (handle) {
        state.folder = handle;
        try { state.writable = await permitted(handle, false); } catch { state.writable = false; }
      }
      changed();
    },

    // B: pick a folder, or let the paused one write again (a key or click: the browser asks)
    async choose() {
      if (state.folder && !state.writable && !state.error) {
        try { state.writable = await permitted(state.folder, true); } catch { state.writable = false; }
        if (state.writable) for (const s of await songs()) await writeSong(s);
        changed();
        return state.writable;
      }
      if (!canPick()) return false;
      let handle;
      try { handle = await picker({ id: 'orange-mouse-songs', mode: 'readwrite' }); } catch { return false; }   // cancelled
      return useFolder(handle);
    },
    useFolder,

    // after each save: the folder gets a copy
    async saved(song) {
      state.lastEdit = now();
      await writeSong(song);
    },
    noteEdit(time) { state.lastEdit = Math.max(state.lastEdit, time); },

    // A: every song was exported
    exported() {
      write(EXPORTED_KEY, String(now()));
      changed();
    },

    // what the display says; '' when the songs are safe
    alert() {
      if (state.folder && state.writable) return '';
      if (state.folder && state.error) return 'BACKUP FOLDER FAILED: SONGS, B';
      if (state.folder) return 'BACKUP PAUSED: SONGS, B';
      const exported = Number(read(EXPORTED_KEY)) || 0;
      const since = Math.max(exported, Number(read(SINCE_KEY)) || 0);
      const unexported = state.lastEdit > exported;
      const fix = canPick() ? 'SONGS, B' : 'SONGS, A';
      if (state.persisted === false && unexported && now() - exported > WEEK) return `SONGS NOT KEPT FOR SURE: ${fix}`;
      if (!canPick() && unexported && now() - since > WEEK) return 'EXPORT? SONGS, A';
      return '';
    },

    // the longer line in SONGS: how the songs are kept
    describe() {
      const where = state.persisted === true ? 'KEPT' : state.persisted === false ? 'MAY BE CLEARED' : 'BROWSER';
      const folder = state.folder ? `FOLDER ${state.folder.name?.toUpperCase() ?? ''}${state.writable ? '' : state.error ? ' FAILED' : ' PAUSED'}`
        : canPick() ? 'NO FOLDER' : 'EXPORT WEEKLY';
      return `${where} · ${folder}`;
    },
  };
}

export const backup = createBackup();
