// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The Dexed panel (PLAN.md Phase 4): DX7 sounds from Dexed's cartridges,
// presets only, as on a DX7 played from cartridges.
//
//   display  row 0: the part, the bank, volume and transpose; rows 2-9: the
//            bank's 32 programs in four columns, the chosen one lit. Click a
//            name to choose it.
//   knobs    bank, program, volume, transpose
//   pads     DX1, DX2 (the part), bank and program back/forward, octave
//   keybed   plays the part, as does the computer keyboard while Dexed shows
//
// The parts' choices live in the song (song.dexed), so they save with it.

import registry from '../registry/dexed.js';
import { formatValue, helpLine } from '../registry/format.js';
import { DotMatrix } from '../kit/dotmatrix.js';
import { describe } from '../kit/help.js';
import { makePad, makeKnob, setLed } from '../kit/controls.js';
import { makeKeybed } from '../kit/keybed.js';
import { addGuide } from '../kit/guide.js';
import { DEXED, dexedMessage } from '../tracker/song.js';

const COLS = 44;
const ROWS = 10;
const NAME_W = 11;            // a column of program names: 10 characters and a space
const FIRST = 2;              // the first row of names
const FIELDS = ['bank', 'program', 'volume', 'transpose'];
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

const GUIDE = [
  { title: 'Choose a sound', text: 'The display lists the bank\'s 32 sounds. Click one, or use PRG &lt; and PRG &gt; (the up and down arrows). BNK &lt; and BNK &gt; (left and right arrows) change the bank: Dexed 01 and the 32 SynprezFM cartridges, over a thousand DX7 sounds.' },
  { title: 'Play', text: 'Keys Z to M play the lower octave, Q to I the upper; or click the keys. OCT - and OCT + (or [ and ]) move both octaves.' },
  { title: 'Two parts', text: 'DX1 and DX2 are two separate DX7s, each with its own sound, on tracker tracks 12 and 13 and on their own mixer strips.' },
  { title: 'Fit it in', text: 'DX7 sounds differ a lot in loudness: VOL evens them out. TRN moves a sound by semitones, for a bass an octave down or a bell an octave up.' },
  { title: 'Saved with the song', text: 'Each part\'s bank, sound, volume and transpose are part of the song.' },
];

