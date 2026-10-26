// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The studio page: builds the LXR's front panel and the Orange Mouse VA panel
// (devices/va.js), starts the studio in an AudioWorklet (studio-worklet.js)
// and connects it to the displays, LEDs, the SD card store (IndexedDB) and
// Web MIDI.

import { Lcd } from './lcd.js';
import { SdCardStore, SD_SECTORS, SECTOR_SIZE, flushOnHide } from './sdcard.js';
import { makePad, makeRotary } from './kit/controls.js';
import { KEY_SEMITONES } from './kit/keybed.js';
import { createVaDevice } from './devices/va.js';
import { createTrackerDevice } from './devices/tracker.js';
import { createMixerDevice } from './devices/mixer.js';
import { createDexedDevice } from './devices/dexed.js';
import { createPlaitsDevice } from './devices/plaits.js';
import { songMessages } from './tracker/song.js';
import * as library from './tracker/library.js';
import { demoSong } from './tracker/demo.js';
import { GENRE_SONGS } from './tracker/genres/index.js';
import { DotMatrix } from './kit/dotmatrix.js';
import { addGuide } from './kit/guide.js';
import { HelpStrip, describe } from './kit/help.js';
import lxrRegistry from './registry/lxr.js';
import { helpLine } from './registry/format.js';

// Button numbers as in enum ButtonNumbers (Core/Inc/buttonHandler.h); each
// button's LED has the same number in enum LedNumbers (ledHandler.h).
const BUT = {
  PAGE_UP: 0, VOICE_1: 1,
  SELECT5: 8, SELECT1: 16,
  MODE1: 24,
  SHIFT: 32, START_STOP: 33, REC: 34, COPY: 35,
  PAGE_DOWN: 40,
};
const SEQ_BUTTONS = [39, 38, 37, 36, 31, 30, 29, 28, 23, 22, 21, 20, 15, 14, 13, 12];  // SEQ1..SEQ16
const SELECT_BUTTONS = [16, 17, 18, 19, 8, 9, 10, 11];                                // SELECT1..SELECT8

const VOICES = ['Drum 1', 'Drum 2', 'Drum 3', 'Snare', 'Cymbal', 'Hihat cl', 'Hihat op'];
const MODES = [['Voice', 'Step'], ['Perf', 'Euklid'], ['Load', 'FX'], ['Save', 'Menu']];
const FADERS = ['Drum 1', 'Drum 2', 'Drum 3', 'Snare', 'Cymbal', 'Hihat'];
// Shift + step button functions (enum SeqButtonFunctions, buttonHandler.h);
// steps 1-7 have no shift function.
const STEP_SHIFT = ['', '', '', '', '', '', '', 'Kit', 'Morph', 'Song', 'Project', 'Config', 'Reload', 'Last', 'FX', 'Pattern'];
const POT_DEGREES_PER_PIXEL = 1.5;
const ENCODER_PIXELS_PER_DETENT = 12;

const lcd = new Lcd(document.getElementById('lcd'));
const buttons = new Map();     // button number -> element
let node = null;               // AudioWorkletNode, once switched on
let turnEncoder = null;        // turns the encoder by n detents
let shiftKeyDown = false;

function send(msg) {
  node?.port.postMessage(msg);
}

// the studio's status, on the help strip for a moment
function setStatus(text) {
  help.note(text);
}

// ---- controls -------------------------------------------------------------------

// An LXR panel button: number from enum ButtonNumbers (buttonHandler.h).
function makeButton(id, label, options = {}) {
  const el = makePad(label, { ...options, onChange: (down) => send({ type: 'button', id, down }) });
  buttons.set(id, el);
  return el;
}

