// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The MIDI device: an external sequencer plays the studio.
//
//   display  row 0: the input port; the clock (external or the tempo here),
//            BPM, playing or stopped. Row 1: the sound set, and how many
//            messages came too late. Rows 2-9: the 16 channels and what each
//            plays, a * while a note arrives. Rows 10-11: the knobs.
//   knobs    CH chooses a channel, PLAYS what it plays, BPM the tempo while no
//            clock arrives, AHEAD the lookahead (midi/timing.js)
//   pads     the sound sets (the library's songs, without their patterns), the
//            backup folder, all notes off, the perform view

import { DotMatrix } from '../kit/dotmatrix.js';
import { describe } from '../kit/help.js';
import { makePad, makeKnob, setKnobValue } from '../kit/controls.js';
import { addGuide } from '../kit/guide.js';
import * as library from '../tracker/library.js';
import { backup } from '../tracker/backup.js';
import { CHANNELS, MIN_TEMPO, MAX_TEMPO, MAX_LOOKAHEAD, DESTINATIONS, destination, stepDestination, routeMessage } from '../midi/routes.js';

const COLS = 44;
const ROWS = 12;
const COL_W = 22;
const PER_COLUMN = CHANNELS / 2;
const FIRST = 2;           // the display row of channel 1
const COL_KNOB = 11;
const ACTIVITY_FRAMES = 6; // how long a channel's * stays, in display updates (60 a second)

const GUIDE = [
  { title: 'Connect', text: 'Choose your sequencer\'s port under MIDI in, in the top bar. Hardware shows up when plugged in; on a Mac, a sequencer on this computer reaches the studio through an IAC bus (Audio MIDI Setup). The port is remembered.' },
  { title: 'Who plays what', text: 'Each MIDI channel plays one part: 1-7 the LXR\'s voices, 8-11 VA\'s slots, 12-13 Dexed, 14-15 Plaits, 16 Rings. Click a channel (or the up and down arrows), then turn PLAYS (or the left and right arrows) to change it. A * shows notes arriving.' },
  { title: 'The LXR\'s notes', text: 'On a drum voice\'s channel the note sets its pitch: 60 (C4) is the kit\'s own. To keep the kit\'s pitch with a drum map, choose LXR MENU and give each voice its note in the LXR\'s config menu (Shift + Save); it can also record those notes.' },
  { title: 'CCs', text: 'A CC on a channel that plays VA, Dexed, Plaits or Rings moves that part\'s parameter: 74 cutoff or timbre, 71 resonance, harmonics or structure, 7 volume, 1 the mod wheel. A knob\'s help line says its CC; docs/MIDI_CC.md lists them all. The panel follows and the sound set keeps the last value. On the LXR\'s channels, CCs go to its firmware.' },
  { title: 'The mixer', text: 'On a part\'s channel (VA, Dexed, Plaits, Rings), CC 11 sets its mixer level, 10 its pan, 91 and 93 its sends, and more (the mixer\'s help lines say which). The LXR\'s channels belong to its firmware, so the drums\' channels, the send returns and the master are on a channel set to MIXER.' },
  { title: 'Bend and programs', text: 'Pitch bend bends those parts by 2 semitones (RPN 0 sets another range, up to 24). A program change loads a preset: program 0 is the panel\'s #1; on Dexed, a program of the part\'s bank. On the LXR\'s channels both go to its firmware, which changes patterns on a program change.' },
  { title: 'Clock', text: 'Send MIDI clock and the studio follows it: Start, Continue and Stop drive the LXR\'s own sequencer and the mixer\'s gates, and the tempo reaches the delays and VA. The display says EXT and the BPM it measured. Without a clock, the BPM knob sets the tempo.' },
  { title: 'Tight timing', text: 'Messages are scheduled from the moment they reached the computer, plus AHEAD (10 ms), which absorbs the browser\'s delays. If LATE counts up, turn AHEAD up. A lower AHEAD responds sooner, a higher one stays tighter. Shift the sequencer\'s track delay by the same amount to line up with other gear.' },
  { title: 'Sounds and safety', text: 'SET &lt; and SET &gt; load another sound set: the sounds and mix of a song in the library. Everything you change saves itself. BACKUP chooses a folder that gets a copy of every set. ALL OFF ends every note. The tracker is archived for now: its songs are kept.' },
];

