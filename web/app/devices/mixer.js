// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The mixer (PLAN.md Phase 3): a channel strip for every engine's stem, the
// two send effects and the master, on one dot-matrix display.
//
//   display  row 0: the selected strip in words; row 1: the strips' names;
//            rows 2-9: meters (the ▶ is the fader); then level, pan, send A,
//            send B, mute/solo. FX A / FX B turn the display into that send
//            effect's page: its type, return and every setting.
//   knobs    the selected strip's level, pan, send A, send B, or with PAGE
//            its Surge filter: type, cutoff, resonance, and again its ducking:
//            amount, key, release (a send: return and
//            effect; the master: level); on an effect's page, four of its
//            settings (PAGE for the next four)
//   pads     strip back/forward, mute, solo, FX A, FX B, type -/+, page
//
// The mix lives in the song (song.mix, mixer/mix.js), so it saves with it.

import mixRegistry from '../registry/mix.js';
import fxRegistry from '../registry/fx.js';
import { formatShort, formatValue, helpLine } from '../registry/format.js';
import { DotMatrix } from '../kit/dotmatrix.js';
import { describe } from '../kit/help.js';
import { makePad, makeKnob, setLed, setKnobValue } from '../kit/controls.js';
import { addGuide } from '../kit/guide.js';
import { CHANNELS, MIX, FX_TYPES, fxValue, channelMessage, busMessage, fxMessage } from '../mixer/mix.js';

const COLS = 84;
const ROWS = 16;
const W = 4;                 // display columns per strip: 18 strips (the ▶ column parts them)
const METER_TOP = 2;
const METER_ROWS = 8;
const FLOOR_DB = -48;        // the meters' bottom
const TOP_DB = 6;            // and top: a fader's +6 dB, so 0 dB sits below it
const BARS = ' ▁▂▃▄▅▆▇█';
const HBARS = ' ▏▎▍▌■';

const STRIPS = [
  ...CHANNELS.map((c, i) => ({ kind: 'channel', index: i, ...c })),
  { kind: 'bus', bus: 0, short: 'FXA', name: 'Send A' },
  { kind: 'bus', bus: 1, short: 'FXB', name: 'Send B' },
  { kind: 'master', short: 'OUT', name: 'Master' },
];
const P = Object.fromEntries(mixRegistry.params.map((p) => [p.id.replace('OM_MIX_', ''), p]));
const FIELD = { LEVEL: 'level', PAN: 'pan', SEND_A: 'sendA', SEND_B: 'sendB', MUTE: 'mute', SOLO: 'solo',
  FILTER: 'filter', CUTOFF: 'cutoff', RESONANCE: 'reso', DUCK: 'duck', DUCK_KEY: 'duckKey', DUCK_RELEASE: 'duckRel' };
const CH_PAGES = ['the mix', 'the filter', 'the ducking'];
const typeParams = FX_TYPES.map((name) =>
  fxRegistry.params.map((p, index) => ({ ...p, index })).filter((p) => p.group === name));

const GUIDE = [
  { title: 'The strips', text: 'One strip for each LXR voice (its FX bus is LFX), each VA slot, the two send effects (FXA, FXB) and the master (OUT). The meters move as they play; the ▶ beside a meter is its fader.' },
  { title: 'Mix', text: 'Click a strip (or &lt; CH, CH &gt;, the arrow keys) and turn the knobs: level, pan, send A, send B. MUTE and SOLO (M, S) silence channels or play only some. Double-click a knob to reset it.' },
  { title: 'Effects', text: 'Send A is a hall reverb, send B a delay on the song\'s tempo. Turn a channel\'s SDA or SDB up to send it there. FX A / FX B open the effect: TYPE - and TYPE + change it (hall, room, delay, chorus, phaser, flanger), the knobs its settings, PAGE the next four.' },
  { title: 'Filters', text: 'Every channel has one of Surge\'s filters. Press PAGE on a channel: the knobs become FLT (the type: low-passes, ladders, high-passes, band-pass, notch), CUT and RES. Sweep CUT for build-ups; FLT off leaves the sound untouched.' },
  { title: 'Ducking', text: 'PAGE twice on a channel puts its ducking on the knobs: DCK, how far it dips when its KEY hits (the kick, drum 1, at first), and REL, how fast it comes back. Pads and basses that pump with the kick are trance\'s and synthwave\'s sound. A muted key still keys.' },
  { title: 'Loud enough', text: 'The master has a limiter: the mix never goes past -0.2 dB. GR on the OUT strip shows how much it holds back; a few dB on the loudest hits is fine, more means turn something down.' },
  { title: 'Saved with the song', text: 'The mix is part of the song: it saves as you change it, and loads with the song.' },
];