function buildPanel() {
  // the four endless pots below the display
  const pots = document.getElementById('pots');
  for (let i = 0; i < 4; i++) {
    const wrap = document.createElement('div');
    wrap.className = 'pot-wrap';
    wrap.innerHTML = `<div class="pot" tabindex="0" role="slider" aria-label="Pot ${i + 1}"><div class="pot-mark"></div></div>`;
    const pot = wrap.firstElementChild;
    let angle = 0;
    const turn = (degrees) => {
      angle += degrees;
      pot.style.setProperty('--angle', `${angle}deg`);
      send({ type: 'pot', pot: i, degrees });
    };
    makeRotary(pot, (px) => turn(px * POT_DEGREES_PER_PIXEL));
    pot.addEventListener('keydown', (e) => {
      if (e.key === 'ArrowUp' || e.key === 'ArrowRight') { e.preventDefault(); e.stopPropagation(); turn(6); }
      if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') { e.preventDefault(); e.stopPropagation(); turn(-6); }
    });
    pots.append(wrap);
  }

  // the data encoder
  const encoder = document.getElementById('encoder');
  let encoderAngle = 0;
  let encoderPixels = 0;
  turnEncoder = (detents) => {
    encoderAngle += detents * 15;
    encoder.style.setProperty('--angle', `${encoderAngle}deg`);
    send({ type: 'encoder', detents });
  };
  makeRotary(encoder, (px) => {
    encoderPixels += px;
    const detents = Math.trunc(encoderPixels / ENCODER_PIXELS_PER_DETENT);
    if (detents) {
      encoderPixels -= detents * ENCODER_PIXELS_PER_DETENT;
      turnEncoder(detents);
    }
  });
  // a click without dragging pushes the encoder
  let pushStart = null;
  encoder.addEventListener('pointerdown', (e) => {
    pushStart = { x: e.clientX, y: e.clientY };
  });
  encoder.addEventListener('pointermove', (e) => {
    if (pushStart && Math.hypot(e.clientX - pushStart.x, e.clientY - pushStart.y) > 4) pushStart = null;
  });
  encoder.addEventListener('pointerup', () => {
    if (pushStart) pushEncoder();
    pushStart = null;
  });
  encoder.addEventListener('pointercancel', () => { pushStart = null; });
  encoder.addEventListener('keydown', (e) => {
    if (e.key === 'ArrowUp' || e.key === 'ArrowRight') { e.preventDefault(); e.stopPropagation(); turnEncoder(1); }
    if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') { e.preventDefault(); e.stopPropagation(); turnEncoder(-1); }
  });

  // volume faders
  const faders = document.getElementById('faders');
  FADERS.forEach((name, i) => {
    const wrap = document.createElement('label');
    wrap.className = 'fader-wrap';
    wrap.innerHTML = `<input type="range" class="fader" min="0" max="1" step="0.005" value="0.8" aria-label="${name} volume"><span class="caption">${name}</span>`;
    const input = wrap.querySelector('input');
    input.addEventListener('input', () => send({ type: 'fader', fader: i, value: Number(input.value) }));
    faders.append(wrap);
  });

  const voices = document.getElementById('voices');
  VOICES.forEach((name, i) => voices.append(makeButton(BUT.VOICE_1 + i, name, { kind: 'voice' })));

  const pages = document.getElementById('pages');
  pages.append(makeButton(BUT.PAGE_UP, 'Page ▲', { kind: 'small' }), makeButton(BUT.PAGE_DOWN, 'Page ▼', { kind: 'small' }));

  const selects = document.getElementById('selects');
  SELECT_BUTTONS.forEach((id, i) => selects.append(makeButton(id, String(i + 1), { kind: 'select' })));

  const modes = document.getElementById('modes');
  MODES.forEach(([label, alt], i) => modes.append(makeButton(BUT.MODE1 + i, label, { alt, kind: 'mode' })));

  const transport = document.getElementById('transport');
  transport.append(
    makeButton(BUT.SHIFT, 'Shift', { kind: 'shift' }),
    makeButton(BUT.COPY, 'Copy', { alt: 'Clear' }),
    makeButton(BUT.REC, 'Rec'),
    makeButton(BUT.START_STOP, 'Start', { kind: 'start' }),
  );

  const steps = document.getElementById('steps');
  SEQ_BUTTONS.forEach((id, i) => steps.append(makeButton(id, String(i + 1), { alt: STEP_SHIFT[i], kind: 'step' })));
}

