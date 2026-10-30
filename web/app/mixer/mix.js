// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The mix, as the song keeps it (song.mix): every channel's level, pan, sends,
// mute and solo, the two send buses' effects and their settings, and the
// master level. The studio's mixer (studio/studio.c) gets a copy of every
// change. Bytes as in registry/mix.json and registry/fx.json.

import fxRegistry from '../registry/fx.js';

// The mixer's channels: the engines' stems, in the studio's order (the LXR's
// seven, VA's four, Dexed's two, Plaits' two; web/port/web_studio.c). Keys stay the same when
// engines are added, so saved songs keep their mix.
export const CHANNELS = [
  { key: 'lxr1', short: 'DR1', name: 'Drum 1', engine: 0 },
  { key: 'lxr2', short: 'DR2', name: 'Drum 2', engine: 0 },
  { key: 'lxr3', short: 'DR3', name: 'Drum 3', engine: 0 },
  { key: 'lxr4', short: 'SNR', name: 'Snare', engine: 0 },
  { key: 'lxr5', short: 'CYM', name: 'Cymbal', engine: 0 },
  { key: 'lxr6', short: 'HAT', name: 'Hihats', engine: 0 },
  { key: 'lxrfx', short: 'LFX', name: 'LXR FX bus', engine: 0 },
  { key: 'va1', short: 'VA1', name: 'VA1 Bass', engine: 1 },
  { key: 'va2', short: 'VA2', name: 'VA2 Poly', engine: 1 },
  { key: 'va3', short: 'VA3', name: 'VA3 Lead', engine: 1 },
  { key: 'va4', short: 'VA4', name: 'VA4 Noise', engine: 1 },
  { key: 'dx1', short: 'DX1', name: 'Dexed DX1', engine: 2 },
  { key: 'dx2', short: 'DX2', name: 'Dexed DX2', engine: 2 },
  { key: 'pl1', short: 'PL1', name: 'Plaits PL1', engine: 3 },
  { key: 'pl2', short: 'PL2', name: 'Plaits PL2', engine: 3 },
];

// studio.h: the controls, and the targets besides the channels
export const MIX = { LEVEL: 0, PAN: 1, SEND_A: 2, SEND_B: 3, MUTE: 4, SOLO: 5, FX_TYPE: 6, RETURN: 7, FILTER: 8, CUTOFF: 9, RESONANCE: 10,
  DUCK: 11, DUCK_KEY: 12, DUCK_RELEASE: 13, GATE: 14, GATE_SMOOTH: 15, GATE_STEPS1: 16, GATE_STEPS2: 17, GATE_STEPS3: 18 };
export const BUS_A = 0xf0;
export const MASTER = 0xff;
export const FX_TYPES = fxRegistry.parts;   // 'Off', 'Hall', ...
// a channel's fields and their controls (studio.h)
const CHANNEL_FIELDS = { level: 0, pan: 1, sendA: 2, sendB: 3, mute: 4, solo: 5, filter: 8, cutoff: 9, reso: 10, duck: 11, duckKey: 12, duckRel: 13,
  gate: 14, gateSmooth: 15, gateS1: 16, gateS2: 17, gateS3: 18 };

export function newMix() {
  return {
    // VA's slots start at -6 dB: they are loud next to the LXR
    channels: Object.fromEntries(CHANNELS.map((c) => [c.key, { level: c.engine === 1 ? 79 : 100, pan: 64, sendA: 0, sendB: 0, mute: 0, solo: 0, filter: 0, cutoff: 127, reso: 0, duck: 0, duckKey: 0, duckRel: 62, gate: 0, gateSmooth: 47, gateS1: 21, gateS2: 21, gateS3: 5 }])),
    buses: [{ type: 1, ret: 100, fx: {} }, { type: 3, ret: 100, fx: {} }],   // a hall and a delay
    master: 100,
  };
}

// A mix from any version, made whole.
export function migrateMix(raw) {
  const base = newMix();
  if (!raw) return base;
  for (const c of CHANNELS) base.channels[c.key] = { ...base.channels[c.key], ...(raw.channels?.[c.key] ?? {}) };
  base.buses = base.buses.map((b, i) => ({ ...b, ...(raw.buses?.[i] ?? {}), fx: { ...(raw.buses?.[i]?.fx ?? {}) } }));
  if (Number.isInteger(raw.master)) base.master = raw.master;
  return base;
}

// A send effect's parameter: its byte in the bus (registry/fx.json index).
export const fxValue = (bus, index) => bus.fx[fxRegistry.params[index].id] ?? fxRegistry.params[index].default;

export const channelMessage = (index, field, value) =>
  ({ type: 'mix', target: index, param: CHANNEL_FIELDS[field], value });
export const busMessage = (bus, param, value) => ({ type: 'mix', target: BUS_A + bus, param, value });
export const fxMessage = (bus, index, value) => ({ type: 'fx', bus, param: index, value });

// Every message that makes the studio's mixer a copy of the mix.
export function mixMessages(mix) {
  const msgs = [];
  CHANNELS.forEach((c, i) => {
    for (const field of Object.keys(CHANNEL_FIELDS)) msgs.push(channelMessage(i, field, mix.channels[c.key][field]));
  });
  mix.buses.forEach((b, i) => {
    // the settings first: the type change applies them all to the effect at once
    fxRegistry.params.forEach((p, index) => msgs.push(fxMessage(i, index, fxValue(b, index))));
    msgs.push(busMessage(i, MIX.FX_TYPE, b.type), busMessage(i, MIX.RETURN, b.ret));
  });
  msgs.push({ type: 'mix', target: MASTER, param: MIX.LEVEL, value: mix.master });
  return msgs;
}

// The trance gate's 16 steps: the channel's three step bytes (6, 6 and 4 bits).
export const gateSteps = (ch) => Array.from({ length: 16 }, (_, i) =>
  Boolean((i < 6 ? ch.gateS1 >> i : i < 12 ? ch.gateS2 >> (i - 6) : ch.gateS3 >> (i - 12)) & 1));
export function setGateSteps(ch, steps) {
  const bits = (from, n) => steps.slice(from, from + n).reduce((b, on, i) => b | (on ? 1 << i : 0), 0);
  ch.gateS1 = bits(0, 6); ch.gateS2 = bits(6, 6); ch.gateS3 = bits(12, 4);
}
// patterns to start from (x on, . off)
export const GATE_PATTERNS = [
  ['8ths', 'x.x.x.x.x.x.x.x.'], ['offbeats', '..x...x...x...x.'], ['trance', 'x.xx.xx.x.xx.xx.'],
  ['skip', 'xx.xx.x.xx.xx.x.'], ['stutter', 'x.x.xxx.x.x.xxx.'], ['dotted', 'x..x..x..x..x..x'],
  ['chop', 'xx..xx..xx..xx..'], ['gallop', 'x.xxx.xxx.xxx.xx'], ['half bar', 'xxxxxxxx........'],
].map(([name, p]) => ({ name, steps: [...p].map((c) => c === 'x') }));
