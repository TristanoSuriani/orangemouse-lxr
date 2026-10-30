// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The playlist (PLAN.md Phase 8.2): queueing, moving, removing, the gap, and
// what plays next, against a fake store. Run by tests/run.sh:
//   node tests/playlist_test.mjs

import { createPlaylist, MAX_GAP } from '../web/app/tracker/playlist.js';

let failures = 0;
const check = (ok, msg) => { if (!ok) { failures++; console.log(`FAIL ${msg}`); } };
const store = new Map();
const kept = { get: async (k) => store.get(k), set: async (k, v) => store.set(k, structuredClone(v)) };

const pl = createPlaylist({ kept });
['a', 'b', 'c'].forEach((id) => pl.add(id));
check(pl.start(0) === 'a' && pl.state.active, 'start plays the first');
check(pl.next() === 'b' && pl.state.current === 1, 'then the second');
check(pl.move(1, 1) === 2 && pl.state.items.join('') === 'acb' && pl.state.current === 2, `moving the playing one keeps it playing: ${pl.state.items}`);
check(pl.next() === null && !pl.state.active, 'past the last, the run ends');
pl.start(0); pl.stop();
check(pl.next() === null, 'after a stop nothing follows');
pl.setGap(99); check(pl.state.gap === MAX_GAP, 'the gap has a limit');
pl.setGap(-3); check(pl.state.gap === 0, 'and no negative gap');
pl.start(2); pl.remove(0);
check(pl.state.current === 1 && pl.state.items[1] === 'b', 'removing an earlier one keeps the playing one');
pl.remove(1);
check(!pl.state.active && pl.state.current === -1, 'removing the playing one ends the run');
pl.add('z'); pl.prune(['c']);
check(pl.state.items.join('') === 'c', `songs gone from the library leave: ${pl.state.items}`);
const again = createPlaylist({ kept });
await again.restore();
check(again.state.items.join('') === 'c' && again.state.gap === 0, 'it is kept');

if (failures) { console.log(`${failures} playlist checks failed`); process.exit(1); }
console.log('ok: the playlist queues, moves, removes, keeps its gap and plays each next');