// A short push of the encoder button, long enough for the firmware's
// debouncing (4 samples, 10 ms apart).
function pushEncoder() {
  send({ type: 'encoderButton', down: true });
  setTimeout(() => send({ type: 'encoderButton', down: false }), 80);
}

function showLeds([low, high]) {
  for (const [id, el] of buttons) {
    const on = id < 32 ? (low >>> id) & 1 : (high >>> (id - 32)) & 1;
    el.classList.toggle('lit', !!on);
  }
}

// ---- keyboard ---------------------------------------------------------------------

function pressFromKeyboard(id, down) {
  const el = buttons.get(id);
  el?.classList.toggle('down', down);
  send({ type: 'button', id, down });
}

window.addEventListener('keydown', (e) => {
  if (e.target instanceof HTMLInputElement || e.target instanceof HTMLSelectElement) return;
  // Alt+1..9 shows a device, Alt+left/right the previous or next one
  if (e.altKey && /^Digit[1-9]$/.test(e.code)) {
    e.preventDefault();
    showDevice(Number(e.code.slice(5)) - 1);
    return;
  }
  if (e.altKey && (e.key === 'ArrowLeft' || e.key === 'ArrowRight')) {
    e.preventDefault();
    showDevice((shown + (e.key === 'ArrowRight' ? 1 : devices.length - 1)) % devices.length);
    return;
  }
  const device = devices[shown]?.id;
  if (device === 'tracker' && tracker.handleKey(e, true)) {
    e.preventDefault();
    return;
  }
  if (device === 'mixer' && mixer.handleKey(e, true)) {
    e.preventDefault();
    return;
  }
  if ((device === 'dexed' && dexed.handleKey(e, true)) || (device === 'plaits' && plaits.handleKey(e, true))) {
    e.preventDefault();
    return;
  }
  // the keyboard plays the instrument on show
  const player = { va, dexed, plaits }[device];
  if (player && e.code in KEY_SEMITONES && !e.metaKey && !e.ctrlKey && !e.altKey) {
    if (!e.repeat) player.noteFromKey(KEY_SEMITONES[e.code], true);
    return;
  }
  if (player && (e.key === '[' || e.key === ']')) {
    player.octave(e.key === ']' ? 1 : -1);
    return;
  }
  if (e.key === 'Shift' && !shiftKeyDown) {
    shiftKeyDown = true;
    pressFromKeyboard(BUT.SHIFT, true);
  } else if (e.key === ' ' && !e.repeat) {
    e.preventDefault();
    pressFromKeyboard(BUT.START_STOP, true);
  } else if (device === 'lxr' && (e.key === 'ArrowLeft' || e.key === 'ArrowRight')) {
    e.preventDefault();
    turnEncoder?.(e.key === 'ArrowRight' ? 1 : -1);
  } else if (device === 'lxr' && e.key === 'Enter' && !e.repeat) {
    e.preventDefault();
    pushEncoder();
  }
});

window.addEventListener('keyup', (e) => {
  if (devices[shown]?.id === 'tracker' && tracker.handleKey(e, false)) return;
  if (e.code in KEY_SEMITONES) {   // released on both, in case the device changed while a key was down
    va.noteFromKey(KEY_SEMITONES[e.code], false);
    dexed.noteFromKey(KEY_SEMITONES[e.code], false);
    plaits.noteFromKey(KEY_SEMITONES[e.code], false);
    return;
  }
  if (e.key === 'Shift') {
    shiftKeyDown = false;
    pressFromKeyboard(BUT.SHIFT, false);
  } else if (e.key === ' ') {
    pressFromKeyboard(BUT.START_STOP, false);
  }
});

// ---- MIDI ------------------------------------------------------------------------

const midi = { access: null, input: null, output: null };