export function createMidiDevice({ send, help, session, settings, saveSettings, inputName = () => '', onPerform = () => {} }) {
  const state = {
    channel: 0,
    status: { running: 0, locked: 0, bpm: settings.tempo, late: 0, lateMs: 0, dropped: 0 },
    activity: new Array(CHANNELS).fill(0),
    message: '',
  };
  const route = () => settings.routes[state.channel];

  const el = document.createElement('section');
  el.className = 'device midi';
  el.setAttribute('aria-label', 'MIDI');
  el.tabIndex = -1;
  el.innerHTML = `
    <header class="device-head"><h2><b>MIDI</b></h2><span class="caption">an external sequencer plays the studio</span><span class="spacer"></span></header>
    <div class="device-top">
      <div class="display-block">
        <canvas class="lcd midi-lcd"></canvas>
      </div>
      <div class="device-pads">
        <div class="pots midi-knobs"></div>
        <div class="group midi-nav"></div>
      </div>
    </div>`;
  const canvas = el.querySelector('.midi-lcd');
  const display = new DotMatrix(canvas, { cols: COLS, rows: ROWS, dot: 2, margin: 6 });
  let summary = () => {};

  // ---- the display ----
  const between = (left, right) => left.slice(0, COLS - right.length - 1).padEnd(COLS - right.length) + right;
  const clockText = () => {
    const s = state.status;
    return `${s.locked ? 'EXT' : 'INT'} ${s.bpm.toFixed(1)} BPM ${s.running ? 'PLAY' : 'STOP'}`;
  };
  const lateText = () => {
    const s = state.status;
    if (s.dropped) return `LOST ${s.dropped}`;
    return s.late ? `LATE ${s.late} (${s.lateMs.toFixed(1)} MS)` : 'ON TIME';
  };

  function render() {
    display.uninvert();
    display.line(0, between(`IN ${(inputName() || 'NONE').toUpperCase()}`, clockText()));
    const alert = state.message || backup.alert().replace('SONGS, B', 'BACKUP').replace('SONGS, A', 'BACKUP');
    display.line(1, between(alert || `SET ${(session.song()?.name ?? '').toUpperCase()}`, lateText()));
    for (let r = 0; r < PER_COLUMN; r++) {
      const cell = (c) => {
        const live = state.activity[c] > 0 ? '*' : ' ';
        return `${String(c + 1).padStart(2, '0')}${live}${destination(settings.routes[c]).name}`.padEnd(COL_W);
      };
      display.line(FIRST + r, cell(r) + cell(r + PER_COLUMN));
    }
    const at = state.channel < PER_COLUMN ? 0 : COL_W;
    display.invert(FIRST + state.channel % PER_COLUMN, at, COL_W - 1);
    const labels = ['CH', 'PLAYS', 'BPM', 'AHEAD'];
    const values = [String(state.channel + 1), destination(route()).name, String(Math.round(settings.tempo)), `${settings.lookahead} MS`];
    display.line(10, labels.map((t) => t.padEnd(COL_KNOB)).join(''));
    display.line(11, values.map((t) => t.slice(0, COL_KNOB - 1).padEnd(COL_KNOB)).join(''));
    display.draw();
    summary([state.status.locked ? `EXT ${state.status.bpm.toFixed(1)}` : `INT ${state.status.bpm.toFixed(1)}`, state.status.running ? 'PLAY' : 'STOP']);
    help.refresh();
  }

  // ---- what the panel changes ----
  function setChannel(c) {
    state.channel = Math.max(0, Math.min(CHANNELS - 1, c));
    render();
  }
  function setRoute(key) {
    if (key === route()) return;
    settings.routes[state.channel] = key;
    send(routeMessage(state.channel, key));
    saveSettings();
    render();
  }
  function setTempo(bpm) {
    settings.tempo = Math.max(MIN_TEMPO, Math.min(MAX_TEMPO, bpm));
    send({ type: 'midiTempo', bpm: settings.tempo });
    saveSettings();
    render();
  }
  function setLookahead(ms) {
    settings.lookahead = Math.max(0, Math.min(MAX_LOOKAHEAD, ms));
    saveSettings();
    render();
  }

  // a short note on the status row, in place of the set's name or an alert
  let messageTimer = null;
  function flash(text) {
    state.message = text;
    clearTimeout(messageTimer);
    messageTimer = setTimeout(() => { state.message = ''; render(); }, 3000);
    render();
  }

  async function stepSet(d) {
    const sets = await library.list();
    if (!sets.length) return;
    sets.sort((a, b) => a.name.localeCompare(b.name));
    const at = sets.findIndex((x) => x.id === session.song()?.id);
    const next = await library.load(sets[(at + d + sets.length) % sets.length].id);
    if (next) session.load(next);
    flash(`SET ${(session.song()?.name ?? '').toUpperCase()}`);
  }

  // ---- knobs ----
  const KNOBS = [
    ['CH', () => `CH: the channel to change, now ${state.channel + 1} (up and down arrows).`, (n) => setChannel(state.channel + n), () => setChannel(0),
      () => [0, CHANNELS - 1, state.channel, `channel ${state.channel + 1}`]],
    ['PLAYS', () => `PLAYS: channel ${state.channel + 1} plays ${destination(route()).words} (left and right arrows).`, (n) => setRoute(stepDestination(route(), n)), () => {},
      () => { const i = DESTINATIONS.findIndex((d) => d.key === route()); return [0, DESTINATIONS.length - 1, i, destination(route()).name]; }],
    ['BPM', () => (state.status.locked ? `BPM: a clock is playing at ${state.status.bpm.toFixed(1)}, it sets the tempo. Without one: ${Math.round(settings.tempo)}.` : `BPM: the tempo of the gates, delays and VA while no clock arrives, now ${Math.round(settings.tempo)}.`),
      (n) => setTempo(Math.round(settings.tempo) + n), () => setTempo(120),
      () => [MIN_TEMPO, MAX_TEMPO, Math.round(settings.tempo), `${Math.round(settings.tempo)} BPM`]],
    ['AHEAD', () => `AHEAD: every message plays this much after it reached the computer, ${settings.lookahead} ms, to absorb the browser's delays. Up if LATE counts up.`,
      (n) => setLookahead(settings.lookahead + n), () => setLookahead(10),
      () => [0, MAX_LOOKAHEAD, settings.lookahead, `${settings.lookahead} ms`]],
  ];
  const knobs = el.querySelector('.midi-knobs');
  for (const [label, text, onStep, onReset, value] of KNOBS) {
    const { wrap, pot } = makeKnob(label, { onStep, onReset });
    describe(pot, help, text);
    pot.addEventListener('focus', () => { const [min, max, v, words] = value(); setKnobValue(pot, { min, max }, v, words); });
    const caption = document.createElement('span');
    caption.className = 'caption';
    caption.textContent = label;
    wrap.append(caption);
    knobs.append(wrap);
  }

  // ---- pads ----
  const NAV = [
    ['SET &lt;', 'The previous sound set: the sounds and mix of a song in the library.', () => stepSet(-1)],
    ['SET &gt;', 'The next sound set: the sounds and mix of a song in the library.', () => stepSet(1)],
    ['BACKUP', () => `A folder that gets a copy of every sound set and song as it saves. Now: ${backup.describe().toLowerCase()}.`, async () => {
      flash(await backup.choose() ? 'BACKUP FOLDER SET' : 'NO BACKUP FOLDER CHOSEN');
    }],
    ['ALL OFF', 'Ends every note and stops following the clock until the next Start.', () => send({ type: 'midiReset' })],
    ['PERF', 'The perform view, full screen: every part\'s level as dots (F).', () => onPerform()],
  ];
  for (const [label, text, action] of NAV) {
    const pad = makePad(label, { kind: 'small', onChange: (down) => down && action() });
    describe(pad, help, text);
    el.querySelector('.midi-nav').append(pad);
  }

  // ---- the display: click a channel ----
  const channelAt = (e) => {
    const rect = canvas.getBoundingClientRect();
    const col = Math.floor(((e.clientX - rect.left) / rect.width) * canvas.width / display.charW);
    const row = Math.floor(((e.clientY - rect.top) / rect.height) * canvas.height / display.charH);
    return row >= FIRST && row < FIRST + PER_COLUMN ? row - FIRST + (col >= COL_W ? PER_COLUMN : 0) : -1;
  };
  let hover = -1;
  canvas.addEventListener('pointerdown', (e) => { const c = channelAt(e); if (c >= 0) setChannel(c); });
  canvas.addEventListener('pointermove', (e) => { hover = channelAt(e); help.refresh(); });
  canvas.addEventListener('pointerleave', () => { hover = -1; });
  describe(canvas, help, () => (hover >= 0
    ? `Channel ${hover + 1} plays ${destination(settings.routes[hover]).words}. Click to choose it, then turn PLAYS.`
    : 'The 16 MIDI channels and what each plays; a * while its notes arrive. Click one to change it.'));

  // arrows: up and down the channels, left and right what it plays
  function handleKey(e, down) {
    if (!down || e.metaKey || e.ctrlKey || e.altKey) return false;
    if (e.key === 'ArrowUp') setChannel(state.channel - 1);
    else if (e.key === 'ArrowDown') setChannel(state.channel + 1);
    else if (e.key === 'ArrowLeft') setRoute(stepDestination(route(), -1));
    else if (e.key === 'ArrowRight') setRoute(stepDestination(route(), 1));
    else return false;
    return true;
  }

  // from the worklet: [running, locked, bpm, late, lateMs, dropped, activity bits]
  function onStatus(s) {
    const was = state.status;
    state.status = { running: s[0], locked: s[1], bpm: s[2], late: s[3], lateMs: s[4], dropped: s[5] };
    let changed = was.running !== s[0] || was.locked !== s[1] || was.bpm !== s[2] || was.late !== s[3] || was.dropped !== s[5];
    for (let c = 0; c < CHANNELS; c++) {
      const before = state.activity[c];
      state.activity[c] = s[6] & (1 << c) ? ACTIVITY_FRAMES : Math.max(0, before - 1);
      if ((before > 0) !== (state.activity[c] > 0)) changed = true;
    }
    if (changed) render();
  }

  addGuide(el, el.querySelector('.device-head'), GUIDE, help);
  backup.onChange(() => render());
  // for the browser tests: a channel's destination
  const setRouteOf = (channel, key) => { const was = state.channel; state.channel = channel; setRoute(key); state.channel = was; render(); };
  return { el, render, handleKey, onStatus, setRoute: setRouteOf, status: () => state.status, onSummary: (fn) => { summary = fn; render(); } };
}
