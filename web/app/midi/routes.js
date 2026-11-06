// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// Which MIDI channel plays what, and the MIDI settings that outlive the page.
// The defaults are the tracks the songs were written for: channels 1-7 the
// LXR's voices, 8-11 VA's slots, 12-13 Dexed, 14-15 Plaits, 16 Rings. The
// worklet's router (studio/midi_in.c) gets them as messages.

export const CHANNELS = 16;
export const MIN_TEMPO = 20;
export const MAX_TEMPO = 300;
export const MAX_LOOKAHEAD = 50;
export const DEFAULT_LOOKAHEAD = 10;   // ms

const OFF = 0xff;
const RAW = 0xfe;
const MIXER = 0xfd;

const part = (key, name, engine, p, words) => ({ key, name, engine, part: p, words });

export const DESTINATIONS = [
  part('off', 'OFF', OFF, 0, 'nothing: this channel\'s notes are ignored'),
  ...['DRUM 1', 'DRUM 2', 'DRUM 3', 'SNARE', 'CYMBAL', 'HIHAT CL', 'HIHAT OP'].map((name, p) =>
    part(`lxr${p}`, name, 0, p, `the LXR's ${name.toLowerCase()}; note 60 (C4) is the kit's pitch`)),
  ...['VA1 BASS', 'VA2 POLY', 'VA3 LEAD', 'VA4 NOISE'].map((name, p) => part(`va${p}`, name, 1, p, `Orange Mouse VA's slot ${p + 1}`)),
  part('dx0', 'DX1', 2, 0, 'Dexed\'s first part'),
  part('dx1', 'DX2', 2, 1, 'Dexed\'s second part'),
  part('pl0', 'PL1', 3, 0, 'Plaits\' first part'),
  part('pl1', 'PL2', 3, 1, 'Plaits\' second part'),
  part('rn0', 'RN1', 4, 0, 'Rings'),
  part('mixer', 'MIXER', MIXER, 0, 'the mixer: CCs set the drums\' channels, the send returns and the master (docs/MIDI_CC.md); notes are ignored'),
  part('lxrmenu', 'LXR MENU', RAW, 0, 'the LXR, by its own MIDI settings (its config menu: channels, a note for each voice); it can record these'),
];

const INDEX = new Map(DESTINATIONS.map((d, i) => [d.key, i]));

export const defaultRoutes = () => DESTINATIONS.slice(1, CHANNELS + 1).map((d) => d.key);

export const destination = (key) => DESTINATIONS[INDEX.get(key) ?? 0];

// The next destination, `steps` along (it stops at the ends).
export function stepDestination(key, steps) {
  const i = (INDEX.get(key) ?? 0) + steps;
  return DESTINATIONS[Math.max(0, Math.min(DESTINATIONS.length - 1, i))].key;
}

export const routeMessage = (channel, key) => {
  const d = destination(key);
  return { type: 'midiRoute', channel, engine: d.engine, part: d.part };
};
export const routeMessages = (routes) => routes.map((key, channel) => routeMessage(channel, key));

const clamp = (v, lo, hi, otherwise) => (Number.isFinite(v) ? Math.max(lo, Math.min(hi, v)) : otherwise);

// The settings, from what localStorage held (anything, or nothing).
export function parseSettings(text) {
  let raw = null;
  try { raw = JSON.parse(text); } catch { /* new, or spoiled: the defaults */ }
  const routes = defaultRoutes();
  if (Array.isArray(raw?.routes)) raw.routes.slice(0, CHANNELS).forEach((key, c) => { if (INDEX.has(key)) routes[c] = key; });
  return {
    routes,
    tempo: clamp(raw?.tempo, MIN_TEMPO, MAX_TEMPO, 120),
    lookahead: clamp(raw?.lookahead, 0, MAX_LOOKAHEAD, DEFAULT_LOOKAHEAD),
    input: typeof raw?.input === 'string' ? raw.input : '',
    output: typeof raw?.output === 'string' ? raw.output : '',
  };
}