async function setUpMidi() {
  if (!navigator.requestMIDIAccess) {
    document.getElementById('midiIn').disabled = true;
    document.getElementById('midiOut').disabled = true;
    return;
  }
  try {
    midi.access = await navigator.requestMIDIAccess();
  } catch {
    return;
  }
  const inSel = document.getElementById('midiIn');
  const outSel = document.getElementById('midiOut');
  const fill = () => {
    for (const [sel, ports] of [[inSel, midi.access.inputs], [outSel, midi.access.outputs]]) {
      const current = sel.value;
      sel.length = 1;
      for (const port of ports.values()) sel.add(new Option(port.name, port.id));
      sel.value = [...ports.keys()].includes(current) ? current : '';
    }
  };
  fill();
  midi.access.onstatechange = fill;
  inSel.addEventListener('change', () => {
    if (midi.input) midi.input.onmidimessage = null;
    midi.input = midi.access.inputs.get(inSel.value) || null;
    if (midi.input) midi.input.onmidimessage = (e) => send({ type: 'midi', bytes: Array.from(e.data) });
  });
  outSel.addEventListener('change', () => {
    midi.output = midi.access.outputs.get(outSel.value) || null;
  });
}

// The firmware sends a running byte stream (DIN MIDI); Web MIDI wants whole
// messages, so split it up again.
const midiOutParser = { status: 0, data: [], sysex: null };

function sendMidiBytes(bytes) {
  if (!midi.output) return;
  const p = midiOutParser;
  for (const b of bytes) {
    if (b >= 0xf8) { midi.output.send([b]); continue; }                 // real-time
    if (b === 0xf0) { p.sysex = [b]; continue; }
    if (p.sysex) {
      p.sysex.push(b);
      if (b === 0xf7) { midi.output.send(p.sysex); p.sysex = null; }
      continue;
    }
    if (b & 0x80) { p.status = b; p.data = []; continue; }
    if (!p.status) continue;
    p.data.push(b);
    const type = p.status & 0xf0;
    const length = type === 0xc0 || type === 0xd0 || p.status === 0xf1 || p.status === 0xf3 ? 1 : 2;
    if (p.data.length === length) {
      midi.output.send([p.status, ...p.data]);
      p.data = [];
      if (p.status >= 0xf0) p.status = 0;
    }
  }
}

// ---- start up ----------------------------------------------------------------------

const store = new SdCardStore();
flushOnHide(store);

async function switchOn() {
  const power = document.getElementById('power');
  const message = document.getElementById('powerMessage');
  message.textContent = 'Starting…';

  const context = new AudioContext({ sampleRate: 44100, latencyHint: 'interactive' });
  const [wasm, card] = await Promise.all([
    fetch('studio.wasm').then((r) => {
      if (!r.ok) throw new Error(`studio.wasm: ${r.status} ${r.statusText} (build it first, see README.md)`);
      return r.arrayBuffer();
    }),
    store.load(),
    context.audioWorklet.addModule('studio-worklet.js'),
  ]);

  const gain = context.createGain();
  const volume = document.getElementById('volume');
  gain.gain.value = Number(volume.value);
  volume.addEventListener('input', () => gain.gain.setTargetAtTime(Number(volume.value), context.currentTime, 0.02));

  node = new AudioWorkletNode(context, 'studio', {
    numberOfInputs: 0,
    outputChannelCount: [2],
    processorOptions: {
      wasm,
      sdImage: card.image.buffer,
      blankCard: card.blank,
      seed: (Math.random() * 0xffffffff) >>> 0,
    },
  });
  node.connect(gain).connect(context.destination);
  node.port.onmessage = (e) => onWorkletMessage(e.data);
  for (const m of songMessages(tracker.song())) send(m);   // the worklet's tracker gets the song
  node.onprocessorerror = () => setStatus('The studio stopped with an error, reload the page.');

  for (const [i, input] of document.querySelectorAll('.fader').entries()) {
    send({ type: 'fader', fader: i, value: Number(input.value) });
  }
  await context.resume();
  power.hidden = true;
  window.orangeMouse = { context, node, va, tracker, mixer, dexed, plaits };   // for the browser tests (tests/browser/)
  setStatus(card.blank ? 'New SD card formatted' : 'SD card loaded');
}

