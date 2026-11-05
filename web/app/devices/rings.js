// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The Rings panel (PLAN.md "Later", 2026-11-01): Mutable Instruments'
// resonator, one part (RN1, MIDI channel 16), with presets for each genre.
//
//   display  row 0: the part, its preset (* once changed) and model; rows
//            1-3: every preset, a column per genre, the chosen one lit
//            (click one to load it); row 5: what strikes it (its own exciter
//            or a channel); rows 9-11: what the knobs change now, in this
//            model's words, and their values
//   knobs    a page of four: sound (structure, brightness, damping,
//            position), setup (model, polyphony, input), output (volume,
//            transpose)
//   pads     RN1, PAGE, model and preset back/forward, octave
//   keybed   strums the part, as do the computer keys while Rings shows
//
// The part's settings live in the song (song.rings), so they save with it.

import registry from '../registry/rings.js';
import { ccHint, formatShort, formatValue, helpLine } from '../registry/format.js';
import { DotMatrix } from '../kit/dotmatrix.js';
import { describe } from '../kit/help.js';
import { makePad, makeKnob, setLed, setKnobValue } from '../kit/controls.js';
import { makeKeybed } from '../kit/keybed.js';
import { addGuide } from '../kit/guide.js';
import { RINGS, ringsMessage } from '../tracker/song.js';

const COLS = 44;
const ROWS = 12;
const COL_W = 11;
const GENRES = ['TRANCE', 'ELECTRO', 'SYNTHWAVE', 'DUB TECHNO'];
const P = Object.fromEntries(registry.params.map((p, i) => [p.id.replace('RN_P_', ''), i]));
const PAGES = [
  ['SOUND', [P.STRUCTURE, P.BRIGHTNESS, P.DAMPING, P.POSITION]],
  ['SETUP', [P.MODEL, P.POLYPHONY, P.INPUT, null]],
  ['OUTPUT', [P.VOLUME, P.TRANSPOSE, null, null]],
];
const PRESETS = registry.presets.length;
// what the four knobs do on each model: short (for the display) and in words
// (for the help strip), after Rings' manual
const MEANING = [
  [['matrl', 'the material: wood, then glass, then bells'], ['bright', 'the strike and the ringing: felt to metal'], ['decay', 'how long it rings'], ['strike', 'where it is struck: hollow in the middle, thin at the ends']],
  [['tuning', 'the sympathetic strings\' intervals: octaves, fifths, then clusters'], ['bright', 'the strike and the strings: dull to bright'], ['decay', 'how long the strings ring'], ['strike', 'where the strings are plucked']],
  [['stiff', 'the string\'s stiffness: a guitar, then a piano wire, then a bell'], ['bright', 'the pluck: soft to bright'], ['decay', 'how long the string rings'], ['pluck', 'where the string is plucked']],
  [['ratio', 'the two operators\' frequency ratio'], ['index', 'the modulation index: brightness'], ['decay', 'how long the note rings'], ['fdbk', 'the feedback: grit']],
  [['chord', 'the chord the strings are tuned to'], ['bright', 'the strike and the strings: dull to bright'], ['decay', 'how long the strings ring'], ['strike', 'where the strings are plucked']],
  [['stiff', 'the string\'s stiffness'], ['bright', 'the pluck and the reverb\'s tone'], ['reverb', 'the reverb\'s size and how long the string rings'], ['stereo', 'how wide the string sits between the speakers']],
];
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

