// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The Plaits panel (PLAN.md Phase 5): Mutable Instruments' macro oscillator,
// with the engines the genres use and presets for each genre.
//
//   display  row 0: the part, its preset (* once changed) and engine; rows
//            1-7: every preset, a column per genre, the chosen one lit
//            (click one to load it); rows 9-11: what the knobs change now, in
//            this engine's words, and their values
//   knobs    a page of four: sound (harmonics, timbre, morph, decay),
//            envelope (attack, LPG colour, timbre and morph envelope),
//            output (volume, transpose, engine)
//   pads     PL1, PL2, PAGE, engine and preset back/forward, octave
//   keybed   plays the part, as do the computer keys while Plaits shows
//
// The parts' settings live in the song (song.plaits), so they save with it.

import registry from '../registry/plaits.js';
import { formatShort, formatValue, helpLine } from '../registry/format.js';
import { DotMatrix } from '../kit/dotmatrix.js';
import { describe } from '../kit/help.js';
import { makePad, makeKnob, setLed } from '../kit/controls.js';
import { makeKeybed } from '../kit/keybed.js';
import { addGuide } from '../kit/guide.js';
import { PLAITS, plaitsMessage } from '../tracker/song.js';

const COLS = 44;
const ROWS = 12;
const COL_W = 11;
const GENRES = ['TRANCE', 'ELECTRO', 'SYNTHWAVE', 'DUB TECHNO'];
const P = Object.fromEntries(registry.params.map((p, i) => [p.id.replace('PL_P_', ''), i]));
const PAGES = [
  ['SOUND', [P.HARMONICS, P.TIMBRE, P.MORPH, P.DECAY]],
  ['ENVELOPE', [P.ATTACK, P.COLOUR, P.TIMBRE_ENV, P.MORPH_ENV]],
  ['OUTPUT', [P.VOLUME, P.TRANSPOSE, P.ENGINE, null]],
];
// what HARMONICS, TIMBRE and MORPH do on each engine: short (for the
// display) and in words (for the help strip), after Plaits' manual
const MEANING = [
  [['reso', 'the filter\'s resonance and slope'], ['cutoff', 'the filter\'s cutoff'], ['wav+sub', 'the wave (saw to pulse) and the sub-oscillator']],
  [['detune', 'the detuning between its two waves'], ['pulse', 'the pulse: narrow, square, then hard-sync formants'], ['saw', 'the saw: triangle to saw, then a notch']],
  [['wave', 'the waveshaper\'s curve'], ['fold', 'how hard the wave folds'], ['asym', 'the wave\'s asymmetry']],
  [['ratio', 'the frequency ratio of the two operators'], ['index', 'the modulation index: brightness'], ['fdbk', 'the feedback: grit']],
  [['freq', 'the distortion\'s frequency'], ['amount', 'how much distortion'], ['asym', 'the asymmetry: a resonant sweep']],
  [['bank', 'the wavetable bank'], ['row', 'the row in the bank'], ['column', 'the column in the bank']],
  [['chord', 'the chord: octave, fifth, minor, seventh...'], ['invert', 'the inversion and voicing'], ['wave', 'the waveform: strings and organ to wavetables']],
  [['chord', 'the chord the machine plays on each key'], ['ensmbl', 'the filter, then the ensemble chorus'], ['regist', 'the registration: which footages sound']],
  [['synth', 'the voice: formants, SAM, then LPC words'], ['specie', 'the species: deeper or higher voice'], ['phonem', 'the vowel or the word']],
  [['matrl', 'the material: strings to bells to plates'], ['bright', 'the brightness and the strike'], ['decay', 'how long it rings']],
  [['random', 'how random the particles\' pitch is'], ['dense', 'how dense the particles are'], ['filter', 'the filter: all-pass to band-pass']],
];
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

const GUIDE = [
  { title: 'Choose a sound', text: 'The display lists every preset, a column per genre: trance, electro, synthwave, dub techno. Click one, or PRE &lt; / PRE &gt; (up and down arrows; left and right change the column).' },
  { title: 'Play', text: 'Keys Z to M and Q to I, or the keybed; OCT - / OCT + (or [ and ]) move the octave. Each part plays chords of up to four notes, from the tracker (tracks 14 and 15) too.' },
  { title: 'Shape it', text: 'Plaits has three knobs that mean something different on each engine: HARMONICS, TIMBRE, MORPH. The display names them for the engine you hear. PAGE switches the knobs between sound, envelope and output. ENG &lt; / ENG &gt; change the engine.' },
  { title: 'Envelope', text: 'DECAY sets how notes fade after their note off; ATTACK how they swell in. LPG colour makes the fade dark like a filter or clean like a VCA. The timbre and morph envelopes move those knobs with each note: a pluck that opens.' },
  { title: 'Saved with the song', text: 'Each part\'s settings are part of the song. A * after the preset name means you changed it.' },
];

