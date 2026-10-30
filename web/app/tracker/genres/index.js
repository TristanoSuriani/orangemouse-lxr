// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The genre demos (PLAN.md Phase 6), in the order the library lists them.
// A new version replaces the library's copy (library.seed keeps the old one).
// Taken out: the trance demo and "First steps" (RETIRED_SONGS: main.js deletes
// them from the library once). Trance came back as version 3, a new song.

import electro from './electro.js';
import synthwave from './synthwave.js';
import dubtechno from './dubtechno.js';
import trance, { VERSION as TRANCE_VERSION } from './trance.js';

export const RETIRED_SONGS = { ids: ['genre-trance'], names: ['First steps', 'Trance: Dawn Run', 'Trance: Dawn Run (v1)'] };

export const GENRE_SONGS = [
  { id: 'genre-electro', make: electro },
  { id: 'genre-synthwave', make: synthwave },
  { id: 'genre-dubtechno', make: dubtechno },
  { id: 'genre-trance-3', make: trance, version: TRANCE_VERSION },   // trance with the gate, the ducking, the effects (PLAN.md 10.6)
];