const GUIDE = [
  { title: 'Choose a sound', text: 'The display lists every preset, a column per genre: trance, electro, synthwave, dub techno. Click one, or PRE &lt; / PRE &gt; (up and down arrows; left and right change the column).' },
  { title: 'Play', text: 'Keys Z to M and Q to I, or the keybed; OCT - / OCT + (or [ and ]) move the octave. Each note strikes the next voice and rings until it fades: there is no note off. Chords strum. MIDI channel 16 plays Rings too.' },
  { title: 'Shape it', text: 'Rings has four knobs: STRUCTURE, BRIGHTNESS, DAMPING, POSITION. What they do depends on the model (modal bars and bells, sympathetic strings, a plucked string, FM, chords, a string with reverb); the display names them. MDL &lt; / MDL &gt; change the model. Harder notes are brighter.' },
  { title: 'Feed it', text: 'PAGE to SETUP: IN chooses what strikes Rings. "own" is its built-in strike; a channel (a drum, VA2, PL1...) sends that sound through the resonator, tuned by the notes Rings plays. Mute the channel in the mixer to hear only the ringing.' },
  { title: 'Saved with the song', text: 'The part\'s settings are part of the song. A * after the preset name means you changed it.' },
];

export function createRingsDevice({ send, help, song }) {
  const state = { page: 0, octave: 3, presets: registry.presets };
  const part = () => song().rings[0].p;
  const meaning = (param) => {
    const i = [P.STRUCTURE, P.BRIGHTNESS, P.DAMPING, P.POSITION].indexOf(param);
    return i < 0 ? null : MEANING[part()[P.MODEL]][i];
  };
  // a preset sets the sound, not what strikes it
  const edited = () => {
    const pr = state.presets[part()[P.PRESET]];
    return pr && pr.params.some((v, i) => v !== part()[i]);
  };

  const el = document.createElement('section');
  el.className = 'device rings';
  el.setAttribute('aria-label', 'Rings');
  el.tabIndex = -1;
  el.innerHTML = `
    <header class="device-head"><h2><b>Rings</b></h2><span class="caption">resonator · 1 part, up to 4 voices</span><span class="spacer"></span></header>
    <div class="device-top">
      <div class="display-block">
        <canvas class="lcd rings-lcd"></canvas>
      </div>
      <div class="device-pads">
        <div class="pots rings-knobs"></div>
        <div class="group rings-parts" aria-label="Parts"></div>
        <div class="group rings-nav"></div>
      </div>
    </div>`;
  const canvas = el.querySelector('.rings-lcd');
  const display = new DotMatrix(canvas, { cols: COLS, rows: ROWS, dot: 2, margin: 6 });
  let summary = () => {};

  // the presets in display order: per genre, top to bottom
  const byGenre = () => GENRES.map((_, g) => state.presets.map((pr, i) => ({ ...pr, index: i })).filter((pr) => pr.genre === g));

  function set(param, value) {
    const info = registry.params[param];
    value = Math.max(info.min, Math.min(info.max, value));
    if (value === part()[param]) return;
    part()[param] = value;
    send(ringsMessage(0, param, value));
    render();
  }

  function loadPreset(i) {
    const pr = state.presets[i];
    if (!pr) return;
    pr.params.forEach((v, k) => { part()[k] = v; send(ringsMessage(0, k, v)); });
    part()[P.PRESET] = i;
    send(ringsMessage(0, P.PRESET, i));
    render();
  }

  // ---- the display ----
  function render() {
    const p = part();
    const pr = state.presets[p[P.PRESET]];
    const name = pr ? `#${String(p[P.PRESET] + 1).padStart(2, '0')} ${pr.name.toUpperCase()}${edited() ? ' *' : ''}` : '';
    const model = formatValue(registry.params[P.MODEL], p[P.MODEL]).toUpperCase();
    display.uninvert();
    display.line(0, `RN1 ${name}`.padEnd(COLS - model.length) + model);
    display.line(1, GENRES.map((g) => g.padEnd(COL_W)).join(''));
    const columns = byGenre();
    const rows = Math.max(...columns.map((c) => c.length));
    for (let r = 0; r < 3; r++) {
      display.line(2 + r, r < rows ? columns.map((c) => (c[r]?.name ?? '').slice(0, COL_W - 1).padEnd(COL_W)).join('') : '');
      columns.forEach((c, g) => { if (c[r] && c[r].index === p[P.PRESET]) display.invert(2 + r, g * COL_W, COL_W - 1); });
    }
    const input = registry.params[P.INPUT];
    display.line(5, p[P.INPUT] ? `STRUCK BY ${formatValue(input, p[P.INPUT]).toUpperCase()}: ITS SOUND RINGS THROUGH` : 'STRUCK BY ITS OWN EXCITER');
    display.line(6, `${formatValue(registry.params[P.POLYPHONY], p[P.POLYPHONY]).toUpperCase()}, NOTES RING UNTIL THEY FADE`);
    display.line(7, '');
    display.line(8, '');
    const [title, params] = PAGES[state.page];
    let labels = '', values = '';
    params.forEach((param) => {
      if (param === null) { labels += ''.padEnd(COL_W); values += ''.padEnd(COL_W); return; }
      const info = registry.params[param];
      const m = meaning(param);
      labels += `${info.short} ${m ? m[0] : ''}`.slice(0, COL_W - 1).padEnd(COL_W);
      values += `    ${formatShort(info, p[param])}`.slice(0, COL_W - 1).padEnd(COL_W);
    });
    display.line(9, labels);
    display.line(10, values);
    display.line(11, `KNOBS: ${title}  (PAGE ${state.page + 1}/${PAGES.length})`);
    display.draw();
    summary([`RN1 ${formatShort(registry.params[P.MODEL], p[P.MODEL]).toUpperCase()}`, pr ? pr.name.toUpperCase() : '']);
    setLed(partPad, true);
    captions.forEach((c, k) => { const param = PAGES[state.page][1][k]; c.textContent = param === null ? '-' : registry.params[param].short; });
    help.refresh();
  }

  // ---- knobs ----
  const knobs = el.querySelector('.rings-knobs');
  const captions = [];
  for (let k = 0; k < 4; k++) {
    const param = () => PAGES[state.page][1][k];
    const { wrap, pot } = makeKnob(`Knob ${k + 1}`, {
      onStep: (steps) => { const q = param(); if (q !== null) set(q, part()[q] + steps); },
      onReset: () => {   // back to the preset's value (INPUT: back to its own exciter)
        const q = param();
        if (q === null) return;
        const pr = state.presets[part()[P.PRESET]];
        set(q, pr && q < pr.params.length ? pr.params[q] : registry.params[q].default);
      },
    });
    describe(pot, help, () => {
      const q = param();
      if (q === null) return 'This knob does nothing on this page.';
      const m = meaning(q);
      const info = registry.params[q];
      return m ? `${info.short} ${info.name} ${formatValue(info, part()[q])}: on this model, ${m[1]}.${ccHint(info)}` : helpLine(info, part()[q]);
    });
    pot.addEventListener('focus', () => { const q = param(); setKnobValue(pot, q === null ? null : registry.params[q], q === null ? 0 : part()[q], q === null ? 'unused' : formatValue(registry.params[q], part()[q])); });
    const caption = document.createElement('span');
    caption.className = 'caption';
    wrap.append(caption);
    captions.push(caption);
    knobs.append(wrap);
  }

  // ---- pads ----
  const partPad = makePad(registry.parts[0], { kind: 'voice va', onChange: () => {} });
  describe(partPad, help, 'RN1: Rings\' one part (MIDI channel 16). Up to four voices ring at once.');
  el.querySelector('.rings-parts').append(partPad);
  const stepPreset = (d) => loadPreset((part()[P.PRESET] + d + PRESETS) % Math.max(1, PRESETS));
  const NAV = [
    ['PAGE', () => `What the knobs change: ${PAGES.map((x) => x[0].toLowerCase()).join(', ')}. Now: ${PAGES[state.page][0].toLowerCase()}.`,
      () => { state.page = (state.page + 1) % PAGES.length; render(); }],
    ['MDL &lt;', 'The previous model.', () => set(P.MODEL, part()[P.MODEL] - 1)],
    ['MDL &gt;', 'The next model.', () => set(P.MODEL, part()[P.MODEL] + 1)],
    ['PRE &lt;', 'The previous preset (Up arrow).', () => stepPreset(-1)],
    ['PRE &gt;', 'The next preset (Down arrow).', () => stepPreset(1)],
    ['OCT -', () => `Keys play from C${state.octave} (Z) and C${state.octave + 1} (Q). Shortcut: [ and ].`, () => octave(-1)],
    ['OCT +', () => `Keys play from C${state.octave} (Z) and C${state.octave + 1} (Q). Shortcut: [ and ].`, () => octave(1)],
  ];
  for (const [label, text, action] of NAV) {
    const pad = makePad(label, { kind: 'small', onChange: (down) => down && action() });
    describe(pad, help, text);
    el.querySelector('.rings-nav').append(pad);
  }

  // ---- the display: click a preset ----
  const cellAt = (e) => {
    const rect = canvas.getBoundingClientRect();
    return {
      col: Math.floor(((e.clientX - rect.left) / rect.width) * canvas.width / display.charW),
      row: Math.floor(((e.clientY - rect.top) / rect.height) * canvas.height / display.charH),
    };
  };
  const presetAt = ({ col, row }) => (row >= 2 && row < 5 ? byGenre()[Math.floor(col / COL_W)]?.[row - 2] ?? null : null);
  let hover = null;
  canvas.addEventListener('pointerdown', (e) => { const pr = presetAt(cellAt(e)); if (pr) loadPreset(pr.index); });
  canvas.addEventListener('pointermove', (e) => { hover = presetAt(cellAt(e)); help.refresh(); });
  canvas.addEventListener('pointerleave', () => { hover = null; });
  describe(canvas, help, () => (hover
    ? `${hover.name} (${GENRES[hover.genre].toLowerCase()}): click to load it on RN1.`
    : 'Every preset, a column per genre. Click one to load it, then play the keys.'));

  // ---- keys ----
  const held = new Map();
  function noteFromKey(semi, on) {
    if (on) {
      if (held.has(semi)) return;
      const note = 12 * (state.octave + 1) + semi;
      held.set(semi, note);
      send({ type: 'note', engine: RINGS, part: 0, note, velocity: 100, on: true });
    } else if (held.has(semi)) {
      send({ type: 'note', engine: RINGS, part: 0, note: held.get(semi), on: false });
      held.delete(semi);
    }
    keybed.light(semi, on);
  }
  const keybed = makeKeybed({
    onNote: noteFromKey,
    describe: (key, semi, letter) => describe(key, help, () => {
      const note = 12 * (state.octave + 1) + semi;
      return `Key ${NOTE_NAMES[note % 12]}${Math.floor(note / 12) - 1}: strikes RN1. Computer key ${letter}.`;
    }),
  });
  el.append(keybed.el);

  function octave(d) {
    state.octave = Math.min(6, Math.max(0, state.octave + d));
    help.refresh();
  }

  // arrows: up/down the presets, left/right the genres (the same row)
  function handleKey(e, down) {
    if (!down || e.metaKey || e.ctrlKey || e.altKey) return false;
    if (e.key === 'ArrowUp') stepPreset(-1);
    else if (e.key === 'ArrowDown') stepPreset(1);
    else if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
      const columns = byGenre();
      const cur = state.presets[part()[P.PRESET]];
      if (!cur) return true;
      const row = columns[cur.genre].findIndex((x) => x.index === part()[P.PRESET]);
      const g = (cur.genre + (e.key === 'ArrowRight' ? 1 : GENRES.length - 1)) % GENRES.length;
      const target = columns[g][Math.min(row, columns[g].length - 1)];
      if (target) loadPreset(target.index);
    } else return false;
    return true;
  }

  addGuide(el, el.querySelector('.device-head'), GUIDE, help);
  // parameters a CC moved (main.js)
  function followCcs(changes) {
    for (const { part: k, param, value } of changes) song().rings[k].p[param] = value;
    render();
  }

  return { el, render, followCcs, noteFromKey, octave, handleKey, onSummary: (fn) => { summary = fn; render(); } };
}