// ---- the LXR's controls, explained on the help strip -------------------------------

let lxrPots = null;   // from the worklet: the parameters on the pots, their values, the cursor

function describeLxr() {
  document.querySelectorAll('#pots .pot').forEach((pot, i) => describe(pot, help, () => {
    if (!lxrPots) return `Pot ${i + 1}: changes the parameter above it on the display.`;
    const n = lxrPots.params[i];
    const info = lxrRegistry.params[n];
    if (!info) return `Pot ${i + 1}: a setting of this page (the display shows its name and value).`;
    return `Pot ${i + 1} (${info.part ? info.part + ' ' : ''}${info.group}): ${helpLine(info, lxrPots.values[i])}`;
  }));
  describe(document.getElementById('encoder'), help, 'Data encoder: turn it to move the cursor on the display, or to change the value. Click (or Enter) switches between the two.');
  document.querySelectorAll('.fader').forEach((f, i) => describe(f, help, `${FADERS[i]} volume fader.`));
  const pads = [
    ...VOICES.map((name, i) => [BUT.VOICE_1 + i, `${name}: selects the voice, to edit its sound or its steps.`]),
    [BUT.PAGE_UP, 'Page up: the previous bar of a long pattern. Copy + Page up copies the bar into the next.'],
    [BUT.PAGE_DOWN, 'Page down: the next bar of a long pattern.'],
    ...SELECT_BUTTONS.map((id, i) => [id, `Select ${i + 1}: a page of the voice (osc, filter, envelopes, LFO...) or, in step edit, a sub-step.`]),
    [BUT.MODE1, 'Voice: edit sounds; press again for step edit. Shift: step mode.'],
    [BUT.MODE1 + 1, 'Perf: performance mode; the select buttons choose patterns. Shift: Euklid, the Euclidean generator.'],
    [BUT.MODE1 + 2, 'Load: kits, patterns, projects from the SD card. Shift: the FX page.'],
    [BUT.MODE1 + 3, 'Save to the SD card. Shift: the config menu (MIDI and more).'],
    [BUT.SHIFT, 'Shift: hold it (or the Shift key) for the grey second functions.'],
    [BUT.COPY, 'Copy: hold it and press the target. Shift: clear.'],
    [BUT.REC, 'Rec: records what you play into the pattern.'],
    [BUT.START_STOP, 'Start/stop the LXR\'s sequencer (Space).'],
    ...SEQ_BUTTONS.map((id, i) => [id, `Step ${i + 1}: switches the step on or off${STEP_SHIFT[i] ? `. Shift: ${STEP_SHIFT[i]}` : ''}.`]),
  ];
  for (const [id, text] of pads) if (buttons.has(id)) describe(buttons.get(id), help, text);
}

function onWorkletMessage(msg) {
  switch (msg.type) {
    case 'ui':
      if (msg.lcd) {
        lcd.update(msg.lcd, msg.cgram, msg.lcdOn);
        lcd.canvas.setAttribute('aria-label', `Display: ${lcd.toString()}`);
        devices[1].mini(lcd.toString().split('\n'));
      }
      if (msg.leds) showLeds(msg.leds);
      if (msg.tracker) {
        tracker.onState(msg.tracker);
        devices[0].mini(tracker.summary());
      }
      if (msg.meters) mixer.onMeters(msg.meters);
      if (msg.lxrKit) tracker.keepLxrKit(msg.lxrKit);
      if (msg.lxrPots) {
        lxrPots = msg.lxrPots;
        help.refresh();
      }
      if (msg.midi) sendMidiBytes(msg.midi);
      if (msg.log) msg.log.forEach((line) => console.info('[LXR]', line));
      break;
    case 'sd':
      store.write(msg.sectors, msg.data);
      break;
    case 'sdAll':
      store.writeAll(msg.data);
      break;
    case 'vaPresets':
    case 'vaParams':
      va.onMessage(msg);
      break;
    case 'dexedNames':
      dexed.onMessage(msg);
      break;
  }
}

