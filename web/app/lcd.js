// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// Draws the 16x2 character display from the firmware's display RAM: codes
// 0-15 are the eight custom characters (CGRAM), 32-127 come from a 5x7 font
// like the controller's character ROM.

import { FONT } from './kit/font5x7.js';

const COLS = 16;
const ROWS = 2;
const DOT = 4;            // dot pitch in canvas pixels
const CHAR_W = 6 * DOT;   // 5 dots + gap
const CHAR_H = 9 * DOT;   // 8 rows + gap
const MARGIN = 8;

export class Lcd {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    canvas.width = MARGIN * 2 + COLS * CHAR_W - DOT;
    canvas.height = MARGIN * 2 + ROWS * CHAR_H - DOT;
    this.text = new Uint8Array(32).fill(32);
    this.cgram = new Uint8Array(64);
    this.on = true;
    this.draw();
  }

  update(text, cgram, on) {
    this.text = text;
    this.cgram = cgram;
    this.on = on;
    this.draw();
  }

  // Rows of a character cell as 5-bit values, bit 4 = leftmost dot.
  glyphRows(code) {
    const rows = new Array(8).fill(0);
    if (code < 16) {
      const base = (code & 7) * 8;
      for (let r = 0; r < 8; r++) rows[r] = this.cgram[base + r] & 0x1f;
    } else if (code >= 0x20 && code < 0x80) {
      const base = (code - 0x20) * 5;
      for (let c = 0; c < 5; c++) {
        const column = FONT[base + c];
        for (let r = 0; r < 7; r++) if (column & (1 << r)) rows[r] |= 0x10 >> c;
      }
    } else if (code === 0xff) {
      rows.fill(0x1f);
    }
    return rows;
  }

  draw() {
    const { ctx, canvas } = this;
    const style = getComputedStyle(canvas);
    const bg = style.getPropertyValue('--lcd-bg').trim() || '#000';
    const dotOn = style.getPropertyValue('--lcd-on').trim() || '#fc0';
    const dotOff = style.getPropertyValue('--lcd-off').trim() || '#111';
    ctx.fillStyle = bg;
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    for (let row = 0; row < ROWS; row++) {
      for (let col = 0; col < COLS; col++) {
        const rows = this.glyphRows(this.on ? this.text[row * COLS + col] : 32);
        const x0 = MARGIN + col * CHAR_W;
        const y0 = MARGIN + row * CHAR_H;
        for (let r = 0; r < 8; r++) {
          for (let c = 0; c < 5; c++) {
            ctx.fillStyle = rows[r] & (0x10 >> c) ? dotOn : dotOff;
            ctx.fillRect(x0 + c * DOT, y0 + r * DOT, DOT - 1, DOT - 1);
          }
        }
      }
    }
  }

  // The display contents as plain text, for screen readers.
  toString() {
    const row = (r) => String.fromCharCode(...this.text.subarray(r * COLS, r * COLS + COLS)
      .map((c) => (c >= 32 && c < 127 ? c : 32)));
    return `${row(0)}\n${row(1)}`;
  }
}
