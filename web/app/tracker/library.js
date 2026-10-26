// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The song library: every song saves itself (IndexedDB) as you edit; songs
// can be listed, loaded, duplicated, renamed, deleted, exported and imported
// as JSON, and passed around as a share code. Songs written by any earlier
// version still load (migrate()).

import { TRACKS, newSong, newPattern, newDexed, newPlaits, newVa } from './song.js';
import { migrateMix } from '../mixer/mix.js';

const DB_NAME = 'orange-mouse';
const STORE = 'songs';
const CURRENT_KEY = 'om.song';
const CODE_PREFIX = 'OM1:';

const request = (req) => new Promise((resolve, reject) => {
  req.onsuccess = () => resolve(req.result);
  req.onerror = () => reject(req.error);
});

let dbPromise = null;
function db() {
  if (!dbPromise) {
    const req = indexedDB.open(DB_NAME, 1);
    req.onupgradeneeded = () => req.result.createObjectStore(STORE, { keyPath: 'id' });
    dbPromise = request(req);
  }
  return dbPromise;
}

async function store(mode) {
  return (await db()).transaction(STORE, mode).objectStore(STORE);
}

// A song from any version, made whole: missing fields get their defaults.
export function migrate(raw) {
  const base = newSong();
  const s = { ...base, ...raw, version: base.version };
  s.patterns = (raw.patterns?.length ? raw.patterns : base.patterns).map((p) => ({ ...newPattern(p.rows ?? 64), ...p, cells: { ...(p.cells ?? {}) } }));
  s.order = (raw.order?.length ? raw.order : [0]).filter((p) => p < s.patterns.length);
  if (!s.order.length) s.order = [0];
  s.tracks = base.tracks.map((t, k) => ({ ...t, ...(raw.tracks?.[k] ?? {}) })).slice(0, TRACKS);
  s.mix = migrateMix(raw.mix);
  s.dexed = newDexed().map((d, k) => ({ ...d, ...(raw.dexed?.[k] ?? {}) }));
  s.plaits = newPlaits().map((d, k) => (raw.plaits?.[k]?.p?.length === d.p.length ? { p: [...raw.plaits[k].p] } : d));
  s.va = newVa().map((d, k) => (Array.isArray(raw.va?.[k]?.params) ? { params: [...raw.va[k].params] }
    : Number.isInteger(raw.va?.[k]?.preset) ? { params: null, preset: raw.va[k].preset } : d));
  s.lxr = Array.isArray(raw.lxr) ? [...raw.lxr] : null;
  return s;
}

export const songId = () => `song-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 7)}`;

export async function list() {
  try {
    const all = await request((await store('readonly')).getAll());
    return all.map(({ id, name, updated }) => ({ id, name, updated })).sort((a, b) => b.updated - a.updated);
  } catch {
    return [];   // storage unavailable: the song still plays, it just isn't kept
  }
}

export async function load(id) {
  try {
    const rec = await request((await store('readonly')).get(id));
    return rec ? { ...migrate(rec.song), id: rec.id } : null;
  } catch {
    return null;
  }
}

export async function save(song) {
  if (!song.id) song.id = songId();
  try {
    await request((await store('readwrite')).put({ id: song.id, name: song.name, updated: Date.now(), song }));
    localStorage.setItem(CURRENT_KEY, song.id);
  } catch { /* not kept this time */ }
}

export async function remove(id) {
  try { await request((await store('readwrite')).delete(id)); } catch { /* already gone */ }
}

// The demo songs (the genre packs), saved into the library: each once per
// version. A demo you delete stays deleted; when a demo gets a new version,
// the copy you had is kept under its old name with "(v<n>)" (you may have
// changed it) and the new one takes its place. Returns how many it wrote.
const SEEDED_KEY = 'om.seeded';   // { id: version }; "1" from the first release: every demo at version 1
export async function seed(songs) {
  let seeded;
  try {
    const raw = localStorage.getItem(SEEDED_KEY);
    seeded = raw === null ? {} : raw === '1' ? Object.fromEntries(songs.map(({ id }) => [id, 1])) : JSON.parse(raw);
  } catch {
    return 0;   // storage unavailable: nothing to seed into
  }
  let wrote = 0;
  const now = Date.now();   // the songs list in the order given: the first is the newest
  for (const [k, { id, make, version = 1 }] of songs.entries()) {
    const had = seeded[id];
    if (had >= version) continue;
    const old = had ? await load(id) : null;
    if (had && !old) { seeded[id] = version; continue; }   // deleted: it stays deleted
    try {
      if (old) {
        const keep = { ...old, id: songId(), name: `${old.name} (v${had})` };
        await request((await store('readwrite')).put({ id: keep.id, name: keep.name, updated: Date.now() - 1000, song: keep }));
      }
      const s = make();
      s.id = id;
      await request((await store('readwrite')).put({ id, name: s.name, updated: now - k * 1000, song: s }));
      seeded[id] = version;
      wrote++;
    } catch { /* not kept this time */ }
  }
  try { localStorage.setItem(SEEDED_KEY, JSON.stringify(seeded)); } catch { /* as above */ }
  return wrote;
}

// Songs taken out of the studio (2026-10-26: the "First steps" and trance
// demos): deleted from the library once, by id or by name.
const RETIRED_KEY = 'om.retired';
export async function retire({ ids = [], names = [] }, version = 1) {
  try {
    if (Number(localStorage.getItem(RETIRED_KEY)) >= version) return 0;
  } catch {
    return 0;
  }
  let removed = 0;
  for (const item of await list()) {
    if (ids.includes(item.id) || names.includes(item.name)) { await remove(item.id); removed++; }
  }
  try {
    localStorage.setItem(RETIRED_KEY, String(version));
    if (ids.includes(currentId()) || removed) localStorage.removeItem(CURRENT_KEY);
  } catch { /* as above */ }
  return removed;
}

export function currentId() {
  try { return localStorage.getItem(CURRENT_KEY); } catch { return null; }
}

// ---- share codes: the song, compressed, as text you can paste anywhere ----

async function pipe(bytes, stream) {
  return new Uint8Array(await new Response(new Blob([bytes]).stream().pipeThrough(stream)).arrayBuffer());
}

export async function toCode(song) {
  const { id, ...rest } = song;
  const packed = await pipe(new TextEncoder().encode(JSON.stringify(rest)), new CompressionStream('deflate-raw'));
  let bin = '';
  packed.forEach((b) => { bin += String.fromCharCode(b); });
  return CODE_PREFIX + btoa(bin).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

export async function fromCode(code) {
  const text = code.trim();
  if (!text.startsWith(CODE_PREFIX)) throw new Error('not an Orange Mouse song code');
  const b64 = text.slice(CODE_PREFIX.length).replace(/-/g, '+').replace(/_/g, '/');
  const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
  const json = new TextDecoder().decode(await pipe(bytes, new DecompressionStream('deflate-raw')));
  return migrate(JSON.parse(json));
}
