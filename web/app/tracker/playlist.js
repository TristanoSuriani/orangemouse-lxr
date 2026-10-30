// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The playlist (PLAN.md Phase 8.2): songs queued one after another, with a
// gap between them, reorderable, startable anywhere. It keeps song ids (the
// library has the songs) and is kept itself (library.setKept). The tracker
// plays it: when a song ends on its own, the next starts after the gap; a
// stop, or loading another song by hand, ends the run.

export const MAX_GAP = 30;   // seconds

export function createPlaylist({ kept = null } = {}) {
  const pl = { items: [], gap: 2, current: -1, active: false };
  const save = () => kept?.set('playlist', { items: pl.items, gap: pl.gap });

  return {
    state: pl,
    async restore() {
      const saved = await kept?.get('playlist');
      if (saved && Array.isArray(saved.items)) {
        pl.items = saved.items.filter((id) => typeof id === 'string');
        pl.gap = Number.isFinite(saved.gap) ? Math.max(0, Math.min(MAX_GAP, saved.gap)) : pl.gap;
      }
    },
    add(id) { pl.items.push(id); save(); return pl.items.length - 1; },
    remove(i) {
      if (i < 0 || i >= pl.items.length) return;
      pl.items.splice(i, 1);
      if (pl.current > i) pl.current--;
      else if (pl.current === i) { pl.current = -1; pl.active = false; }
      save();
    },
    // moves entry i by d places; returns where it went
    move(i, d) {
      const j = i + d;
      if (i < 0 || i >= pl.items.length || j < 0 || j >= pl.items.length) return i;
      [pl.items[i], pl.items[j]] = [pl.items[j], pl.items[i]];
      if (pl.current === i) pl.current = j; else if (pl.current === j) pl.current = i;
      save();
      return j;
    },
    setGap(seconds) { pl.gap = Math.max(0, Math.min(MAX_GAP, seconds)); save(); },
    // songs deleted from the library leave the playlist
    prune(ids) {
      const known = new Set(ids);
      for (let i = pl.items.length - 1; i >= 0; i--) if (!known.has(pl.items[i])) this.remove(i);
    },
    start(i) {
      if (i < 0 || i >= pl.items.length) return null;
      pl.current = i;
      pl.active = true;
      return pl.items[i];
    },
    stop() { pl.active = false; },
    // the playing song ended on its own: the next entry's id, or null at the end
    next() {
      if (!pl.active) return null;
      if (pl.current + 1 >= pl.items.length) { pl.active = false; return null; }
      pl.current++;
      return pl.items[pl.current];
    },
  };
}