export function createDexedDevice({ send, help, song }) {
  const state = {
    part: 0,
    octave: 3,
    banks: [],      // from the worklet: [{ name, programs: [32 names] }]
  };
  const part = () => song().dexed[state.part];

  const el = document.createElement('section');
  el.className = 'device dexed';
  el.setAttribute('aria-label', 'Dexed');
  el.tabIndex = -1;
  el.innerHTML = `
    <header class="device-head"><h2><b>Dexed</b></h2><span class="caption">DX7 sounds · 2 parts</span><span class="spacer"></span></header>
    <div class="device-top">
      <div class="display-block">
        <canvas class="lcd dexed-lcd"></canvas>
      </div>
      <div class="device-pads">
        <div class="pots dexed-knobs"></div>
        <div class="group dexed-parts" aria-label="Parts"></div>
        <div class="group dexed-nav"></div>
      </div>
    </div>`;
  const canvas = el.querySelector('.dexed-lcd');
  const display = new DotMatrix(canvas, { cols: COLS, rows: ROWS, dot: 2, margin: 6 });
  let summary = () => {};

  const bankName = (b) => state.banks[b]?.name ?? `Bank ${b + 1}`;
  const programName = (b, p) => (state.banks[b]?.programs[p] ?? '').trim();

  function set(field, value) {
    const info = registry.params[FIELDS.indexOf(field)];
    const max = field === 'bank' && state.banks.length ? state.banks.length - 1 : info.max;
    value = Math.max(info.min, Math.min(max, value));
    if (value === part()[field]) return;
    part()[field] = value;
    send(dexedMessage(state.part, field, value));
    render();
  }

  // ---- the display ----
  function render() {
    const p = part();
    const vol = formatValue(registry.params[2], p.volume);
    const trn = formatValue(registry.params[3], p.transpose);
    display.uninvert();
    display.line(0, `DX${state.part + 1} ${bankName(p.bank).toUpperCase()}`.padEnd(24) + `VOL ${vol}  TRN ${trn}`.padStart(20));
    display.line(1, '');
    for (let r = 0; r < 8; r++) {
      let line = '';
      for (let c = 0; c < 4; c++) line += programName(p.bank, c * 8 + r).padEnd(NAME_W).slice(0, NAME_W);
      display.line(FIRST + r, line);
    }
    display.invert(FIRST + (p.program % 8), Math.floor(p.program / 8) * NAME_W, NAME_W - 1);
    display.draw();
    summary([`DX${state.part + 1} ${bankName(p.bank).toUpperCase()}`, `#${String(p.program + 1).padStart(2, '0')} ${programName(p.bank, p.program).toUpperCase()}`]);
    partPads.forEach((pad, i) => setLed(pad, i === state.part));
    help.refresh();
  }

  // ---- knobs ----
  const knobs = el.querySelector('.dexed-knobs');
  FIELDS.forEach((field, k) => {
    const info = registry.params[k];
    const { wrap, pot } = makeKnob(info.name, {
      onStep: (steps) => set(field, part()[field] + steps),
      onReset: () => set(field, info.default),
    });
    describe(pot, help, () => {
      const v = part()[field];
      if (field === 'bank') return `${helpLine(info, v)} Now: ${bankName(v)}.`;
      if (field === 'program') return `PRG Program #${v + 1} ${programName(part().bank, v)}: one of the bank's 32 sounds.`;
      return helpLine(info, v);
    });
    pot.addEventListener('focus', () => pot.setAttribute('aria-valuetext', formatValue(info, part()[field])));
    const caption = document.createElement('span');
    caption.className = 'caption';
    caption.textContent = info.short;
    wrap.append(caption);
    knobs.append(wrap);
  });

  // ---- pads ----
  const partPads = registry.parts.map((name, i) => {
    const pad = makePad(name, {
      kind: 'voice va',
      onChange: (down) => { if (down) { state.part = i; render(); } },
    });
    describe(pad, help, `${name}: one of Dexed's two parts (tracker track ${12 + i}). Select it to choose its sound and play it.`);
    el.querySelector('.dexed-parts').append(pad);
    return pad;
  });
  const nav = el.querySelector('.dexed-nav');
  const NAV = [
    ['BNK &lt;', 'The previous bank (Left arrow).', () => set('bank', part().bank - 1)],
    ['BNK &gt;', 'The next bank (Right arrow).', () => set('bank', part().bank + 1)],
    ['PRG &lt;', 'The previous sound (Up arrow).', () => set('program', part().program - 1)],
    ['PRG &gt;', 'The next sound (Down arrow).', () => set('program', part().program + 1)],
    ['OCT -', () => `Keys play from C${state.octave} (Z) and C${state.octave + 1} (Q). Shortcut: [ and ].`, () => octave(-1)],
    ['OCT +', () => `Keys play from C${state.octave} (Z) and C${state.octave + 1} (Q). Shortcut: [ and ].`, () => octave(1)],
  ];
  for (const [label, text, action] of NAV) {
    const pad = makePad(label, { kind: 'small', onChange: (down) => down && action() });
    describe(pad, help, text);
    nav.append(pad);
  }

  // ---- the display: click a program ----
  const cellAt = (e) => {
    const rect = canvas.getBoundingClientRect();
    return {
      col: Math.floor(((e.clientX - rect.left) / rect.width) * canvas.width / display.charW),
      row: Math.floor(((e.clientY - rect.top) / rect.height) * canvas.height / display.charH),
    };
  };
  const programAt = ({ col, row }) => {
    const r = row - FIRST, c = Math.floor(col / NAME_W);
    return r >= 0 && r < 8 && c >= 0 && c < 4 ? c * 8 + r : null;
  };
  let hover = null;
  canvas.addEventListener('pointerdown', (e) => {
    const p = programAt(cellAt(e));
    if (p !== null) set('program', p);
  });
  canvas.addEventListener('pointermove', (e) => { hover = programAt(cellAt(e)); help.refresh(); });
  canvas.addEventListener('pointerleave', () => { hover = null; });
  describe(canvas, help, () => (hover !== null
    ? `#${hover + 1} ${programName(part().bank, hover)}: click to play it on DX${state.part + 1}.`
    : 'The bank\'s 32 sounds. Click one to choose it, then play the keys.'));

  // ---- keys ----
  const held = new Map();   // semitone -> the note it started
  function noteFromKey(semi, on) {
    if (on) {
      if (held.has(semi)) return;
      const note = 12 * (state.octave + 1) + semi;
      held.set(semi, note);
      send({ type: 'note', engine: DEXED, part: state.part, note, velocity: 100, on: true });
    } else if (held.has(semi)) {
      send({ type: 'note', engine: DEXED, part: state.part, note: held.get(semi), on: false });
      held.delete(semi);
    }
    keybed.light(semi, on);
  }
  const keybed = makeKeybed({
    onNote: noteFromKey,
    describe: (key, semi, letter) => describe(key, help, () => {
      const note = 12 * (state.octave + 1) + semi;
      return `Key ${NOTE_NAMES[note % 12]}${Math.floor(note / 12) - 1}: plays DX${state.part + 1}. Computer key ${letter}.`;
    }),
  });
  el.append(keybed.el);

  function octave(d) {
    state.octave = Math.min(6, Math.max(0, state.octave + d));
    help.refresh();
  }

  // arrows browse; the note keys are handled by main.js (as for VA)
  function handleKey(e, down) {
    if (!down || e.metaKey || e.ctrlKey || e.altKey) return false;
    if (e.key === 'ArrowUp') set('program', part().program - 1);
    else if (e.key === 'ArrowDown') set('program', part().program + 1);
    else if (e.key === 'ArrowLeft') set('bank', part().bank - 1);
    else if (e.key === 'ArrowRight') set('bank', part().bank + 1);
    else return false;
    return true;
  }

  function onMessage(msg) {
    if (msg.type === 'dexedNames') {
      state.banks = msg.banks;
      render();
    }
  }

  addGuide(el, el.querySelector('.device-head'), GUIDE, help);
  return { el, render, onMessage, noteFromKey, octave, handleKey, onSummary: (fn) => { summary = fn; render(); } };
}
