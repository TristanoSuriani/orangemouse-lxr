// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The perform view (PLAN.md Phase 8.3): the big display, full screen, as
// visuals that react to each track's level. One field of amber dots, as the
// displays are: the song and where it plays on top; a column per track that
// rises with its level, a peak dot above it, sparks thrown up when the track
// hits; a ring from the centre on every kick (track 1). F or the Perform
// button opens it, F or Esc closes it; the studio plays on underneath.

import { FONT } from './kit/font5x7.js';
import { CHANNELS } from './mixer/mix.js';

const W = 160, H = 90;          // dots
const TRACKS = 16;
const COL = 10;                 // dots per track column: 9 lit, 1 gap
const BASE = H - 2;             // the columns' foot
const TALL = 52;                // a column at 0 dBFS
const FLOOR_DB = -48;
const FALL_DB = 30;             // per second, after a peak
const HIT_DB = 6;               // a rise this fast (per frame) is a hit

// the mixer channel a track plays on (mix.js CHANNELS: the LXR's hihats share a stem)
const channelOf = (t) => {
  if (!t || t.engine === null || t.engine === undefined) return -1;
  const firsts = [0, 7, 11, 13];
  const part = t.engine === 0 && t.part === 6 ? 5 : t.part;
  const c = firsts[t.engine] + part;
  return c < CHANNELS.length ? c : -1;
};

