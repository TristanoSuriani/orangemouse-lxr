// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// When a MIDI message should sound. Web MIDI stamps each message with the time
// it reached the computer, but the page sees it whenever its main thread gets
// round to it, and the audio thread later still. So a message is scheduled at
// its stamp plus a short lookahead: the jitter of the way here turns into a
// fixed delay, as long as the message arrives within the lookahead.
//
// That needs the audio clock in the stamps' terms. context.currentTime moves in
// jumps (a whole audio callback at a time) and is only ever behind the true
// position, so the largest difference seen lately is the closest to it.

const WINDOW_MS = 5000;
const AUDIO_JUMP_S = 0.1;      // the audio clock fell this far back: it stalled, start again
const OLDEST_STAMP_MS = 1000;  // an older stamp, or one from the future, is no use: use the time now

export function createTiming({ audioTime, rate, now = () => performance.now() }) {
  let samples = [];   // { t, offset }

  const best = () => samples.reduce((m, s) => Math.max(m, s.offset), -Infinity);

  function sample() {
    const t = now();
    const offset = audioTime() - t / 1000;
    if (samples.length && offset < best() - AUDIO_JUMP_S) samples = [];
    samples = samples.filter((s) => t - s.t < WINDOW_MS);
    samples.push({ t, offset });
  }

  // The audio frame (of the context) a message stamped `stampMs` should sound on.
  function frameFor(stampMs, lookaheadMs) {
    sample();
    const t = now();
    const usable = Number.isFinite(stampMs) && stampMs <= t && stampMs >= t - OLDEST_STAMP_MS;
    return Math.round(((usable ? stampMs : t) / 1000 + best() + lookaheadMs / 1000) * rate);
  }

  return { sample, frameFor };
}
