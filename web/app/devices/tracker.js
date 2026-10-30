// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The tracker device (PLAN.md Phase 2): a large dot-matrix display of the
// pattern, four soft knobs and the transport and edit pads, built from the kit.
// It edits the song (tracker/song.js); the studio's tracker in the worklet
// plays it. The rules come from TRIS Tracker: edit and jam are separate modes,
// STEP sets how far the cursor moves after a note, Shift stacks a chord, a copy
// without a selection is one cell, undo covers everything.
//
//   display  row 0: status; row 1: the 8 tracks shown (number, name, M/S);
//            rows 2-17: the pattern (a bar of sixteenths); row 18: the order
//            list; row 19: the knobs
//   cell     "C-4³ 64 4": note, chord size, velocity (hex; ·· = 100), retrigger

import { DotMatrix } from '../kit/dotmatrix.js';
import { describe } from '../kit/help.js';
import { makePad, makeKnob, setLed } from '../kit/controls.js';
import { addGuide } from '../kit/guide.js';
import { KEY_SEMITONES } from '../kit/keybed.js';
import { TRACKS, MAX_ROWS, PATTERNS, CHORD, ENGINES, newPattern, getCell, songMessages, Editor } from '../tracker/song.js';
import * as library from '../tracker/library.js';
import { backup } from '../tracker/backup.js';
import { renderSong, wavFile } from '../tracker/wav.js';
import { createPlaylist } from '../tracker/playlist.js';

const COLS = 84;
const ROWS = 20;
const VISIBLE = 16;          // pattern rows on the display
const FIRST = 2;             // the display row of the first pattern row
const CELL = 10;             // display columns per track (9 and a gap)
const SHOWN = 8;             // tracks on the display at once
const NOTE_NAMES = ['C-', 'C#', 'D-', 'D#', 'E-', 'F-', 'F#', 'G-', 'G#', 'A-', 'A#', 'B-'];
const SUPERSCRIPT = ['', '', '²', '³', '⁴'];
const STEPS = [0, 1, 2, 4, 8];

const hex = (n) => n.toString(16).toUpperCase().padStart(2, '0');
const noteName = (n) => NOTE_NAMES[n % 12] + (n < 12 ? '-' : String(Math.floor(n / 12) - 1));

function cellText(cell) {
  if (!cell) return '···  ·· ·';
  if (cell.off) return 'OFF  ·· ·';
  const n = cell.notes;
  return `${noteName(n[0])}${SUPERSCRIPT[n.length] || ' '} ${cell.vel ? hex(cell.vel) : '··'} ${cell.retrig >= 2 ? cell.retrig : '·'}`;
}

function cellWords(cell) {
  if (!cell) return 'empty';
  if (cell.off) return 'note off: ends the note playing';
  const chord = cell.notes.length > 1 ? ` (a chord: ${cell.notes.map(noteName).join(' ')})` : '';
  return `${noteName(cell.notes[0])}${chord}, velocity ${cell.vel || 100}${cell.retrig >= 2 ? `, ${cell.retrig} hits` : ''}`;
}

const GUIDE = [
  { title: 'Write notes', text: 'EDIT on (Esc toggles it): the keys Z-M and Q-I write notes at the cursor and the cursor moves down by STEP. EDIT off: the keys only play. A writes a note off, Backspace clears. [ and ] change the octave.' },
  { title: 'Chords', text: 'Hold Shift while typing: the note joins the cell instead of replacing it, up to 4 (the cell shows ² ³ ⁴). The cursor stays.' },
  { title: 'Step', text: 'STEP (or - and =) sets how far the cursor moves after a note: 1 fills every row, 4 writes on the beat, 0 keeps it still for auditioning.' },
  { title: 'Select, copy, paste', text: 'Without a selection, Cmd/Ctrl C and V copy one cell. Shift+arrows or a drag select a block; paste lands at the cursor. Cmd+Shift+V pastes back into the same tracks. Cmd+A: the track, again: everything. Cmd+Z undoes.' },
  { title: 'Play', text: 'Space plays the song from the order position you chose (click it on the ORDER row), or stops. Enter (LOOP) repeats the pattern you edit. PAT -/+ choose the pattern; PAT + past the last makes a new one; Cmd+D duplicates it. ORD + adds it to the order list. BPM sets the pattern\'s tempo.' },
  { title: 'Songs', text: 'Every song saves itself as you edit. SONGS lists them: arrows choose, Enter loads, N new, D duplicate, R rename, Delete (twice) deletes, E export and I import a .json file, W renders a WAV file, C copies a share code, V loads one.' },
  { title: 'The playlist', text: 'In SONGS, Q queues the chosen song and L shows the playlist: Enter plays from there, each song to its end and then the next after the gap (- and =). Shift+arrows reorder, Delete removes. STOP ends it.' },
  { title: 'Keeping songs safe', text: 'The browser may clear its storage. B picks a backup folder: every song is copied there as .json each time it saves (after a reload, B lets it write again). A exports every song in one file, which I imports. The display says when the songs are not safe.' },
  { title: 'Tracks', text: 'Tracks 1-7 play the LXR\'s voices, 8-11 VA\'s slots, 12-13 Dexed\'s parts, 14-15 Plaits\'. The left and right arrows move between tracks; T 9-16 shows the other eight. Click a track\'s name to mute it, right-click to solo it.' },
];