// ---- SD card menu -----------------------------------------------------------------

document.getElementById('exportCard').addEventListener('click', async () => {
  send({ type: 'flushSd' });
  await new Promise((r) => setTimeout(r, 100));
  const image = await store.snapshot();
  const url = URL.createObjectURL(new Blob([image], { type: 'application/octet-stream' }));
  const a = Object.assign(document.createElement('a'), { href: url, download: 'lxr-sdcard.img' });
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
});

document.getElementById('importCard').addEventListener('change', async (e) => {
  const file = e.target.files[0];
  e.target.value = '';
  if (!file) return;
  if (file.size !== SD_SECTORS * SECTOR_SIZE) {
    setStatus(`The image must be exactly ${SD_SECTORS * SECTOR_SIZE / 1048576} MB (an exported LXR Web card).`);
    return;
  }
  if (!confirm('Replace the SD card with this image? The page reloads.')) return;
  await store.writeAll(new Uint8Array(await file.arrayBuffer()));
  location.reload();
});

document.getElementById('resetCard').addEventListener('click', async () => {
  if (!confirm('Erase the SD card with all projects, kits and patterns? The page reloads.')) return;
  node?.port.close();
  await store.erase();
  location.reload();
});

// the studio's help strip, in the top bar: it explains whatever the pointer or
// the keyboard focus is on, on every device
const help = new HelpStrip(document.getElementById('help'),
  'Point at any knob, pad or key to see what it does. Alt+1, Alt+2 switch devices.');

buildPanel();
describeLxr();
const tracker = createTrackerDevice({ send, help });
// VA's slots are part of the song: its changes go through the tracker, which saves them
const va = createVaDevice({ send: tracker.send, help, song: tracker.song });
// the mix is part of the song: its changes go through the tracker, which saves them
const mixer = createMixerDevice({ send: tracker.send, help, song: tracker.song });
const dexed = createDexedDevice({ send: tracker.send, help, song: tracker.song });
const plaits = createPlaitsDevice({ send: tracker.send, help, song: tracker.song });
tracker.onLoad((s) => { mixer.render(); dexed.render(); plaits.render(); va.load(s); });
// the demo song at once; the song last edited replaces it when the library answers
tracker.load(demoSong());
(async () => {
  await library.seed(GENRE_SONGS);   // the genre demos, in the library once
  const id = library.currentId();
  const saved = id && (await library.load(id));
  if (saved) {
    saved.id = id;
    tracker.load(saved);
    devices[0].mini(tracker.summary());
  }
})();
document.getElementById('devices').prepend(tracker.el);
document.getElementById('devices').append(va.el);
document.getElementById('devices').append(dexed.el);
document.getElementById('devices').append(plaits.el);
document.getElementById('devices').append(mixer.el);

// ---- the rack: one device expanded, the others as cards in the top bar ----------

