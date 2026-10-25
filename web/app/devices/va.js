// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The Orange Mouse VA panel, built from the kit (kit/) and the parameter
// registry (registry/va.js). It shows one slot at a time:
//
//   display  row 0: the slot and the page; rows 1-2: the four knobs' labels and
//            values; row 3: the slot's preset (* once edited)
//   knobs    the four parameters on the display; double-click: the preset's value
//   help     the control under the pointer, spelled out on the studio's help
//            strip (passed in: one strip in the top bar for every device)
//   pads     8 pages (press again: the page's second half), the 4 slots,
//            preset back and forward, octave down and up
//   keybed   plays the slot, as does the computer keyboard while VA has focus

import registry from '../registry/va.js';
import { formatShort, formatValue, helpLine } from '../registry/format.js';
import { DotMatrix } from '../kit/dotmatrix.js';
import { describe } from '../kit/help.js';
import { makePad, makeKnob, setLed } from '../kit/controls.js';
import { makeKeybed } from '../kit/keybed.js';
import { addGuide } from '../kit/guide.js';

const ENGINE = 1;             // the studio's engine number (web/port/web_studio.c)
const COL = 5;                // display columns per knob
const PAGES = [
  ['OSC', 'Osc', 'the two oscillators: waves, tuning, pulse width, sync, octave'],
  ['MIX', 'Mixer', 'levels of the oscillators, sub-oscillator and noise; drive; analogue slop'],
  ['FLT', 'Filter', 'cutoff, resonance, type, envelope amount, key tracking, high-pass'],
  ['F.EG', 'Filter envelope', "the filter envelope's attack, decay, sustain and release"],
  ['A.EG', 'Amp envelope', 'the volume envelope and how much velocity changes the level'],
  ['LFO', 'LFO', 'the LFO: rate, wave, and how much it moves pitch, cutoff and pulse width'],
  ['VCE', 'Voice', 'mono, legato or poly; gate length; glide; unison'],
  ['OUT', 'Mix', 'preset and volume'],
];
const SLOT_HELP = [
  'VA1 Bass: mono basses. Select it to edit it and play it from the keys.',
  'VA2 Poly: chords, pads, plucks. Select it to edit it and play it from the keys.',
  'VA3 Lead: mono leads. Select it to edit it and play it from the keys.',
  'VA4 Noise: noises, risers and effects. Select it to edit it and play it.',
];
const VA_P_PRESET = registry.params.findIndex((p) => p.id === 'VA_P_PRESET');
const GUIDE = [
  { title: 'Choose a sound', text: 'Pick a slot: VA1 for basses, VA2 for chords and pads, VA3 for leads, VA4 for noises. PRE &lt; and PRE &gt; step through its presets.' },
  { title: 'Play', text: 'Keys Z to M play the lower octave, Q to I the upper; or click the keys. OCT - and OCT + (or [ and ]) move both octaves.' },
  { title: 'Shape it', text: 'The eight page pads choose what the four knobs change: oscillators, mixer, filter, the two envelopes, LFO, voice, and output. Press a page again for its second half (1/2 and 2/2 on the display).' },
  { title: 'Turn a knob', text: 'Drag up or scroll. Hold Shift for fine steps. Double-click to go back to the preset\'s value. A * after the preset name means you changed it.' },
  { title: 'Learn as you go', text: 'Point at any knob or pad: the help strip in the top bar says what it does and, for a knob, its value in real units.' },
];

// the parameters of each page, in registry order, without the unused ones
const pageParams = PAGES.map(([, group]) =>
  registry.params.map((p, i) => ({ ...p, index: i })).filter((p) => p.group === group && !p.unused));

const center = (s, width) => {
  const left = Math.floor((width - s.length) / 2);
  return ' '.repeat(Math.max(0, left)) + s;
};