export function createTrackerDevice({ send: sendToWorklet, help, sdImage = () => null }) {
  const song = { value: null };
  // every change to the song is sent to the worklet, and saves the song a moment later
  let saveTimer = null;
  const send = (m) => {
    sendToWorklet(m);
    // (the mixer and Dexed send through here too: their settings are the song's)
    if ((m.type === 'tracker' && m.op !== 'play' && m.op !== 'stop') || m.type === 'mix' || m.type === 'fx' || m.type === 'param') {
      clearTimeout(saveTimer);
      saveTimer = setTimeout(() => library.save(song.value), 800);
    }
  };
  let editor = null;
  const state = {
    p: 0,                     // the pattern edited
    cur: { r: 0, k: 0 },
    anchor: null,             // the other corner of a selected block
    edit: true,
    step: 1,
    octs: new Array(TRACKS).fill(4),
    view: 0,                  // 0: tracks 1-8, 1: 9-16
    top: 0,                   // the first pattern row on the display
    playing: [0, 0, 0, 0, 120],   // from the worklet: playing, pattern, position, row, tempo
    clip: null,               // { w, h, k0, cells }
    pos: 0,                   // the order position PLAY starts from
    mode: 'pattern',          // or 'songs': the library on the display
    songs: [],                // the library's list, in songs mode
    songCur: 0,
    confirm: null,            // a song id waiting for its second Delete
    message: '',              // a short note on the status row (songs mode)
    banner: '',               // the same in pattern mode: a WAV being rendered, then its result
    plCur: 0,                 // the chosen playlist entry
  };

  const el = document.createElement('section');
  el.className = 'device tracker';
  el.setAttribute('aria-label', 'Tracker');
  el.tabIndex = -1;
  // the ? pad shares the control row (the rack card names the device): the
  // display gets the height
  el.innerHTML = `
    <canvas class="lcd tracker-lcd"></canvas>
    <div class="tracker-controls">
      <div class="pots tracker-knobs"></div>
      <div class="group tracker-pads"></div>
      <header class="device-head"></header>
    </div>`;
  const canvas = el.querySelector('.tracker-lcd');
  const display = new DotMatrix(canvas, { cols: COLS, rows: ROWS, dot: 2, margin: 6 });

  const pat = () => song.value.patterns[state.p];
  const sel = () => {
    if (!state.anchor) return null;
    return {
      k0: Math.min(state.anchor.k, state.cur.k), k1: Math.max(state.anchor.k, state.cur.k),
      r0: Math.min(state.anchor.r, state.cur.r), r1: Math.max(state.anchor.r, state.cur.r),
    };
  };
  const cell = (r = state.cur.r, k = state.cur.k) => getCell(song.value, state.p, r, k);
  const isPlayingHere = () => state.playing[0] && state.playing[1] === state.p;

  // ---- the display ----
  function render() {
    const s = song.value;
    const pt = pat();
    // follow the playhead while jamming, else keep the cursor in view
    const focus = !state.edit && isPlayingHere() ? state.playing[3] : state.cur.r;
    state.top = Math.max(0, Math.min(pt.rows - VISIBLE, focus - Math.floor(VISIBLE / 2)));
    if (pt.rows <= VISIBLE) state.top = 0;
    display.uninvert();

    const play = state.playing[0] ? `▶ ${state.playing[1] === state.p ? 'HERE' : `P${hex(state.playing[1])}`} POS ${hex(state.playing[2])}` : 'STOPPED';
    if (state.mode === 'songs') { renderSongs(play); return; }
    if (state.mode === 'playlist') { renderPlaylist(play); return; }
    const bpm = pt.bpm || s.bpm;
    display.line(0, state.banner || `PAT ${hex(state.p)}/${hex(s.patterns.length - 1)} LEN ${pt.rows} BPM ${Math.round(bpm)}  ${play.padEnd(16)}`
      + ` ${state.edit ? 'EDIT' : 'JAM '} STEP ${state.step} OCT ${state.octs[state.cur.k]}  T${state.view ? '9-16' : '1-8'}`);

    let head = '   ';
    for (let j = 0; j < SHOWN; j++) {
      const k = state.view * SHOWN + j;
      const t = s.tracks[k];
      const mark = t.solo ? 'S' : t.mute ? 'M' : t.engine === null ? '-' : ' ';
      head += `${String(k + 1).padStart(2)} ${t.name.replace(/\s+/g, '').slice(0, 5).padEnd(5)}${mark} `;
    }
    display.line(1, head);
    if (Math.floor(state.cur.k / SHOWN) === state.view) display.invert(1, 3 + (state.cur.k % SHOWN) * CELL, CELL - 1);

    const box = sel();
    for (let i = 0; i < VISIBLE; i++) {
      const r = state.top + i;
      const row = FIRST + i;
      if (r >= pt.rows) { display.line(row, ''); continue; }
      let text = `${hex(r)}${r % 4 === 0 ? '·' : ' '}`;
      for (let j = 0; j < SHOWN; j++) text += cellText(cell(r, state.view * SHOWN + j)) + ' ';
      display.line(row, text);
      if (isPlayingHere() && state.playing[3] === r) display.invert(row, 0, 2);
      for (let j = 0; j < SHOWN; j++) {
        const k = state.view * SHOWN + j;
        const inSel = box && r >= box.r0 && r <= box.r1 && k >= box.k0 && k <= box.k1;
        if (inSel || (r === state.cur.r && k === state.cur.k)) display.invert(row, 3 + j * CELL, CELL - 1);
      }
    }

    // the order list, the position playing inverted
    let order = 'ORDER ';
    const starts = [];
    s.order.forEach((p) => { starts.push(order.length); order += hex(p) + ' '; });
    const alert = backup.alert();
    display.line(ROWS - 2, alert && order.length + alert.length < COLS ? order.padEnd(COLS - alert.length) + alert : order);
    display.invert(ROWS - 2, starts[state.playing[0] ? state.playing[2] : state.pos] ?? 0, 2);
    orderStarts = starts;

    // the knobs' values
    const c = cell();
    const vel = c && !c.off ? (c.vel || 100) : '-';
    const rtg = c && !c.off ? (c.retrig >= 2 ? c.retrig : 'off') : '-';
    display.line(ROWS - 1, [`VEL ${vel}`, `RTG ${rtg}`, `BPM ${Math.round(bpm)}`, `LEN ${pt.rows}`].map((x) => x.padEnd(21)).join(''));
    display.draw();
    help.refresh();
    setLed(pads.edit, state.edit);
    setLed(pads.play, state.playing[0] && !loopMode);
    setLed(pads.loop, state.playing[0] && loopMode);
    setLed(pads.view, state.view === 1);
  }

  // ---- the library on the display (SONGS) ----
  let orderStarts = [];
  function renderSongs(play) {
    display.uninvert();
    display.line(0, `SONGS ${state.message || 'N new D dup R rename DEL del E/I json W wav C/V code A all B folder Q/L list'}`.slice(0, COLS));
    display.line(1, `   ${'NAME'.padEnd(40)}SAVED`);
    const list = state.songs;
    const top = Math.max(0, Math.min(list.length - VISIBLE, state.songCur - Math.floor(VISIBLE / 2)));
    for (let i = 0; i < VISIBLE; i++) {
      const item = list[top + i];
      const row = FIRST + i;
      if (!item) { display.line(row, ''); continue; }
      const current = item.id === song.value.id ? '▶' : ' ';
      display.line(row, ` ${current} ${item.name.slice(0, 39).padEnd(40)}${new Date(item.updated).toLocaleString().slice(0, 30)}`);
      if (top + i === state.songCur) display.invert(row, 3, 40);
    }
    const count = list.length ? `${list.length} SONG${list.length === 1 ? '' : 'S'}   ${play}` : 'NO SONGS SAVED YET';
    const kept = backup.alert() || backup.describe();
    display.line(ROWS - 2, count.padEnd(COLS - kept.length) + kept);
    display.line(ROWS - 1, 'SONGS again (or Esc) goes back to the pattern');
    display.draw();
    help.refresh();
  }

  // W in SONGS: the song as a WAV file, rendered offline (the studio keeps playing)
  let rendering = false, bannerTimer = null;
  const say = (text, keep = false) => {   // on the status row in either mode
    clearTimeout(bannerTimer);
    state.banner = text;
    if (!keep) bannerTimer = setTimeout(() => { state.banner = ''; render(); }, 4000);
    note(text);
  };
  async function exportWav(s) {
    if (rendering) return null;
    rendering = true;
    say(`RENDERING ${s.name.toUpperCase()}…`, true);
    try {
      const card = sdImage();
      const r = await renderSong(s, { sdImage: card ? card.slice().buffer : null,
        onProgress: (done) => say(`RENDERING ${s.name.toUpperCase()}… ${Math.round(done * 100)} %`, true) });
      const blob = wavFile(r.left, r.right, r.rate);
      const url = URL.createObjectURL(blob);
      Object.assign(document.createElement('a'), { href: url, download: `${s.name}.wav` }).click();
      setTimeout(() => URL.revokeObjectURL(url), 1000);
      say(`WAV EXPORTED: ${Math.round(r.seconds)} S, PEAK ${(20 * Math.log10(r.peak + 1e-9)).toFixed(1)} DB`);
      return blob;
    } catch (e) {
      say(`THE WAV FAILED: ${String(e.message ?? e).toUpperCase()}`);
      return null;
    } finally {
      rendering = false;
    }
  }

  // ---- the playlist on the display (L in SONGS) ----
  const playlist = createPlaylist({ kept: { get: library.getKept, set: library.setKept } });
  playlist.restore().then(() => render());
  let gapTimer = null, fromPlaylist = false;
  function renderPlaylist(play) {
    const pl = playlist.state;
    const names = Object.fromEntries(state.songs.map((x) => [x.id, x.name]));
    display.uninvert();
    display.line(0, `PLAYLIST ${state.message || 'Enter plays from here  Shift+arrows move  DEL removes  - = gap'}`.slice(0, COLS));
    display.line(1, `    #  ${'SONG'.padEnd(40)}`);
    const top = Math.max(0, Math.min(pl.items.length - VISIBLE, state.plCur - Math.floor(VISIBLE / 2)));
    for (let i = 0; i < VISIBLE; i++) {
      const k = top + i;
      const id = pl.items[k];
      if (id === undefined) { display.line(FIRST + i, ''); continue; }
      const mark = pl.active && pl.current === k ? '▶' : ' ';
      display.line(FIRST + i, ` ${mark} ${String(k + 1).padStart(2)}  ${(names[id] ?? '(deleted)').slice(0, 40)}`);
      if (k === state.plCur) display.invert(FIRST + i, 7, 40);
    }
    display.line(ROWS - 2, `${pl.items.length ? `${pl.items.length} IN THE PLAYLIST` : 'EMPTY: Q IN SONGS QUEUES A SONG'}   GAP ${pl.gap} S   ${pl.active ? `PLAYING ${pl.current + 1}` : play}`);
    display.line(ROWS - 1, 'L (or Esc) goes back to the songs');
    display.draw();
    help.refresh();
  }
  async function openPlaylist(on) {
    state.mode = on ? 'playlist' : 'songs';
    state.message = '';
    state.songs = await library.list();
    playlist.prune(state.songs.map((x) => x.id));
    state.plCur = Math.max(0, Math.min(state.plCur, playlist.state.items.length - 1));
    render();
  }
  // plays entry i from its start; when it ends on its own, the next follows
  async function startEntry(i) {
    clearTimeout(gapTimer);
    const id = playlist.start(i);
    const s = id && (await library.load(id));
    if (!s) { playlist.stop(); return note('THAT SONG IS GONE'); }
    s.id = id;
    fromPlaylist = true;
    load(s);
    fromPlaylist = false;
    loopMode = false;
    send({ type: 'tracker', op: 'play', mode: 'song', from: 0 });
    render();
  }
  function stopPlaylist() {
    clearTimeout(gapTimer);
    playlist.stop();
  }
  function playlistKey(e) {
    const pl = playlist.state;
    const n = pl.items.length;
    if (e.key === 'ArrowUp' || e.key === 'ArrowDown') {
      const d = e.key === 'ArrowUp' ? -1 : 1;
      state.plCur = e.shiftKey ? playlist.move(state.plCur, d) : Math.max(0, Math.min(n - 1, state.plCur + d));
    } else if (e.key === 'Enter' && n) return startEntry(state.plCur);
    else if ((e.key === 'Delete' || e.key === 'Backspace') && n) {
      playlist.remove(state.plCur);
      state.plCur = Math.max(0, Math.min(state.plCur, pl.items.length - 1));
    } else if (e.key === '-' || e.key === '=') playlist.setGap(pl.gap + (e.key === '=' ? 1 : -1));
    else if (e.key === 'Escape' || e.key.toLowerCase() === 'l') return openPlaylist(false);
    else return false;
    render();
    return true;
  }

  async function openSongs(on = state.mode !== 'songs') {
    state.mode = on ? 'songs' : 'pattern';
    state.message = '';
    state.confirm = null;
    if (on) {
      await library.save(song.value);
      state.songs = await library.list();
      state.songCur = Math.max(0, state.songs.findIndex((x) => x.id === song.value.id));
    }
    setLed(pads.songs, on);
    render();
  }

  function note(text) {
    state.message = text;
    render();
  }

  async function songKey(e) {
    const list = state.songs;
    const item = list[state.songCur];
    const k = e.key.toLowerCase();
    if (e.key === 'ArrowUp') state.songCur = Math.max(0, state.songCur - 1);
    else if (e.key === 'ArrowDown') state.songCur = Math.min(list.length - 1, state.songCur + 1);
    else if (e.key === 'Escape') return openSongs(false);
    else if (e.key === 'Enter' && item) {
      const loaded = await library.load(item.id);
      if (loaded) { load(loaded); return openSongs(false); }
    } else if (k === 'n') {
      const fresh = library.migrate({ name: `Song ${list.length + 1}` });
      load(fresh);
      await library.save(fresh);
      return openSongs(true);
    } else if (k === 'd' && item) {
      const copy = await library.load(item.id);
      if (copy) { copy.id = library.songId(); copy.name = `${copy.name} copy`; await library.save(copy); state.songs = await library.list(); note(`DUPLICATED ${item.name.toUpperCase()}`); }
    } else if (k === 'r' && item) {
      const name = window.prompt('Song name', item.name);
      if (name) {
        const target = item.id === song.value.id ? song.value : await library.load(item.id);
        if (target) { target.id = item.id; target.name = name.slice(0, 40); await library.save(target); state.songs = await library.list(); }
      }
    } else if ((e.key === 'Delete' || e.key === 'Backspace') && item) {
      if (state.confirm !== item.id) { state.confirm = item.id; return note(`DELETE ${item.name.toUpperCase()}? PRESS DELETE AGAIN`); }
      await library.remove(item.id);
      state.confirm = null;
      state.songs = await library.list();
      state.songCur = Math.min(state.songCur, state.songs.length - 1);
      if (item.id === song.value.id) load(state.songs.length ? await library.load(state.songs[0].id) : library.migrate({}));
      return note('DELETED');
    } else if (k === 'e' && item) {
      const s = await library.load(item.id);
      const url = URL.createObjectURL(new Blob([JSON.stringify(s, null, 1)], { type: 'application/json' }));
      Object.assign(document.createElement('a'), { href: url, download: `${s.name}.json` }).click();
      setTimeout(() => URL.revokeObjectURL(url), 1000);
      return note('EXPORTED');
    } else if (k === 'q' && item) {
      playlist.add(item.id);
      return note(`QUEUED ${item.name.toUpperCase()}: ${playlist.state.items.length} IN THE PLAYLIST (L SHOWS IT)`);
    } else if (k === 'l') {
      return openPlaylist(true);
    } else if (k === 'w' && item) {
      const s = item.id === song.value.id ? song.value : await library.load(item.id);
      if (s) exportWav(s);
      return;
    } else if (k === 'a' && list.length) {
      const all = (await Promise.all(list.map(({ id }) => library.load(id)))).filter(Boolean);
      const url = URL.createObjectURL(new Blob([library.allSongsFile(all)], { type: 'application/json' }));
      Object.assign(document.createElement('a'), { href: url, download: `Orange Mouse songs ${new Date().toISOString().slice(0, 10)}.json` }).click();
      setTimeout(() => URL.revokeObjectURL(url), 1000);
      backup.exported();
      return note(`EXPORTED ${all.length} SONGS IN ONE FILE`);
    } else if (k === 'b') {
      if (!backup.canPick() && !backup.state.folder) return note('THIS BROWSER CAN\'T WRITE TO A FOLDER: A EXPORTS EVERY SONG');
      const ok = await backup.choose();
      return note(ok ? `BACKUP FOLDER ${backup.state.folder.name.toUpperCase()}: EVERY SONG COPIED` : 'NO BACKUP FOLDER CHOSEN');
    } else if (k === 'i') {
      const input = Object.assign(document.createElement('input'), { type: 'file', accept: '.json,application/json' });
      input.onchange = async () => {
        try {
          const songs = library.parseImport(await input.files[0].text());
          for (const s of songs) await library.save(s);
          load(songs[0]);
          await openSongs(true);
          if (songs.length > 1) note(`IMPORTED ${songs.length} SONGS`);
        } catch { note('THAT FILE IS NOT A SONG'); }
      };
      input.click();
      return;
    } else if (k === 'c' && item) {
      const code = await library.toCode(item.id === song.value.id ? song.value : await library.load(item.id));
      try { await navigator.clipboard.writeText(code); note(`SHARE CODE COPIED (${code.length} CHARACTERS)`); } catch { window.prompt('Share code', code); }
      return;
    } else if (k === 'v') {
      const code = window.prompt('Paste a share code');
      if (!code) return;
      try {
        const s = await library.fromCode(code);
        s.id = library.songId();
        await library.save(s);
        load(s);
        return openSongs(true);
      } catch { return note('THAT IS NOT A SONG CODE'); }
    } else return false;
    state.confirm = state.confirm && e.key !== 'Delete' && e.key !== 'Backspace' ? null : state.confirm;
    render();
    return true;
  }

  // ---- editing ----
  const clampRow = (r) => Math.max(0, Math.min(pat().rows - 1, r));
  const move = (dr, dk, extend = false) => {
    if (extend && !state.anchor) state.anchor = { ...state.cur };
    if (!extend) state.anchor = null;
    state.cur.r = clampRow(state.cur.r + dr);
    state.cur.k = Math.max(0, Math.min(TRACKS - 1, state.cur.k + dk));
    state.view = Math.floor(state.cur.k / SHOWN);
    render();
  };
  const apply = (changes) => {
    if (editor.apply(changes)) render();
  };

  const held = new Map();   // semitone -> { engine, part, note }, for the key release
  function audition(semi, on) {
    const t = song.value.tracks[state.cur.k];
    if (on) {
      if (t.engine === null || held.has(semi)) return;
      const note = 12 * (state.octs[state.cur.k] + 1) + semi;
      held.set(semi, { engine: t.engine, part: t.part, note });
      send({ type: 'note', engine: t.engine, part: t.part, note, velocity: 100, on: true });
    } else if (held.has(semi)) {
      const h = held.get(semi);
      send({ type: 'note', engine: h.engine, part: h.part, note: h.note, on: false });
      held.delete(semi);
    }
  }

  function writeNote(semi, stack) {
    const note = Math.min(127, 12 * (state.octs[state.cur.k] + 1) + semi);
    const old = cell();
    let next;
    if (stack && old && !old.off) {
      if (old.notes.includes(note) || old.notes.length >= CHORD) return;
      next = { ...old, notes: [...old.notes, note] };
    } else {
      next = { notes: [note], vel: old && !old.off ? old.vel : 0, retrig: old && !old.off ? old.retrig : 0 };
    }
    apply([{ p: state.p, row: state.cur.r, track: state.cur.k, cell: next }]);
    if (!stack) move(state.step, 0);
  }

  function setInCells(fn) {
    const box = sel() ?? { k0: state.cur.k, k1: state.cur.k, r0: state.cur.r, r1: state.cur.r };
    const changes = [];
    for (let r = box.r0; r <= box.r1; r++) {
      for (let k = box.k0; k <= box.k1; k++) changes.push({ p: state.p, row: r, track: k, cell: fn(cell(r, k)) });
    }
    apply(changes);
  }

  function copy(cut) {
    const box = sel() ?? { k0: state.cur.k, k1: state.cur.k, r0: state.cur.r, r1: state.cur.r };
    const cells = [];
    for (let r = box.r0; r <= box.r1; r++) {
      const line = [];
      for (let k = box.k0; k <= box.k1; k++) line.push(cell(r, k));
      cells.push(line);
    }
    state.clip = { k0: box.k0, cells };
    if (cut) setInCells(() => null);
    // the cursor to the block's top-left, so moving down and pasting repeats it
    if (state.anchor) { state.cur = { r: box.r0, k: box.k0 }; state.anchor = null; }
    render();
  }

  function paste(sameTracks) {
    if (!state.clip) return;
    const k0 = sameTracks ? state.clip.k0 : state.cur.k;
    const changes = [];
    state.clip.cells.forEach((line, i) => line.forEach((c, j) => {
      const r = state.cur.r + i, k = k0 + j;
      if (r < pat().rows && k < TRACKS) changes.push({ p: state.p, row: r, track: k, cell: c });
    }));
    apply(changes);
  }

  function selectAll() {
    const wholeTrack = state.anchor && state.anchor.r === 0 && state.cur.r === pat().rows - 1 && state.anchor.k === state.cur.k;
    if (wholeTrack) { state.anchor = { r: 0, k: 0 }; state.cur = { r: pat().rows - 1, k: TRACKS - 1 }; }
    else { state.anchor = { r: 0, k: state.cur.k }; state.cur = { r: pat().rows - 1, k: state.cur.k }; }
    render();
  }

  function undo(redo) {
    const step = redo ? editor.redo() : editor.undo();
    if (step) render();   // undo doesn't move the view (TRIS)
  }

  // ---- patterns, order, transport ----
  let loopMode = false;
  function choosePattern(d) {
    const s = song.value;
    let p = state.p + d;
    if (p >= s.patterns.length) {
      if (s.patterns.length >= PATTERNS) return;
      s.patterns.push(newPattern(pat().rows));
      send({ type: 'tracker', op: 'pattern', p: s.patterns.length - 1, rows: pat().rows, bpm: 0 });
      p = s.patterns.length - 1;
    }
    state.p = Math.max(0, p);
    state.cur.r = clampRow(state.cur.r);
    state.anchor = null;
    render();
  }
  function setOrder(order) {
    song.value.order = order;
    order.forEach((p, i) => send({ type: 'tracker', op: 'order', position: i, p }));
    send({ type: 'tracker', op: 'order', position: order.length, p: 0xff });
    render();
  }
  function play(mode) {
    stopPlaylist();   // playing by hand ends a playlist's run
    if (state.playing[0]) { send({ type: 'tracker', op: 'stop' }); return; }
    loopMode = mode === 'pattern';
    send({ type: 'tracker', op: 'play', mode, from: mode === 'song' ? state.pos : state.p });
  }
  function duplicatePattern() {
    const s = song.value;
    if (s.patterns.length >= PATTERNS) return;
    const copy = JSON.parse(JSON.stringify(pat()));
    s.patterns.push(copy);
    state.p = s.patterns.length - 1;
    for (const m of songMessages(s).filter((x) => x.op === 'pattern' || (x.op === 'cell' && x.p === state.p))) send(m);
    render();
  }
  function toggleMute(k, solo) {
    const t = song.value.tracks[k];
    if (solo) { t.solo = !t.solo; send({ type: 'tracker', op: 'solo', track: k, on: t.solo }); }
    else { t.mute = !t.mute; send({ type: 'tracker', op: 'mute', track: k, on: t.mute }); }
    render();
  }

  // ---- knobs: velocity, retrigger (the cell or the selection), song tempo, pattern length ----
  const KNOBS = [
    ['VEL', 'Velocity of the cell (or the selected cells): how hard the notes play.',
      (n) => setInCells((c) => (c && !c.off ? { ...c, vel: Math.max(1, Math.min(127, (c.vel || 100) + n)) } : c))],
    ['RTG', 'Retrigger: the cell plays 2 to 8 evenly spaced hits in its row (rolls, ratchets).',
      (n) => setInCells((c) => {
        if (!c || c.off) return c;
        const order = [0, 2, 3, 4, 5, 6, 7, 8];
        const i = Math.max(0, Math.min(order.length - 1, order.indexOf(c.retrig >= 2 ? c.retrig : 0) + Math.sign(n)));
        return { ...c, retrig: order[i] };
      })],
    ['BPM', 'The tempo of the pattern you edit: each pattern has its own (new ones start at the song\'s).',
      (n) => {
        const pt = pat();
        pt.bpm = Math.max(20, Math.min(300, (pt.bpm || song.value.bpm) + n));
        send({ type: 'tracker', op: 'pattern', p: state.p, rows: pt.rows, bpm: pt.bpm });
        render();
      }],
    ['LEN', 'The pattern\'s length in rows (sixteenths): 16 is a bar, 64 four bars.',
      (n) => { pat().rows = Math.max(1, Math.min(MAX_ROWS, pat().rows + n)); send({ type: 'tracker', op: 'pattern', p: state.p, rows: pat().rows, bpm: pat().bpm }); state.cur.r = clampRow(state.cur.r); render(); }],
  ];
  const knobs = el.querySelector('.tracker-knobs');
  KNOBS.forEach(([label, text, onStep]) => {
    const { wrap, pot } = makeKnob(label, { onStep: (s) => onStep(Math.sign(s) * Math.max(1, Math.abs(s) >> 1)) });
    const cap = document.createElement('span');
    cap.className = 'caption';
    cap.textContent = label;
    wrap.append(cap);
    describe(pot, help, `${label}: ${text}`);
    knobs.append(wrap);
  });

  // ---- pads ----
  const padsEl = el.querySelector('.tracker-pads');
  const pads = {};
  const PADS = [
    ['play', 'PLAY', 'Plays the song from the order position you chose (click it on the ORDER row), or stops (Space).', () => play('song')],
    ['loop', 'LOOP', 'Repeats the pattern you edit, or stops (Enter).', () => play('pattern')],
    ['stop', 'STOP', 'Stops and releases every note (and a playlist).', () => { stopPlaylist(); send({ type: 'tracker', op: 'stop' }); }],
    ['edit', 'EDIT', 'On: the keys write notes at the cursor. Off: they only play (Esc).', () => { state.edit = !state.edit; render(); }],
    ['patDown', 'PAT -', 'The previous pattern.', () => choosePattern(-1)],
    ['patUp', 'PAT +', 'The next pattern; past the last, a new empty one.', () => choosePattern(1)],
    ['ordAdd', 'ORD +', 'Adds the pattern you edit to the end of the order list (the song).', () => setOrder([...song.value.order, state.p])],
    ['ordDel', 'ORD -', 'Removes the last entry of the order list.', () => song.value.order.length > 1 && setOrder(song.value.order.slice(0, -1))],
    ['view', 'T 9-16', 'Shows tracks 9-16, or 1-8 again.', () => { state.view ^= 1; state.cur.k = state.view * SHOWN + (state.cur.k % SHOWN); render(); }],
    ['octDown', 'OCT -', 'The keys play an octave lower ([).', () => { state.octs[state.cur.k] = Math.max(0, state.octs[state.cur.k] - 1); render(); }],
    ['octUp', 'OCT +', 'The keys play an octave higher (]).', () => { state.octs[state.cur.k] = Math.min(8, state.octs[state.cur.k] + 1); render(); }],
    ['step', 'STEP', 'How far the cursor moves after a note: 0, 1, 2, 4, 8 (- and =).', () => { state.step = STEPS[(STEPS.indexOf(state.step) + 1) % STEPS.length]; render(); }],
    ['songs', 'SONGS', 'The library on the display: load, new, duplicate, rename, delete, export, import, share codes, the backup folder.', () => openSongs()],
  ];
  for (const [id, label, text, action] of PADS) {
    const pad = makePad(label, { kind: 'small', onChange: (down) => down && action() });
    describe(pad, help, `${label}: ${text}`);
    pads[id] = pad;
    padsEl.append(pad);
  }

  // ---- the display under the pointer: cursor, selection, mute, and its own help ----
  let hover = null;   // { r, k, header }
  const hit = (e) => {
    const box = canvas.getBoundingClientRect();
    const col = Math.floor(((e.clientX - box.left) * (canvas.width / box.width) - display.margin) / display.charW);
    const row = Math.floor(((e.clientY - box.top) * (canvas.height / box.height) - display.margin) / display.charH);
    if (row === ROWS - 2) {
      const i = orderStarts.findIndex((start) => col >= start && col < start + 2);
      return i >= 0 ? { order: i } : null;
    }
    if (col < 3) return null;
    const k = state.view * SHOWN + Math.min(SHOWN - 1, Math.floor((col - 3) / CELL));
    if (row === 1) return { header: true, k };
    const r = state.top + row - FIRST;
    return row >= FIRST && row < FIRST + VISIBLE && r < pat().rows ? { r, k } : null;
  };
  let dragging = false;
  canvas.addEventListener('pointerdown', (e) => {
    if (state.mode === 'songs') return;
    const h = hit(e);
    if (!h) return;
    e.preventDefault();
    if (h.order !== undefined) {   // choose where PLAY starts, and edit that pattern
      state.pos = h.order;
      state.p = song.value.order[h.order];
      state.cur.r = clampRow(state.cur.r);
      render();
      return;
    }
    if (h.header) { toggleMute(h.k, e.button === 2); return; }
    if (e.button !== 0) return;
    canvas.setPointerCapture(e.pointerId);
    state.anchor = e.shiftKey ? (state.anchor ?? { ...state.cur }) : null;
    state.cur = { r: h.r, k: h.k };
    dragging = true;
    render();
  });
  canvas.addEventListener('pointermove', (e) => {
    hover = state.mode === 'songs' ? null : hit(e);
    if (dragging && hover && hover.r !== undefined) {
      if (!state.anchor) state.anchor = { ...state.cur };
      state.cur = { r: hover.r, k: hover.k };
      render();
    } else {
      help.refresh();
    }
  });
  canvas.addEventListener('pointerup', () => {
    dragging = false;
    if (state.anchor && state.anchor.r === state.cur.r && state.anchor.k === state.cur.k) { state.anchor = null; render(); }
  });
  canvas.addEventListener('contextmenu', (e) => e.preventDefault());
  describe(canvas, help, () => {
    if (state.mode === 'playlist') return 'The playlist: arrows choose, Enter plays from that song, Shift+arrows move it, Delete removes it, - and = set the gap between songs. Each song plays to its end, then the next. STOP ends it. L or Esc goes back.';
    if (state.mode === 'songs') return 'The songs: Q queues one in the playlist, L shows it; arrows choose, Enter loads, N new, D duplicate, R rename, Delete twice deletes, E/I export/import .json, C/V share code, W renders it as a WAV file, A exports every song, B picks a backup folder. Esc goes back.';
    if (!hover) return 'The pattern: click a cell to move there, drag to select. Click a track name to mute it, right-click to solo it.';
    if (hover.order !== undefined) return `Order position ${hover.order + 1}: pattern ${hex(song.value.order[hover.order])}. Click: PLAY starts here, and you edit that pattern.`;
    const t = song.value.tracks[hover.k];
    const where = t.engine === null ? 'not playing anything yet' : `plays ${ENGINES[t.engine].name} ${ENGINES[t.engine].parts[t.part]}`;
    if (hover.header) return `Track ${hover.k + 1} ${t.name} (${where}): click to mute, right-click to solo.${t.mute ? ' Muted.' : ''}${t.solo ? ' Solo.' : ''}`;
    return `Track ${hover.k + 1} row ${hex(hover.r)}: ${cellWords(getCell(song.value, state.p, hover.r, hover.k))}.`;
  });

  // ---- the computer keyboard (while the tracker is shown) ----
  function handleKey(e, down) {
    const mod = e.metaKey || e.ctrlKey;
    if (state.mode === 'songs' || state.mode === 'playlist') {
      if (!down || mod || e.altKey) return false;
      if (state.mode === 'playlist') playlistKey(e); else songKey(e);
      return true;
    }
    if (!down) {
      if (e.code in KEY_SEMITONES) { audition(KEY_SEMITONES[e.code], false); return true; }
      return false;
    }
    if (mod) {
      const k = e.key.toLowerCase();
      if (k === 'z') { undo(e.shiftKey); return true; }
      if (k === 'y') { undo(true); return true; }
      if (k === 'c') { copy(false); return true; }
      if (k === 'x') { copy(true); return true; }
      if (k === 'v') { paste(e.shiftKey); return true; }
      if (k === 'a') { selectAll(); return true; }
      if (k === 'd') { duplicatePattern(); return true; }
      return false;
    }
    if (e.altKey) return false;
    if (e.code === 'KeyA') {   // note off (A is not a note key)
      if (state.edit && !e.repeat) {
        apply([{ p: state.p, row: state.cur.r, track: state.cur.k, cell: { off: true } }]);
        move(state.step, 0);
      }
      return true;
    }
    if (e.code in KEY_SEMITONES) {
      if (!e.repeat) {
        const semi = KEY_SEMITONES[e.code];
        audition(semi, true);
        if (state.edit) writeNote(semi, e.shiftKey);
      }
      return true;
    }
    switch (e.key) {
      case 'ArrowUp': move(-1, 0, e.shiftKey); return true;
      case 'ArrowDown': move(1, 0, e.shiftKey); return true;
      case 'ArrowLeft': move(0, -1, e.shiftKey); return true;
      case 'ArrowRight': move(0, 1, e.shiftKey); return true;
      case 'PageUp': move(-16, 0, e.shiftKey); return true;
      case 'PageDown': move(16, 0, e.shiftKey); return true;
      case 'Home': move(-MAX_ROWS, 0, e.shiftKey); return true;
      case 'End': move(MAX_ROWS, 0, e.shiftKey); return true;
      case ' ': if (!e.repeat) play('song'); return true;
      case 'Enter': if (!e.repeat) play('pattern'); return true;
      case 'Escape':
        if (state.anchor) state.anchor = null; else state.edit = !state.edit;
        render();
        return true;
      case 'Backspace':
        if (state.edit) {
          if (state.anchor) setInCells(() => null);
          else { apply([{ p: state.p, row: state.cur.r, track: state.cur.k, cell: null }]); move(-state.step, 0); }
        }
        return true;
      case 'Delete':
        if (state.edit) { setInCells(() => null); if (!state.anchor) move(state.step, 0); }
        return true;
      case '[': state.octs[state.cur.k] = Math.max(0, state.octs[state.cur.k] - 1); render(); return true;
      case ']': state.octs[state.cur.k] = Math.min(8, state.octs[state.cur.k] + 1); render(); return true;
      case '-': state.step = STEPS[Math.max(0, STEPS.indexOf(state.step) - 1)]; render(); return true;
      case '=': state.step = STEPS[Math.min(STEPS.length - 1, STEPS.indexOf(state.step) + 1)]; render(); return true;
    }
    return false;
  }

  // ---- the song, and the worklet ----
  function load(s) {
    if (!fromPlaylist) stopPlaylist();   // another song, by hand
    if (song.value) sendToWorklet({ type: 'tracker', op: 'stop' });
    song.value = s;
    editor = new Editor(s, send);
    state.p = s.order[0] ?? 0;
    state.pos = 0;
    state.cur = { r: 0, k: 0 };
    state.anchor = null;
    for (const m of songMessages(s)) send(m);
    render();
    loadListeners.forEach((fn) => fn(s));
  }
  const loadListeners = [];

  // where the worklet's tracker plays: [playing, pattern, order position, row, bpm].
  // While the song plays, the display follows it into each next pattern.
  function onState(tracker) {
    // a playlist's song ended on its own (a stop by hand ended the run first): the next, after the gap
    if (state.playing[0] && !tracker[0] && playlist.state.active) {
      clearTimeout(gapTimer);
      gapTimer = setTimeout(() => { if (playlist.next()) startEntry(playlist.state.current); else render(); }, playlist.state.gap * 1000);
    }
    const playedRow = tracker[3] !== state.playing[3] || tracker[0] !== state.playing[0];
    const nextPattern = tracker[0] && tracker[1] !== state.p && song.value.patterns[tracker[1]];
    state.playing = tracker;
    if (nextPattern) {
      state.p = tracker[1];
      state.pos = tracker[2];
      state.anchor = null;
      state.cur.r = clampRow(state.cur.r);
    }
    if (playedRow || nextPattern) render();
  }

  addGuide(el, el.querySelector('.device-head'), GUIDE, help);
  backup.onChange(() => render());
  // The LXR's kit as the worklet reports it (its panel or SD card changed it):
  // the song keeps it, and saves.
  function keepLxrKit(values) {
    const s = song.value;
    if (s.lxr && s.lxr.length === values.length && s.lxr.every((v, i) => v === values[i])) return;
    s.lxr = values;
    clearTimeout(saveTimer);
    saveTimer = setTimeout(() => library.save(song.value), 800);
  }

  // send: changes to the song (the mixer's too) go through it, so they save
  return { el, load, onState, handleKey, send, keepLxrKit, exportWav, playlist, onLoad: (fn) => loadListeners.push(fn), song: () => song.value, summary: () => [
    song.value.name.toUpperCase(),
    `P${hex(state.p)} ${state.playing[0] ? '▶ ROW ' + hex(state.playing[3]) : 'STOPPED'}`,
  ] };
}