export function createPerformView({ song, playing }) {
  const el = document.createElement('div');
  el.className = 'perform';
  el.hidden = true;
  el.setAttribute('role', 'dialog');
  el.setAttribute('aria-modal', 'true');
  el.setAttribute('aria-label', 'Perform view');
  el.innerHTML = `<canvas class="perform-lcd" role="img" aria-label="The tracks' levels"></canvas>
    <button class="dock-button perform-close" type="button">Close (Esc)</button>`;
  const canvas = el.querySelector('canvas');
  const ctx = canvas.getContext('2d');
  const lit = new Uint8Array(W * H);
  const level = new Float32Array(TRACKS).fill(FLOOR_DB);   // dB, falling
  const peak = new Float32Array(TRACKS).fill(FLOOR_DB);
  const peakAge = new Float32Array(TRACKS);
  let sparks = [], rings = [];
  let fresh = new Float32Array(CHANNELS.length);   // the meters since the last frame
  let open = false, frame = 0, last = 0, opener = null, seed = 1;
  const rand = () => { seed = (seed * 1103515245 + 12345) >>> 0; return seed / 4294967296; };

  const set = (x, y, top = 0) => { if (x >= 0 && x < W && y >= top && y < H) lit[(y | 0) * W + (x | 0)] = 1; };
  const below = (x, y) => set(x, y, 12);   // the visuals stay under the header
  function text(x, y, s) {
    for (const ch of s) {
      const code = ch.charCodeAt(0);
      if (code >= 0x20 && code < 0x80)
        for (let c = 0; c < 5; c++) {
          const col = FONT[(code - 0x20) * 5 + c];
          for (let r = 0; r < 7; r++) if (col & (1 << r)) set(x + c, y + r);
        }
      x += 6;
    }
  }

  function step(dt) {
    const s = song();
    const state = playing();
    for (let k = 0; k < TRACKS; k++) {
      const c = channelOf(s.tracks[k]);
      const db = c >= 0 ? Math.max(FLOOR_DB, 20 * Math.log10(fresh[c] + 1e-9)) : FLOOR_DB;
      const muted = s.tracks[k].mute;
      const was = level[k];
      level[k] = muted ? FLOOR_DB : Math.max(db, was - FALL_DB * dt);
      if (!muted && db - was > HIT_DB && db > FLOOR_DB + 12) {
        const x0 = 1 + k * COL, top = BASE - ((level[k] - FLOOR_DB) / -FLOOR_DB) * TALL;
        for (let i = 0; i < 6 + ((db - FLOOR_DB) / 6 | 0); i++)
          sparks.push({ x: x0 + rand() * (COL - 1), y: top, vx: (rand() - 0.5) * 30, vy: -40 - rand() * 60, life: 0.5 + rand() * 0.6 });
        if (k === 0) rings.push({ r: 2, life: 0.6 });
      }
      if (level[k] >= peak[k]) { peak[k] = level[k]; peakAge[k] = 0; }
      else if ((peakAge[k] += dt) > 0.6) peak[k] = Math.max(level[k], peak[k] - FALL_DB * dt);
    }
    fresh.fill(0);
    sparks = sparks.filter((p) => (p.life -= dt) > 0);
    for (const p of sparks) { p.vy += 90 * dt; p.x += p.vx * dt; p.y += p.vy * dt; }
    rings = rings.filter((r) => (r.life -= dt) > 0);
    for (const r of rings) r.r += 120 * dt;

    // the picture
    lit.fill(0);
    const hex = (n) => n.toString(16).toUpperCase().padStart(2, '0');
    const where = state[0] ? `P${hex(state[1])}:${hex(state[3])} ${Math.round(state[4])}BPM` : 'STOPPED';
    text(W - 1 - where.length * 6, 1, where);
    text(1, 1, (s.name || '').toUpperCase().slice(0, Math.floor((W - 8 - where.length * 6) / 6)));
    for (let x = 0; x < W; x += 2) set(x, 10);
    for (const r of rings) {
      if (r.life < 0.2 && frame % 2) continue;   // a ring blinks out
      for (let a = 0; a < 360; a += 360 / Math.max(24, r.r * 4)) below(W / 2 + Math.cos(a * Math.PI / 180) * r.r, 50 + Math.sin(a * Math.PI / 180) * r.r * 0.8);
    }
    for (let k = 0; k < TRACKS; k++) {
      const x0 = 1 + k * COL;
      const h = Math.round(((level[k] - FLOOR_DB) / -FLOOR_DB) * TALL);
      for (let y = 0; y < h; y++) if (y % 3 !== 2) for (let x = 0; x < COL - 1; x++) set(x0 + x, BASE - y);
      const p = Math.round(((peak[k] - FLOOR_DB) / -FLOOR_DB) * TALL);
      if (p > 0) for (let x = 1; x < COL - 2; x++) set(x0 + x, BASE - p - 1);
      set(x0 + 4, BASE + 1);   // the column's foot
    }
    for (const p of sparks) if (p.life > 0.15 || frame % 2) below(p.x, p.y);
  }

  function draw() {
    const box = el.getBoundingClientRect();
    const dot = Math.max(2, Math.floor(Math.min(box.width / (W + 2), (box.height - 40) / (H + 2))));
    if (canvas.width !== (W + 2) * dot) { canvas.width = (W + 2) * dot; canvas.height = (H + 2) * dot; }
    const style = getComputedStyle(el);
    ctx.fillStyle = style.getPropertyValue('--lcd-bg').trim() || '#000';
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    const size = dot > 2 ? dot - 1 : dot;
    const on = style.getPropertyValue('--lcd-on').trim() || '#fc0';
    const off = style.getPropertyValue('--lcd-off').trim() || '#111';
    for (const [colour, want] of [[off, 0], [on, 1]]) {
      ctx.fillStyle = colour;
      for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) if (lit[y * W + x] === want) ctx.fillRect((x + 1) * dot, (y + 1) * dot, size, size);
    }
  }

  function tick(now) {
    if (!open) return;
    const dt = last ? Math.min(0.1, (now - last) / 1000) : 1 / 60;
    last = now;
    frame++;
    step(dt);
    draw();
    if (frame % 30 === 0) {
      const s = song();
      const loud = [...level].map((db, k) => [k, db]).filter(([, db]) => db > -30).map(([k]) => s.tracks[k].name);
      canvas.setAttribute('aria-label', `The tracks' levels: ${loud.length ? `${loud.join(', ')} sounding` : 'quiet'}`);
    }
    requestAnimationFrame(tick);
  }

  function show() {
    if (open) return;
    open = true;
    opener = document.activeElement;
    el.hidden = false;
    last = 0;
    el.querySelector('.perform-close').focus();
    el.requestFullscreen?.().catch(() => {});   // where the browser allows it
    requestAnimationFrame(tick);
  }
  function hide() {
    if (!open) return;
    open = false;
    el.hidden = true;
    if (document.fullscreenElement === el) document.exitFullscreen?.().catch(() => {});
    opener?.focus?.();
  }
  el.querySelector('.perform-close').addEventListener('click', hide);
  document.addEventListener('fullscreenchange', () => { if (open && !document.fullscreenElement && el.dataset.wasFull) hide(); el.dataset.wasFull = document.fullscreenElement === el ? '1' : ''; });

  return {
    el,
    isOpen: () => open,
    toggle: () => (open ? hide() : show()),
    show,
    hide,
    lit: () => lit.reduce((a, b) => a + b, 0),   // for the browser test
    // the mixer's meters (the peaks since the last message): kept until the next frame
    onMeters(m) { for (let c = 0; c < fresh.length; c++) fresh[c] = Math.max(fresh[c], m[c] || 0); },
  };
}