export function createPlaitsDevice({ send, help, song }) {
  const state = { part: 0, page: 0, octave: 3, presets: [] };
  const part = () => song().plaits[state.part].p;
  const meaning = (param) => {
    const i = [P.HARMONICS, P.TIMBRE, P.MORPH].indexOf(param);
    return i < 0 ? null : MEANING[part()[P.ENGINE]][i];
  };
  const edited = () => {
    const pr = state.presets[part()[P.PRESET]];
    return pr && pr.params.some((v, i) => v !== part()[i]);
  };

  const el = document.createElement('section');
  el.className = 'device plaits';
  el.setAttribute('aria-label', 'Plaits');
  el.tabIndex = -1;
  el.innerHTML = `
    <header class="device-head"><h2><b>Plaits</b></h2><span class="caption">macro oscillator · 2 parts</span><span class="spacer"></span></header>
    <div class="device-top">
      <div class="display-block">
        <canvas class="lcd plaits-lcd"></canvas>
      </div>
      <div class="device-pads">
        <div class="pots plaits-knobs"></div>
        <div class="group plaits-parts" aria-label="Parts"></div>
        <div class="group plaits-nav"></div>
      </div>
    </div>`;
  const canvas = el.querySelector('.plaits-lcd');
  const display = new DotMatrix(canvas, { cols: COLS, rows: ROWS, dot: 2, margin: 6 });
  let summary = () => {};

  // the presets in display order: per genre, top to bottom
  const byGenre = () => GENRES.map((_, g) => state.presets.map((pr, i) => ({ ...pr, index: i })).filter((pr) => pr.genre === g));

  function set(param, value) {
    const info = registry.params[param];
    value = Math.max(info.min, Math.min(info.max, value));
    if (value === part()[param]) return;
    part()[param] = value;
    send(plaitsMessage(state.part, param, value));
    render();
  }

  function loadPreset(i) {
    const pr = state.presets[i];
    if (!pr) return;
    pr.params.forEach((v, k) => { part()[k] = v; send(plaitsMessage(state.part, k, v)); });
    part()[P.PRESET] = i;
    send(plaitsMessage(state.part, P.PRESET, i));
    render();
  }

  // ---- the display ----
  function render() {
    const p = part();
    const pr = state.presets[p[P.PRESET]];
    const name = pr ? `#${String(p[P.PRESET] + 1).padStart(2, '0')} ${pr.name.toUpperCase()}${edited() ? ' *' : ''}` : '';
    const engine = formatValue(registry.params[P.ENGINE], p[P.ENGINE]).toUpperCase();
    display.uninvert();
    display.line(0, `PL${state.part + 1} ${name}`.padEnd(COLS - engine.length) + engine);
    display.line(1, GENRES.map((g) => g.padEnd(COL_W)).join(''));
    const columns = byGenre();
    for (let r = 0; r < 6; r++) {
      display.line(2 + r, columns.map((c) => (c[r]?.name ?? '').slice(0, COL_W - 1).padEnd(COL_W)).join(''));
      columns.forEach((c, g) => { if (c[r] && c[r].index === p[P.PRESET]) display.invert(2 + r, g * COL_W, COL_W - 1); });
    }
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
    summary([`PL${state.part + 1} ${formatShort(registry.params[P.ENGINE], p[P.ENGINE]).toUpperCase()}`, pr ? pr.name.toUpperCase() : '']);
    partPads.forEach((pad, i) => setLed(pad, i === state.part));
    captions.forEach((c, k) => { const param = PAGES[state.page][1][k]; c.textContent = param === null ? '-' : registry.params[param].short; });
    help.refresh();
  }

  // ---- knobs ----
  const knobs = el.querySelector('.plaits-knobs');
  const captions = [];
  for (let k = 0; k < 4; k++) {
    const param = () => PAGES[state.page][1][k];
    const { wrap, pot } = makeKnob(`Knob ${k + 1}`, {
      onStep: (steps) => { const q = param(); if (q !== null) set(q, part()[q] + steps); },
      onReset: () => {   // back to the preset's value
        const q = param();
        if (q === null) return;
        const pr = state.presets[part()[P.PRESET]];
        set(q, pr ? pr.params[q] : registry.params[q].default);
      },
    });
    describe(pot, help, () => {
      const q = param();
      if (q === null) return 'This knob does nothing on this page.';
      const m = meaning(q);
      const info = registry.params[q];
      return m ? `${info.short} ${info.name} ${formatValue(info, part()[q])}: on this engine, ${m[1]}.` : helpLine(info, part()[q]);
    });
    pot.addEventListener('focus', () => { const q = param(); pot.setAttribute('aria-valuetext', q === null ? 'unused' : formatValue(registry.params[q], part()[q])); });
    const caption = document.createElement('span');
    caption.className = 'caption';
    wrap.append(caption);
    captions.push(caption);
    knobs.append(wrap);
  }

  // ---- pads ----
  const partPads = registry.parts.map((name, i) => {
    const pad = makePad(name, { kind: 'voice va', onChange: (down) => { if (down) { state.part = i; render(); } } });
    describe(pad, help, `${name}: one of Plaits' two parts (tracker track ${14 + i}). Select it to choose its sound and play it.`);
    el.querySelector('.plaits-parts').append(pad);
    return pad;
  });
  const stepPreset = (d) => loadPreset((part()[P.PRESET] + d + state.presets.length) % Math.max(1, state.presets.length));
  const NAV = [
    ['PAGE', () => `What the knobs change: ${PAGES.map((x) => x[0].toLowerCase()).join(', ')}. Now: ${PAGES[state.page][0].toLowerCase()}.`,
      () => { state.page = (state.page + 1) % PAGES.length; render(); }],
    ['ENG &lt;', 'The previous engine.', () => set(P.ENGINE, part()[P.ENGINE] - 1)],
    ['ENG &gt;', 'The next engine.', () => set(P.ENGINE, part()[P.ENGINE] + 1)],
    ['PRE &lt;', 'The previous preset (Up arrow).', () => stepPreset(-1)],
    ['PRE &gt;', 'The next preset (Down arrow).', () => stepPreset(1)],
    ['OCT -', () => `Keys play from C${state.octave} (Z) and C${state.octave + 1} (Q). Shortcut: [ and ].`, () => octave(-1)],
    ['OCT +', () => `Keys play from C${state.octave} (Z) and C${state.octave + 1} (Q). Shortcut: [ and ].`, () => octave(1)],
  ];
  for (const [label, text, action] of NAV) {
    const pad = makePad(label, { kind: 'small', onChange: (down) => down && action() });
    describe(pad, help, text);
    el.querySelector('.plaits-nav').append(pad);
  }

  // ---- the display: click a preset ----
  const cellAt = (e) => {
    const rect = canvas.getBoundingClientRect();
    return {
      col: Math.floor(((e.clientX - rect.left) / rect.width) * canvas.width / display.charW),
      row: Math.floor(((e.clientY - rect.top) / rect.height) * canvas.height / display.charH),
    };
  };
  const presetAt = ({ col, row }) => (row >= 2 && row < 8 ? byGenre()[Math.floor(col / COL_W)]?.[row - 2] ?? null : null);
  let hover = null;
  canvas.addEventListener('pointerdown', (e) => { const pr = presetAt(cellAt(e)); if (pr) loadPreset(pr.index); });
  canvas.addEventListener('pointermove', (e) => { hover = presetAt(cellAt(e)); help.refresh(); });
  canvas.addEventListener('pointerleave', () => { hover = null; });
  describe(canvas, help, () => (hover
    ? `${hover.name} (${GENRES[hover.genre].toLowerCase()}): click to load it on PL${state.part + 1}.`
    : 'Every preset, a column per genre. Click one to load it, then play the keys.'));

  // ---- keys ----
  const held = new Map();
  function noteFromKey(semi, on) {
    if (on) {
      if (held.has(semi)) return;
      const note = 12 * (state.octave + 1) + semi;
      held.set(semi, note);
      send({ type: 'note', engine: PLAITS, part: state.part, note, velocity: 100, on: true });
    } else if (held.has(semi)) {
      send({ type: 'note', engine: PLAITS, part: state.part, note: held.get(semi), on: false });
      held.delete(semi);
    }
    keybed.light(semi, on);
  }
  const keybed = makeKeybed({
    onNote: noteFromKey,
    describe: (key, semi, letter) => describe(key, help, () => {
      const note = 12 * (state.octave + 1) + semi;
      return `Key ${NOTE_NAMES[note % 12]}${Math.floor(note / 12) - 1}: plays PL${state.part + 1}. Computer key ${letter}.`;
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

  function onMessage(msg) {
    if (msg.type === 'plaitsPresets') {
      state.presets = msg.presets;
      render();
    }
  }

  addGuide(el, el.querySelector('.device-head'), GUIDE, help);
  return { el, render, onMessage, noteFromKey, octave, handleKey, presets: () => state.presets, onSummary: (fn) => { summary = fn; render(); } };
}
