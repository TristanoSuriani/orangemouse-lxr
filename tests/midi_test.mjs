// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The MIDI input's pure parts: when a message sounds (web/app/midi/timing.js)
// and the channels' settings (web/app/midi/routes.js).
import assert from 'node:assert/strict';
import { createTiming } from '../web/app/midi/timing.js';
import { CHANNELS, DESTINATIONS, defaultRoutes, destination, parseSettings, routeMessages, stepDestination } from '../web/app/midi/routes.js';

const RATE = 44100;

// a fake clock: the audio position jumps in callbacks of `buffer` frames and is
// `offsetS` ahead of performance.now's zero
function clocks({ buffer = 128, offsetS = 3 } = {}) {
  const c = { t: 1000, audioTime: () => offsetS + Math.floor(((c.t / 1000) * RATE) / buffer) * buffer / RATE };
  return c;
}

// ---- timing ----
{
  // however the audio clock jumps, messages stamped 10 ms apart land 10 ms apart
  for (const buffer of [128, 512, 2048]) {
    const c = clocks({ buffer });
    const timing = createTiming({ audioTime: c.audioTime, rate: RATE, now: () => c.t });
    for (let i = 0; i < 400; i++) { c.t += 7.3; timing.sample(); }
    const frames = [];
    for (let i = 0; i < 20; i++) {
      const stamp = c.t;
      c.t += 1 + (i * 37 % 9);        // the main thread sees it 1-9 ms later
      frames.push(timing.frameFor(stamp, 10));
      c.t = stamp + 10;
    }
    const gaps = frames.slice(1).map((f, i) => f - frames[i]);
    const worst = Math.max(...gaps.map((g) => Math.abs(g - 441)));
    assert.ok(worst <= 2, `buffer ${buffer}: messages 10 ms apart land ${gaps} frames apart`);
  }

  // the lookahead moves it by exactly that much
  {
    const c = clocks();
    const timing = createTiming({ audioTime: c.audioTime, rate: RATE, now: () => c.t });
    for (let i = 0; i < 100; i++) { c.t += 7.3; timing.sample(); }
    const stamp = c.t - 2;
    assert.equal(timing.frameFor(stamp, 20) - timing.frameFor(stamp, 10), 441);
  }

  // it lands ahead of what has been rendered, by about the lookahead
  {
    const c = clocks({ buffer: 128 });
    const timing = createTiming({ audioTime: c.audioTime, rate: RATE, now: () => c.t });
    for (let i = 0; i < 400; i++) { c.t += 7.3; timing.sample(); }
    const ahead = timing.frameFor(c.t, 10) - c.audioTime() * RATE;
    assert.ok(ahead >= 441 - 2 && ahead <= 441 + 128 + 2, `10 ms lands ${ahead} frames ahead`);
  }

  // an unusable stamp (none, older than a second, from the future) means now
  {
    const c = clocks();
    const timing = createTiming({ audioTime: c.audioTime, rate: RATE, now: () => c.t });
    for (let i = 0; i < 100; i++) { c.t += 7.3; timing.sample(); }
    const now = timing.frameFor(c.t, 10);
    for (const stamp of [undefined, NaN, 0, c.t - 5000, c.t + 5000]) assert.equal(timing.frameFor(stamp, 10), now, `stamp ${stamp}`);
  }

  // the audio stalled: its clock is behind now, and that is believed at once
  {
    const c = clocks();
    let lost = 0;
    const timing = createTiming({ audioTime: () => c.audioTime() - lost, rate: RATE, now: () => c.t });
    for (let i = 0; i < 100; i++) { c.t += 7.3; timing.sample(); }
    const before = timing.frameFor(c.t, 10);
    lost = 0.5;
    const after = timing.frameFor(c.t, 10);
    assert.ok(Math.abs(before - after - 0.5 * RATE) < 200, `a 500 ms stall moves it by ${before - after} frames`);
  }
}

// ---- routes ----
{
  const routes = defaultRoutes();
  assert.equal(routes.length, CHANNELS);
  const messages = routeMessages(routes);
  // the tracks the songs were written for
  assert.deepEqual(messages[0], { type: 'midiRoute', channel: 0, engine: 0, part: 0 });
  assert.deepEqual(messages[6], { type: 'midiRoute', channel: 6, engine: 0, part: 6 });
  assert.deepEqual(messages[7], { type: 'midiRoute', channel: 7, engine: 1, part: 0 });
  assert.deepEqual(messages[10], { type: 'midiRoute', channel: 10, engine: 1, part: 3 });
  assert.deepEqual(messages[11], { type: 'midiRoute', channel: 11, engine: 2, part: 0 });
  assert.deepEqual(messages[13], { type: 'midiRoute', channel: 13, engine: 3, part: 0 });
  assert.deepEqual(messages[15], { type: 'midiRoute', channel: 15, engine: 4, part: 0 });

  assert.equal(destination('nonsense').key, 'off');
  assert.equal(stepDestination('off', -1), 'off', 'stops at the start');
  assert.equal(stepDestination(DESTINATIONS.at(-1).key, 1), DESTINATIONS.at(-1).key, 'stops at the end');
  assert.equal(stepDestination('lxr0', 1), 'lxr1');
  assert.equal(new Set(DESTINATIONS.map((d) => d.key)).size, DESTINATIONS.length, 'the keys are distinct');

  // settings: anything in storage gives something usable
  for (const text of [null, '', 'not json', '[]', '{"routes":3,"tempo":"x","lookahead":null}']) {
    const s = parseSettings(text);
    assert.deepEqual(s.routes, routes, `routes from ${text}`);
    assert.equal(s.tempo, 120);
    assert.equal(s.lookahead, 10);
  }
  const s = parseSettings(JSON.stringify({ routes: ['va1', 'gone', 'off'], tempo: 9999, lookahead: -3, input: 'IAC Bus 1' }));
  assert.deepEqual(s.routes.slice(0, 4), ['va1', 'lxr1', 'off', 'lxr3'], 'a known route is kept, an unknown one is the default');
  assert.equal(s.tempo, 300);
  assert.equal(s.lookahead, 0);
  assert.equal(s.input, 'IAC Bus 1');
}

console.log('midi: all checks passed');