const LXR_GUIDE = [
  { title: 'Your first beat', text: 'Drum 1 is selected after switch-on. Click steps 1, 5, 9, 13: their LEDs light, a four-on-the-floor kick. Press Space (or Start). Click Snare, then steps 5 and 13; Hihat cl, then every odd step. Balance the voices with the faders.' },
  { title: 'Shape the sound', text: 'The display\'s top row names four parameters, the bottom row shows their values; a &gt; means the page has more. The four pots change them. The data encoder moves the cursor or changes the value; push it (click, or Enter) to switch. Select buttons 1-8 switch the voice\'s pages. A voice button opens that voice.' },
  { title: 'Modes', text: 'Voice: edit sounds; press again for step edit (Shift: Step). Perf: performance, the select buttons choose the pattern (Shift: Euklid, the Euclidean generator). Load (Shift: FX). Save (Shift: the config menu, MIDI settings and more).' },
  { title: 'Steps and bars', text: 'In step edit, click a step to edit its velocity, probability and note; the select buttons are its sub-steps, for flams and rolls. Shift + a step button selects the function printed under steps 8-16. Page up/down switch bars; hold Copy while pressing Page up to copy the bar into the next.' },
  { title: 'Two buttons with a mouse', text: 'Hold the Shift key and click for Shift. For any other button, such as Copy, Alt-click it to latch it down, click the second button, then click the latched one to release it.' },
  { title: 'Keys and saving', text: 'Space starts and stops; arrow keys turn the encoder, Enter pushes it. The LXR autosaves the kit and pattern 2 s after the last edit, but not while it plays: stop before closing the tab. SD card (top bar) exports a backup.' },
];
const machine = document.getElementById('machine');
addGuide(machine, machine.querySelector('.device-head'), LXR_GUIDE, help);

const devices = [
  { id: 'tracker', name: 'TRK', what: 'tracker', el: tracker.el },
  { id: 'lxr', name: 'LXR', what: 'drum machine', el: machine },
  { id: 'va', name: 'VA', what: 'Orange Mouse VA', el: va.el },
  { id: 'dexed', name: 'DX', what: 'Dexed', el: dexed.el },
  { id: 'plaits', name: 'PL', what: 'Plaits', el: plaits.el },
  { id: 'mixer', name: 'MIX', what: 'mixer', el: mixer.el },
];
let shown = 0;
const rack = document.getElementById('rack');
devices.forEach((d, i) => {
  const card = document.createElement('button');
  card.type = 'button';
  card.className = 'rack-card';
  card.innerHTML = `<span class="rack-head"><span class="led"></span><span class="rack-name">${d.name}<small>Alt+${i + 1}</small></span></span><canvas class="lcd rack-lcd"></canvas>`;
  card.setAttribute('aria-label', `${d.what} (Alt+${i + 1})`);
  card.addEventListener('click', () => showDevice(i));
  // 12 columns: six cards fit across; each device writes its two rows for it
  const mini = new DotMatrix(card.querySelector('canvas'), { cols: 12, rows: 2, dot: 2, margin: 5 });
  d.card = card;
  d.mini = (rows) => { mini.line(0, rows[0] ?? ''); mini.line(1, rows[1] ?? ''); mini.draw(); };
  d.mini(['', '']);
  rack.append(card);
});
va.onSummary((rows) => devices[2].mini(rows));
dexed.onSummary((rows) => devices[3].mini(rows));
plaits.onSummary((rows) => devices[4].mini(rows));
mixer.onSummary((rows) => devices[5].mini(rows));
devices[0].mini(tracker.summary());

function showDevice(i) {
  if (i < 0 || i >= devices.length) return;
  shown = i;
  devices.forEach((d, j) => {
    d.el.hidden = j !== i;
    d.card.classList.toggle('lit', j === i);
    d.card.setAttribute('aria-current', j === i ? 'true' : 'false');
  });
  try { localStorage.setItem('om.device', devices[i].id); } catch { /* storage unavailable: start on the LXR next time */ }
}
let saved = null;
try { saved = localStorage.getItem('om.device'); } catch { /* as above */ }
showDevice(Math.max(0, devices.findIndex((d) => d.id === saved)));
window.orangeMouseUi = { showDevice, devices: devices.map((d) => d.id), lxrPots: () => lxrPots };   // for the browser tests

setUpMidi();
// #preview: the panels without the switch-on overlay and without audio, for
// screenshots (tests) and for looking around
if (location.hash === '#preview') document.getElementById('power').hidden = true;
document.getElementById('powerButton').addEventListener('click', () => {
  switchOn().catch((err) => {
    console.error(err);
    document.getElementById('powerMessage').textContent = `Could not start: ${err.message}`;
  });
});
