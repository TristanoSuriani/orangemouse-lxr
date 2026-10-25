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
  { title: 'Play', text: 'Space plays the song from the start of the order list, or stops. Enter (LOOP) repeats the pattern you edit. PAT -/+ choose the pattern; PAT + past the last makes a new one. ORD + adds it to the order list.' },
  { title: 'Tracks', text: 'Tracks 1-7 play the LXR\'s voices, 8-11 VA\'s slots. Tab moves between tracks; T 9-16 shows the other eight. Click a track\'s name to mute it, right-click to solo it.' },
];

export function createTrackerDevice({ send, help }) {
  const song = { value: null };
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
  };

  const el = document.createElement('section');
  el.className = 'device tracker';
  el.setAttribute('aria-label', 'Tracker');
  el.tabIndex = -1;
  // the title and the ? pad share the control row: the display gets the height
  el.innerHTML = `
    <canvas class="lcd tracker-lcd"></canvas>
    <div class="tracker-controls">
      <div class="pots tracker-knobs"></div>
      <div class="group tracker-pads"></div>
      <header class="device-head"><h2><b>Tracker</b></h2></header>
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
    const bpm = pt.bpm || s.bpm;
    display.line(0, `PAT ${hex(state.p)}/${hex(s.patterns.length - 1)} LEN ${pt.rows} BPM ${Math.round(bpm)}${pt.bpm ? '*' : ''}  ${play.padEnd(16)}`
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
    display.line(ROWS - 2, order);
    if (state.playing[0]) display.invert(ROWS - 2, starts[state.playing[2]] ?? 0, 2);

    // the knobs' values
    const c = cell();
    const vel = c && !c.off ? (c.vel || 100) : '-';
    const rtg = c && !c.off ? (c.retrig >= 2 ? c.retrig : 'off') : '-';
    display.line(ROWS - 1, [`VEL ${vel}`, `RTG ${rtg}`, `BPM ${Math.round(s.bpm)}`, `LEN ${pt.rows}`].map((x) => x.padEnd(21)).join(''));
    display.draw();
    help.refresh();
    setLed(pads.edit, state.edit);
    setLed(pads.play, state.playing[0] && !loopMode);
    setLed(pads.loop, state.playing[0] && loopMode);
    setLed(pads.view, state.view === 1);
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
    if (state.playing[0]) { send({ type: 'tracker', op: 'stop' }); return; }
    loopMode = mode === 'pattern';
    send({ type: 'tracker', op: 'play', mode, from: mode === 'song' ? 0 : state.p });
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
    ['BPM', 'The song\'s tempo. A pattern can have its own (marked * on the display).',
      (n) => { song.value.bpm = Math.max(20, Math.min(300, song.value.bpm + n)); send({ type: 'tracker', op: 'tempo', bpm: song.value.bpm }); render(); }],
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
    ['play', 'PLAY', 'Plays the song from the start of the order list, or stops (Space).', () => play('song')],
    ['loop', 'LOOP', 'Repeats the pattern you edit, or stops (Enter).', () => play('pattern')],
    ['stop', 'STOP', 'Stops and releases every note.', () => send({ type: 'tracker', op: 'stop' })],
    ['edit', 'EDIT', 'On: the keys write notes at the cursor. Off: they only play (Esc).', () => { state.edit = !state.edit; render(); }],
    ['patDown', 'PAT -', 'The previous pattern.', () => choosePattern(-1)],
    ['patUp', 'PAT +', 'The next pattern; past the last, a new empty one.', () => choosePattern(1)],
    ['ordAdd', 'ORD +', 'Adds the pattern you edit to the end of the order list (the song).', () => setOrder([...song.value.order, state.p])],
    ['ordDel', 'ORD -', 'Removes the last entry of the order list.', () => song.value.order.length > 1 && setOrder(song.value.order.slice(0, -1))],
    ['view', 'T 9-16', 'Shows tracks 9-16, or 1-8 again.', () => { state.view ^= 1; state.cur.k = state.view * SHOWN + (state.cur.k % SHOWN); render(); }],
    ['octDown', 'OCT -', 'The keys play an octave lower ([).', () => { state.octs[state.cur.k] = Math.max(0, state.octs[state.cur.k] - 1); render(); }],
    ['octUp', 'OCT +', 'The keys play an octave higher (]).', () => { state.octs[state.cur.k] = Math.min(8, state.octs[state.cur.k] + 1); render(); }],
    ['step', 'STEP', 'How far the cursor moves after a note: 0, 1, 2, 4, 8 (- and =).', () => { state.step = STEPS[(STEPS.indexOf(state.step) + 1) % STEPS.length]; render(); }],
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
    if (col < 3) return null;
    const k = state.view * SHOWN + Math.min(SHOWN - 1, Math.floor((col - 3) / CELL));
    if (row === 1) return { header: true, k };
    const r = state.top + row - FIRST;
    return row >= FIRST && row < FIRST + VISIBLE && r < pat().rows ? { r, k } : null;
  };
  let dragging = false;
  canvas.addEventListener('pointerdown', (e) => {
    const h = hit(e);
    if (!h) return;
    e.preventDefault();
    if (h.header) { toggleMute(h.k, e.button === 2); return; }
    if (e.button !== 0) return;
    canvas.setPointerCapture(e.pointerId);
    state.anchor = e.shiftKey ? (state.anchor ?? { ...state.cur }) : null;
    state.cur = { r: h.r, k: h.k };
    dragging = true;
    render();
  });
  canvas.addEventListener('pointermove', (e) => {
    hover = hit(e);
    if (dragging && hover && !hover.header) {
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
    if (!hover) return 'The pattern: click a cell to move there, drag to select. Click a track name to mute it, right-click to solo it.';
    const t = song.value.tracks[hover.k];
    const where = t.engine === null ? 'not playing anything yet' : `plays ${ENGINES[t.engine].name} ${ENGINES[t.engine].parts[t.part]}`;
    if (hover.header) return `Track ${hover.k + 1} ${t.name} (${where}): click to mute, right-click to solo.${t.mute ? ' Muted.' : ''}${t.solo ? ' Solo.' : ''}`;
    return `Track ${hover.k + 1} row ${hex(hover.r)}: ${cellWords(getCell(song.value, state.p, hover.r, hover.k))}.`;
  });

  // ---- the computer keyboard (while the tracker is shown) ----
  function handleKey(e, down) {
    const mod = e.metaKey || e.ctrlKey;
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
      case 'Tab': move(0, e.shiftKey ? -1 : 1); return true;
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
    song.value = s;
    editor = new Editor(s, send);
    state.p = 0;
    state.cur = { r: 0, k: 0 };
    state.anchor = null;
    for (const m of songMessages(s)) send(m);
    render();
  }

  function onState(tracker) {
    const playedRow = tracker[3] !== state.playing[3] || tracker[0] !== state.playing[0];
    state.playing = tracker;
    if (playedRow) render();
  }

  addGuide(el, el.querySelector('.device-head'), GUIDE, help);
  return { el, load, onState, handleKey, song: () => song.value, summary: () => [
    `PAT ${hex(state.p)} ${state.playing[0] ? '▶ ROW ' + hex(state.playing[3]) : 'STOPPED'}`,
    `BPM ${Math.round(song.value.bpm)} ORD ${song.value.order.length}`,
  ] };
}