// a strip's values in three characters, so a space parts the strips: whole
// dB ("-6", "off"), pan ("C", "L50"), the effect ("hal"); row 0 has them in full
const FX3 = ['off', 'hal', 'rom', 'dly', 'chr', 'phs', 'flg'];
const FLT3 = ['-', 'l12', 'l24', 'lad', 'vin', 'obx', 'k35', 'dio', 'h12', 'h24', 'k3h', 'bp', 'nch', 'obp'];
const HIGH_PASS = [8, 9, 10];   // registry/mix.json OM_MIX_FILTER: the high-passes
const db3 = (v) => (v === 0 ? 'off' : (() => { const d = Math.round(60 * Math.log10(v / 100)); return d > 0 ? `+${d}` : String(d); })());
const pan3 = (v) => { const x = Math.round(((v - 64) / 63) * 100); return x === 0 ? 'C' : `${x < 0 ? 'L' : 'R'}${Math.min(99, Math.abs(x))}`; };

const center = (s, width) => {
  const left = Math.max(0, Math.floor((width - s.length) / 2));
  return (' '.repeat(left) + s).padEnd(width).slice(0, width);
};

// a level (linear) as a meter height in dot rows, 0 .. METER_ROWS * 8
function meterDots(x) {
  if (!(x > 0)) return 0;
  const db = 20 * Math.log10(x);
  return Math.max(0, Math.min(METER_ROWS * 8, Math.round(((db - FLOOR_DB) / (TOP_DB - FLOOR_DB)) * METER_ROWS * 8)));
}

