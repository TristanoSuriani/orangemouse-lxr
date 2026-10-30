// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// Songs can't be lost (PLAN.md Phase 7.1): the backup folder, the export
// reminder and the import of a file of every song, against a fake folder,
// storage and clock. Run by tests/run.sh:
//   node tests/backup_test.mjs

import { createBackup, fileName } from '../web/app/tracker/backup.js';
import { migrate, parseImport, allSongsFile } from '../web/app/tracker/library.js';

let failures = 0;
const check = (ok, msg) => { if (!ok) { failures++; console.log(`FAIL ${msg}`); } };
const DAY = 24 * 3600 * 1000;

const fakeStorage = () => {
  const m = new Map();
  return { getItem: (k) => (m.has(k) ? m.get(k) : null), setItem: (k, v) => m.set(k, String(v)) };
};
// a folder whose permission is 'granted' or 'prompt' until requested
const fakeFolder = (permission = 'granted', { grant = true } = {}) => {
  const files = new Map();
  return {
    name: 'songs', files,
    queryPermission: async () => permission,
    requestPermission: async () => (permission = grant ? 'granted' : 'denied'),
    getFileHandle: async (name) => ({
      createWritable: async () => { let text = ''; return { write: async (t) => { text += t; }, close: async () => files.set(name, text) }; },
    }),
    removeEntry: async (name) => { files.delete(name); },
  };
};
const fakeKept = () => { const m = new Map(); return { get: async (k) => m.get(k), set: async (k, v) => m.set(k, v), m }; };
const song = (id, name) => ({ ...migrate({ name }), id });
const make = ({ persisted = true, picker, now, kept = fakeKept(), songs = [] } = {}) => createBackup({
  storage: fakeStorage(), kept, picker, now: () => now.t, songs: async () => songs,
  nav: { storage: { persisted: async () => false, persist: async () => persisted } },
});

// a picked folder gets every song at once, then each save; a rename moves the file
{
  const now = { t: 1000 * DAY };
  const folder = fakeFolder();
  const a = song('song-a', 'Acid / line'), b = song('song-b', 'Dub');
  const bk = make({ picker: async () => folder, now, songs: [a, b] });
  await bk.start();
  check(bk.alert() === '', `persistent storage and a fresh start: no alert, got "${bk.alert()}"`);
  check(await bk.choose(), 'B picks the folder');
  check(folder.files.size === 2 && folder.files.has('Acid - line (song-a).json'), `every song copied at once: ${[...folder.files.keys()]}`);
  check(JSON.parse(folder.files.get(fileName(b))).name === 'Dub', 'the file is the song');
  b.name = 'Dub chords';
  await bk.saved(b);
  check(folder.files.has('Dub chords (song-b).json') && !folder.files.has('Dub (song-b).json'), `a rename moves the file: ${[...folder.files.keys()]}`);
  check(bk.alert() === '' && bk.describe() === 'KEPT · FOLDER SONGS', `with a folder: no alert, "${bk.describe()}"`);
  const back = parseImport(folder.files.get('Dub chords (song-b).json'));
  check(back.length === 1 && back[0].name === 'Dub chords' && back[0].id !== 'song-b', 'a backed-up file imports as a new song');
}

// after a reload the folder is paused until B; a folder that fails says so
{
  const now = { t: 1000 * DAY };
  const kept = fakeKept();
  const folder = fakeFolder('prompt');
  kept.m.set('backupFolder', folder);
  const s = song('song-c', 'Electro');
  const bk = make({ now, kept, songs: [s], picker: async () => folder });
  await bk.start();
  await bk.saved(s);
  check(folder.files.size === 0, 'a paused folder is not written');
  check(bk.alert() === 'BACKUP PAUSED: SONGS, B', `paused: "${bk.alert()}"`);
  check(await bk.choose() && folder.files.size === 1, 'B resumes it and copies the songs');
  check(bk.alert() === '', 'resumed: no alert');
  folder.getFileHandle = async () => { throw Object.assign(new Error('gone'), { name: 'NotFoundError' }); };
  await bk.saved(s);
  check(bk.alert() === 'BACKUP FOLDER FAILED: SONGS, B', `a failed write: "${bk.alert()}"`);
}

// refused persistent storage, no folder: say so until every song is exported
{
  const now = { t: 1000 * DAY };
  const bk = make({ persisted: false, picker: async () => fakeFolder(), now });
  await bk.start();
  bk.noteEdit(now.t);
  check(bk.alert() === 'SONGS NOT KEPT FOR SURE: SONGS, B', `refused: "${bk.alert()}"`);
  bk.exported();
  check(bk.alert() === '', 'exported: quiet');
  now.t += 8 * DAY;
  check(bk.alert() === '', 'a week later, nothing edited: quiet');
  await bk.saved(song('song-d', 'New'));
  check(bk.alert() === 'SONGS NOT KEPT FOR SURE: SONGS, B', `a week later, edited: "${bk.alert()}"`);
}

// no folder in this browser: EXPORT? a week after the first start or the last export, if edited
{
  const now = { t: 1000 * DAY };
  const bk = make({ now });
  await bk.start();
  bk.noteEdit(now.t);
  check(bk.alert() === '' && !bk.canPick(), `first week: quiet, "${bk.alert()}"`);
  now.t += 8 * DAY;
  check(bk.alert() === 'EXPORT? SONGS, A', `a week on: "${bk.alert()}"`);
  bk.exported();
  check(bk.alert() === '', 'exported: quiet');
  now.t += 8 * DAY;
  check(bk.alert() === '', 'a week later, nothing edited: quiet');
  bk.noteEdit(now.t);
  check(bk.alert() === 'EXPORT? SONGS, A', 'edited since: EXPORT? again');
  check(bk.describe() === 'KEPT · EXPORT WEEKLY', `"${bk.describe()}"`);
}

// a file of every song imports as that many new songs; other files don't
{
  const all = parseImport(allSongsFile([song('x', 'One'), song('y', 'Two')]));
  check(all.length === 2 && all[1].name === 'Two' && all.every((s) => !['x', 'y'].includes(s.id)), 'every song in one file imports');
  for (const bad of ['{}', '[]', '{"kind":"orange-mouse-songs","songs":[]}', 'null'])
    check((() => { try { parseImport(bad); return false; } catch { return true; } })(), `"${bad}" is not a song`);
}

if (failures) { console.log(`${failures} backup checks failed`); process.exit(1); }
console.log('ok: the backup folder, the export reminder and importing every song');
