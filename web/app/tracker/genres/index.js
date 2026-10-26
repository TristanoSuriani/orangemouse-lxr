// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The genre demos (PLAN.md Phase 6), in the order the library lists them.

import trance from './trance.js';
import electro from './electro.js';
import synthwave from './synthwave.js';
import dubtechno from './dubtechno.js';

export const GENRE_SONGS = [
  { id: 'genre-trance', make: trance },
  { id: 'genre-electro', make: electro },
  { id: 'genre-synthwave', make: synthwave },
  { id: 'genre-dubtechno', make: dubtechno },
];