export function createMixerDevice({ send, help, song }) {
  const state = {
    sel: 0,          // the strip the knobs work on
    fx: null,        // the send effect on the display (0, 1), or null: the strips
    page: 0,         // which four of the effect's settings are on the knobs
    chPage: 0,       // a channel's knobs: 0 the mix, 1 its filter, 2 its ducking
    level: new Float32Array(CHANNELS.length + 5),   // the meters, falling back slowly
    hold: new Float32Array(CHANNELS.length + 5),
    holdAge: new Uint16Array(CHANNELS.length + 5),
  };
  const mix = () => song().mix;
  const strip = () => STRIPS[state.sel];

  const el = document.createElement('section');
  el.className = 'device mixer';
  el.setAttribute('aria-label', 'Mixer');
  el.tabIndex = -1;
  el.innerHTML = `
    <canvas class="lcd mixer-lcd"></canvas>
    <div class="tracker-controls mixer-controls">
      <div class="pots mixer-knobs"></div>
      <div class="group mixer-pads"></div>
      <header class="device-head"></header>
    </div>`;
  const canvas = el.querySelector('.mixer-lcd');
  const display = new DotMatrix(canvas, { cols: COLS, rows: ROWS, dot: 2, margin: 6 });
  let summary = () => {};

  // ---- what the knobs change ----
  // [registry param, get, set] for each knob; null: nothing
  function knobTargets() {
    if (state.fx !== null) {
      const b = mix().buses[state.fx];
      return typeParams[b.type].slice(state.page * 4, state.page * 4 + 4).map((p) => [
        p, () => fxValue(b, p.index), (v) => { b.fx[p.id] = v; send(fxMessage(state.fx, p.index, v)); },
      ]).concat([null, null, null, null]).slice(0, 4);
    }
    const s = strip();
    if (s.kind === 'channel') {
      const ch = mix().channels[s.key];
      const knob = (k) => [P[k], () => ch[FIELD[k]], (v) => { ch[FIELD[k]] = v; send(channelMessage(s.index, FIELD[k], v)); }];
      if (state.chPage === 1) {
        const filter = knob('FILTER');
        // a high-pass wide open at the top would be silent: it starts at the bottom (and back)
        filter[2] = (v) => {
          const wasHigh = HIGH_PASS.includes(ch.filter), isHigh = HIGH_PASS.includes(v);
          ch.filter = v;
          send(channelMessage(s.index, 'filter', v));
          if (isHigh !== wasHigh && (ch.cutoff === 127 || ch.cutoff === 0)) {
            ch.cutoff = isHigh ? 0 : 127;
            send(channelMessage(s.index, 'cutoff', ch.cutoff));
          }
        };
        return [filter, knob('CUTOFF'), knob('RESONANCE'), null];
      }
      if (state.chPage === 2) return [knob('DUCK'), knob('DUCK_KEY'), knob('DUCK_RELEASE'), null];
      return ['LEVEL', 'PAN', 'SEND_A', 'SEND_B'].map(knob);
    }
    if (s.kind === 'bus') {
      const b = mix().buses[s.bus];
      return [
        [P.RETURN, () => b.ret, (v) => { b.ret = v; send(busMessage(s.bus, MIX.RETURN, v)); }],
        [P.FX_TYPE, () => b.type, (v) => setType(s.bus, v)],
        null, null,
      ];
    }
    return [[{ ...P.LEVEL, help: 'The whole mix\'s level, before the limiter.' }, () => mix().master,
      (v) => { mix().master = v; send({ type: 'mix', target: 0xff, param: MIX.LEVEL, value: v }); }], null, null, null];
  }

  function setType(bus, type) {
    const b = mix().buses[bus];
    b.type = Math.max(0, Math.min(FX_TYPES.length - 1, type));
    send(busMessage(bus, MIX.FX_TYPE, b.type));
    state.page = 0;
  }

  // ---- the display ----
  function renderStrips() {
    const s = strip();
    const m = mix();
    let words;
    if (s.kind === 'channel') {
      const ch = m.channels[s.key];
      words = `${s.name.toUpperCase()}  LEVEL ${formatValue(P.LEVEL, ch.level)}  PAN ${formatValue(P.PAN, ch.pan)}` +
        `  A ${formatValue(P.SEND_A, ch.sendA)}  B ${formatValue(P.SEND_B, ch.sendB)}` +
        (ch.filter ? `  ${formatShort(P.FILTER, ch.filter).toUpperCase()} ${formatShort(P.CUTOFF, ch.cutoff)}` : '') +
        (ch.duck ? `  DUCK ${formatShort(P.DUCK, ch.duck)} ${formatShort(P.DUCK_KEY, ch.duckKey)}` : '') +
        `${ch.mute ? '  MUTED' : ''}${ch.solo ? '  SOLO' : ''}`;
    } else if (s.kind === 'bus') {
      const b = m.buses[s.bus];
      words = `${s.name.toUpperCase()}: ${FX_TYPES[b.type].toUpperCase()}  RETURN ${formatValue(P.RETURN, b.ret)}   (FX ${'AB'[s.bus]}: its settings)`;
    } else {
      words = `MASTER  LEVEL ${formatValue(P.LEVEL, m.master)}  LIMITER ${gr() > 0.05 ? `-${gr().toFixed(1)} dB` : 'idle'}`;
    }
    display.line(0, words);
    display.line(1, STRIPS.map((x) => center(x.short, W)).join(''));
    const anySolo = CHANNELS.some((c) => m.channels[c.key].solo);
    const rows = Array.from({ length: ROWS - 2 }, () => []);
    STRIPS.forEach((x, i) => {
      // the meter: columns 1-3 of the strip (the master: left, gap, right)
      const meter = x.kind === 'channel' ? [x.index, x.index, x.index]
        : x.kind === 'bus' ? [CHANNELS.length + x.bus, CHANNELS.length + x.bus, CHANNELS.length + x.bus]
          : [CHANNELS.length + 2, -1, CHANNELS.length + 3];
      const fader = x.kind === 'channel' ? m.channels[x.key].level : x.kind === 'bus' ? m.buses[x.bus].ret : m.master;
      const faderRow = METER_ROWS - 1 - Math.min(METER_ROWS - 1, Math.floor(meterDots((fader / 100) ** 3) / 8));
      for (let r = 0; r < METER_ROWS; r++) {
        let cells = r === faderRow && fader > 0 ? '▶' : ' ';
        for (const k of meter) {
          const dots = k < 0 ? 0 : meterDots(state.level[k]);
          const below = (METER_ROWS - 1 - r) * 8;
          cells += BARS[Math.max(0, Math.min(8, dots - below))];
        }
        rows[r].push(cells.padEnd(W));
      }
      let lines;
      if (x.kind === 'channel') {
        const ch = m.channels[x.key];
        const silent = ch.mute || (anySolo && !ch.solo);
        lines = [db3(ch.level), pan3(ch.pan), ch.sendA ? db3(ch.sendA) : '-', ch.sendB ? db3(ch.sendB) : '-',
          FLT3[ch.filter], `${ch.mute ? 'M' : silent ? '·' : ' '}${ch.solo ? 'S' : ' '}`];
      } else if (x.kind === 'bus') {
        const b = m.buses[x.bus];
        lines = [db3(b.ret), FX3[b.type], '', '', '', ''];
      } else {
        lines = [db3(m.master), 'GR', gr() > 0.5 ? `-${Math.round(gr())}` : '0', '', '', ''];
      }
      lines.forEach((t, j) => rows[METER_ROWS + j].push(center(t, W)));
    });
    rows.forEach((cells, r) => display.line(METER_TOP + r, cells.join('')));
    display.uninvert();
    display.invert(1, state.sel * W, W);
    STRIPS.forEach((x, i) => {   // mute and solo lit
      if (x.kind !== 'channel') return;
      const ch = m.channels[x.key];
      const col = i * W + Math.floor((W - 2) / 2);
      if (ch.mute) display.invert(ROWS - 1, col, 1);
      if (ch.solo) display.invert(ROWS - 1, col + 1, 1);
    });
  }

  function renderFx() {
    const b = mix().buses[state.fx];
    const params = typeParams[b.type];
    const pages = Math.max(1, Math.ceil(params.length / 4));
    display.uninvert();
    display.line(0, `SEND ${'AB'[state.fx]}  ${FX_TYPES[b.type].toUpperCase()}  RETURN ${formatValue(P.RETURN, b.ret)}` +
      (params.length ? `   KNOBS: PAGE ${state.page + 1}/${pages}` : ''));
    display.line(1, params.length ? `     ${'SETTING'.padEnd(18)}${'VALUE'.padEnd(14)}` : '');
    for (let r = 2; r < ROWS; r++) {
      const p = params[r - 2];
      if (!p) {
        display.line(r, r === 3 && !params.length ? '  NO EFFECT: TYPE + CHOOSES ONE (HALL, ROOM, DELAY, CHORUS, PHASER, FLANGER)' : '');
        continue;
      }
      const v = fxValue(b, p.index);
      const fill = Math.round(((v - p.min) / (p.max - p.min)) * 30 * 5);
      const bar = Array.from({ length: 30 }, (_, i) => HBARS[Math.max(0, Math.min(5, fill - i * 5))]).join('');
      display.line(r, ` ${p.short.padEnd(3)} ${p.name.padEnd(18)}${formatValue(p, v).padEnd(14)}${bar}`);
      if (Math.floor((r - 2) / 4) === state.page) display.invert(r, 1, 3);
    }
  }

  // the limiter's gain reduction, in dB (the meters' last value)
  const gr = () => -20 * Math.log10(Math.max(1e-6, state.limiter ?? 1));

  function render() {
    if (state.fx !== null) renderFx();
    else renderStrips();
    display.draw();
    const targets = knobTargets();
    captions.forEach((c, k) => { c.textContent = targets[k] ? targets[k][0].short : '-'; });
    setLed(pads.mute, strip().kind === 'channel' && mix().channels[strip().key].mute);
    setLed(pads.solo, strip().kind === 'channel' && mix().channels[strip().key].solo);
    setLed(pads.fxA, state.fx === 0);
    setLed(pads.fxB, state.fx === 1);
    summary(card());
    help.refresh();
  }

  // the rack card: the two effects, and the master's meter
  function card() {
    const m = mix();
    const dots = meterDots(Math.max(state.level[CHANNELS.length + 2], state.level[CHANNELS.length + 3]));
    const lit = Math.round((dots / (METER_ROWS * 8)) * 8);
    const type = (b) => formatShort(P.FX_TYPE, m.buses[b].type).toUpperCase();
    return [`A ${type(0)} B ${type(1)}`, `OUT ${'■'.repeat(lit)}`];
  }

  // ---- knobs ----
  const knobs = el.querySelector('.mixer-knobs');
  const captions = [];
  for (let k = 0; k < 4; k++) {
    const target = () => knobTargets()[k];
    const setTo = (v) => {
      const t = target();
      if (!t) return;
      v = Math.min(t[0].max, Math.max(t[0].min, v));
      if (v === t[1]()) return;
      t[2](v);
      render();
    };
    const { wrap, pot } = makeKnob(`Knob ${k + 1}`, {
      onStep: (steps) => { const t = target(); if (t) setTo(t[1]() + steps); },
      onReset: () => { const t = target(); if (t) setTo(t[0].default); },
    });
    describe(pot, help, () => {
      const t = target();
      return t ? helpLine(t[0], t[1]()) : 'This knob does nothing here.';
    });
    pot.addEventListener('focus', () => {
      const t = target();
      setKnobValue(pot, t?.[0], t ? t[1]() : 0, t ? `${t[0].name} ${formatValue(t[0], t[1]())}` : 'unused');
    });
    const caption = document.createElement('span');
    caption.className = 'caption';
    wrap.append(caption);
    captions.push(caption);
    knobs.append(wrap);
  }

  // ---- pads ----
  const padRow = el.querySelector('.mixer-pads');
  const select = (i) => {
    state.sel = (i + STRIPS.length) % STRIPS.length;
    render();
  };
  const toggle = (field) => {
    const s = strip();
    if (s.kind !== 'channel') return;
    const ch = mix().channels[s.key];
    ch[field] = ch[field] ? 0 : 1;
    send(channelMessage(s.index, field, ch[field]));
    render();
  };
  const openFx = (bus) => {
    state.fx = state.fx === bus ? null : bus;
    state.page = 0;
    render();
  };
  const PADS = [
    ['prev', '&lt; CH', 'The strip to the left (Left arrow).', () => select(state.sel - 1)],
    ['next', 'CH &gt;', 'The strip to the right (Right arrow).', () => select(state.sel + 1)],
    ['mute', 'MUTE', 'Silences the selected channel, its sends too (M).', () => toggle('mute')],
    ['solo', 'SOLO', 'Plays only the soloed channels (S). Press again to undo.', () => toggle('solo')],
    ['fxA', 'FX A', 'Send A\'s effect on the display: its type and settings. Press again for the strips.', () => openFx(0)],
    ['fxB', 'FX B', 'Send B\'s effect on the display: its type and settings. Press again for the strips.', () => openFx(1)],
    ['typeDown', 'TYPE -', 'The previous effect on this send (with FX A or FX B lit).', () => changeType(-1)],
    ['typeUp', 'TYPE +', 'The next effect on this send: hall, room, delay, chorus, phaser, flanger.', () => changeType(1)],
    ['page', 'PAGE', () => (state.fx !== null ? 'The next four settings of the effect on the knobs.'
      : `A channel's knobs: its mix (level, pan, sends), its filter (type, cutoff, resonance) or its ducking (amount, key, release). Now: ${CH_PAGES[state.chPage]}.`),
    () => nextPage()],
  ];
  const pads = {};
  for (const [key, label, text, action] of PADS) {
    const pad = makePad(label, { kind: 'small', onChange: (down) => down && action() });
    describe(pad, help, text);
    padRow.append(pad);
    pads[key] = pad;
  }
  function busInView() {
    if (state.fx !== null) return state.fx;
    return strip().kind === 'bus' ? strip().bus : null;
  }
  function changeType(d) {
    const bus = busInView();
    if (bus === null) return;
    setType(bus, mix().buses[bus].type + d);
    render();
  }
  function nextPage() {
    if (state.fx === null) {
      state.chPage = (state.chPage + 1) % CH_PAGES.length;
      setLed(pads.page, state.chPage > 0);
      render();
      return;
    }
    const count = typeParams[mix().buses[state.fx].type].length;
    state.page = (state.page + 1) % Math.max(1, Math.ceil(count / 4));
    render();
  }

  // ---- the display: click a strip, or an effect's setting ----
  const cellAt = (e) => {
    const rect = canvas.getBoundingClientRect();
    return {
      col: Math.floor(((e.clientX - rect.left) / rect.width) * canvas.width / display.charW),
      row: Math.floor(((e.clientY - rect.top) / rect.height) * canvas.height / display.charH),
    };
  };
  canvas.addEventListener('pointerdown', (e) => {
    const { col, row } = cellAt(e);
    if (state.fx !== null) {
      if (row >= 2) {
        state.page = Math.min(Math.floor((row - 2) / 4), Math.ceil(typeParams[mix().buses[state.fx].type].length / 4) - 1);
        render();
      }
      return;
    }
    select(Math.min(STRIPS.length - 1, Math.max(0, Math.floor(col / W))));
  });
  let hover = null;
  canvas.addEventListener('pointermove', (e) => { hover = cellAt(e); help.refresh(); });
  canvas.addEventListener('pointerleave', () => { hover = null; });
  describe(canvas, help, () => {
    if (state.fx !== null) return 'The effect\'s settings. The highlighted four are on the knobs; click a setting to put its four there.';
    const x = hover ? STRIPS[Math.min(STRIPS.length - 1, Math.floor(hover.col / W))] : strip();
    if (x.kind === 'channel') return `${x.name}: click to put its level, pan and sends on the knobs.`;
    if (x.kind === 'bus') return `${x.name}'s return: ${FX_TYPES[mix().buses[x.bus].type]}. Click it, then turn the knobs: return, effect.`;
    return 'The master: the whole mix, and the limiter that keeps it under -0.2 dB (GR: how much it holds back).';
  });

  // ---- keys (while the mixer is shown) ----
  function handleKey(e, down) {
    if (!down || e.metaKey || e.ctrlKey || e.altKey) return false;
    const k = e.key.toLowerCase();
    if (e.key === 'ArrowLeft') select(state.sel - 1);
    else if (e.key === 'ArrowRight') select(state.sel + 1);
    else if (k === 'm') toggle('mute');
    else if (k === 's') toggle('solo');
    else if (e.key === 'Escape' && state.fx !== null) openFx(state.fx);
    else return false;
    return true;
  }

  // ---- meters, from the worklet ----
  function onMeters(meters) {
    for (let i = 0; i < state.level.length && i < meters.length; i++) {
      state.level[i] = Math.max(meters[i], state.level[i] * 0.86);   // falls 20 dB in ~0.3 s
    }
    state.limiter = meters[CHANNELS.length + 4];
    if (!el.hidden) render();
    else summary(card());
  }

  addGuide(el, el.querySelector('.device-head'), GUIDE, help);
  return {
    el, handleKey, onMeters, render,
    onSummary: (fn) => { summary = fn; render(); },
  };
}