export function createVaDevice({ send, help }) {
  const state = {
    slot: 0,
    page: 0,
    half: 0,
    octave: 3,                                     // the lower keybed octave starts at C3
    params: registry.parts.map(() => registry.params.map((p) => p.default)),
    presets: registry.parts.map(() => []),         // per slot: [{ name, params }]
  };

  const el = document.createElement('section');
  el.className = 'device va';
  el.setAttribute('aria-label', 'Orange Mouse VA');
  el.tabIndex = -1;
  el.innerHTML = `
    <header class="device-head"><h2>Orange Mouse <b>VA</b></h2><span class="caption">virtual analogue · 4 slots</span><span class="spacer"></span></header>
    <div class="device-top">
      <div class="display-block">
        <canvas class="lcd va-lcd"></canvas>
        <div class="pots va-knobs"></div>
      </div>
      <div class="device-pads">
        <div class="group va-pages" aria-label="Pages"></div>
        <div class="group va-slots" aria-label="Slots"></div>
        <div class="group va-nav"></div>
      </div>
    </div>`;
  let summary = () => {};   // the rack card's two rows
  const display = new DotMatrix(el.querySelector('.va-lcd'), { cols: 20, rows: 4 });

  const visible = () => pageParams[state.page].slice(state.half * 4, state.half * 4 + 4);
  const halves = () => Math.ceil(pageParams[state.page].length / 4);
  const value = (p) => state.params[state.slot][p.index];
  const preset = () => state.presets[state.slot][state.params[state.slot][VA_P_PRESET]];
  const edited = () => {
    const pr = preset();
    return pr && pr.params.some((v, i) => v !== state.params[state.slot][i]);
  };

  // ---- the display ----
  function render() {
    const slot = registry.parts[state.slot].toUpperCase();
    const page = PAGES[state.page][1].toUpperCase();
    const pages = halves() > 1 ? ` ${state.half + 1}/${halves()}` : '';
    display.line(0, slot + ' '.repeat(Math.max(1, 20 - slot.length - page.length - pages.length)) + page + pages);
    const ps = visible();
    let labels = '', values = '';
    for (let k = 0; k < 4; k++) {
      const p = ps[k];
      labels += center(p ? p.short.toUpperCase() : '', COL).padEnd(COL);
      values += center(p ? formatShort(p, value(p)) : '', COL).padEnd(COL);
    }
    display.line(1, labels);
    display.line(2, values);
    const pr = preset();
    const n = state.params[state.slot][VA_P_PRESET] + 1;
    display.line(3, pr ? `#${String(n).padStart(2, '0')} ${pr.name.toUpperCase()}${edited() ? ' *' : ''}` : '');
    display.draw();
    summary([slot, pr ? `#${String(n).padStart(2, '0')} ${pr.name.toUpperCase()}` : '']);
    help.refresh();
    pagePads.forEach((pad, i) => setLed(pad, i === state.page));
    slotPads.forEach((pad, i) => setLed(pad, i === state.slot));
  }

  // ---- knobs ----
  const knobs = el.querySelector('.va-knobs');
  for (let k = 0; k < 4; k++) {
    const setTo = (v) => {
      const p = visible()[k];
      if (!p) return;
      v = Math.min(p.max, Math.max(p.min, v));
      if (v === value(p)) return;
      state.params[state.slot][p.index] = v;
      send({ type: 'param', engine: ENGINE, part: state.slot, param: p.index, value: v });
      render();
    };
    const { wrap, pot } = makeKnob(`Knob ${k + 1}`, {
      onStep: (steps) => { const p = visible()[k]; if (p) setTo(value(p) + steps); },
      // back to the preset's value (the mix parameters, which presets leave alone: the default)
      onReset: () => {
        const p = visible()[k];
        if (!p) return;
        const pr = preset();
        setTo(pr && p.index < pr.params.length ? pr.params[p.index] : p.default);
      },
    });
    describe(pot, help, () => {
      const p = visible()[k];
      return p ? helpLine(p, value(p)) : 'This knob has no parameter on this page.';
    });
    const caption = document.createElement('span');
    caption.className = 'caption';
    caption.textContent = `Knob ${k + 1}`;
    wrap.append(caption);
    knobs.append(wrap);
    pot.setAttribute('aria-valuetext', '');
    pot.addEventListener('focus', () => {
      const p = visible()[k];
      pot.setAttribute('aria-valuetext', p ? `${p.name} ${formatValue(p, value(p))}` : 'unused');
    });
  }

  // ---- pads ----
  const pagePads = PAGES.map(([label, name, what], i) => {
    const pad = makePad(label, {
      kind: 'select',
      onChange: (down) => {
        if (!down) return;
        if (state.page === i) state.half = (state.half + 1) % halves();
        else { state.page = i; state.half = 0; }
        render();
      },
    });
    describe(pad, help, () => `${name} page: ${what}.${halves() > 1 && state.page === i ? ' Press again for the rest.' : ''}`);
    el.querySelector('.va-pages').append(pad);
    return pad;
  });
  const slotPads = registry.parts.map((name, i) => {
    const [short, alt] = name.split(' ');
    const pad = makePad(short, {
      alt,
      kind: 'voice va',
      onChange: (down) => {
        if (!down) return;
        state.slot = i;
        render();
      },
    });
    describe(pad, help, SLOT_HELP[i]);
    el.querySelector('.va-slots').append(pad);
    return pad;
  });
  const nav = el.querySelector('.va-nav');
  const stepPreset = (d) => {
    const count = state.presets[state.slot].length;
    if (!count) return;
    const next = (state.params[state.slot][VA_P_PRESET] + d + count) % count;
    state.params[state.slot][VA_P_PRESET] = next;
    send({ type: 'param', engine: ENGINE, part: state.slot, param: VA_P_PRESET, value: next });
    render();   // the worklet sends the preset's parameters back (vaParams)
  };
  const presetHelp = (d) => () => {
    const count = state.presets[state.slot].length;
    const next = (state.params[state.slot][VA_P_PRESET] + d + count) % Math.max(count, 1);
    const pr = state.presets[state.slot][next];
    return `${d < 0 ? 'Previous' : 'Next'} preset of ${registry.parts[state.slot]}${pr ? `: #${next + 1} ${pr.name}` : ''}.`;
  };
  for (const [label, d] of [['PRE <', -1], ['PRE >', 1]]) {
    const pad = makePad(label, { kind: 'small', onChange: (down) => down && stepPreset(d) });
    describe(pad, help, presetHelp(d));
    nav.append(pad);
  }
  for (const [label, d] of [['OCT -', -1], ['OCT +', 1]]) {
    const pad = makePad(label, {
      kind: 'small',
      onChange: (down) => { if (down) { state.octave = Math.min(6, Math.max(0, state.octave + d)); help.refresh(); } },
    });
    describe(pad, help, () => `Keys play from C${state.octave} (Z) and C${state.octave + 1} (Q). Shortcut: [ and ].`);
    nav.append(pad);
  }

  // ---- keys ----
  const held = new Map();   // semitone -> the note it started (so an octave change can't strand a note)
  function noteFromKey(semi, on) {
    if (on) {
      if (held.has(semi)) return;
      const note = 12 * (state.octave + 1) + semi;
      held.set(semi, note);
      send({ type: 'note', engine: ENGINE, part: state.slot, note, velocity: 100, on: true });
    } else if (held.has(semi)) {
      send({ type: 'note', engine: ENGINE, part: state.slot, note: held.get(semi), on: false });
      held.delete(semi);
    }
    keybed.light(semi, on);
  }
  const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const keybed = makeKeybed({
    onNote: noteFromKey,
    describe: (key, semi, letter) => describe(key, help, () => {
      const note = 12 * (state.octave + 1) + semi;
      return `Key ${NOTE_NAMES[note % 12]}${Math.floor(note / 12) - 1}: plays ${registry.parts[state.slot]}. Computer key ${letter}.`;
    }),
  });
  el.append(keybed.el);

  // ---- from the worklet ----
  function onMessage(msg) {
    if (msg.type === 'vaPresets') {
      state.presets = msg.slots;
      render();
    } else if (msg.type === 'vaParams') {
      state.params[msg.slot] = msg.params;
      if (msg.slot === state.slot) render();
    }
  }

  function octave(d) {
    state.octave = Math.min(6, Math.max(0, state.octave + d));
  }

  addGuide(el, el.querySelector('.device-head'), GUIDE, help);
  render();
  return { el, onMessage, noteFromKey, octave, onSummary: (fn) => { summary = fn; render(); } };
}
