// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// A character dot-matrix display of any size, drawn like the LXR's (lcd.js):
// 5x7 characters on a dot grid, the colours from the skin's --lcd-* tokens.
// Every display in the studio is one of these, so they all read alike.
//
//   const d = new DotMatrix(canvas, { cols: 20, rows: 4, dot: 4 });
//   d.line(0, 'VA1 BASS        OSC');   // a whole row, padded or cut
//   d.print(2, 5, '1.2k');              // text at a row and column
//   d.invert(3, 0, 20);                 // cells drawn lit with dark dots (a cursor, a selection)
//   d.draw();
//
// Besides ASCII the displays draw a few glyphs of their own: ² ³ ⁴ (a chord's
// notes), · (an empty cell) and ▶ (playing).

import { FONT } from './font5x7.js';

// the extra glyphs: 7 rows of 5 dots, bit 4 = the leftmost dot
const EXTRA = {
  '²': [0x0c, 0x02, 0x04, 0x08, 0x0e, 0x00, 0x00],
  '³': [0x0e, 0x02, 0x06, 0x02, 0x0e, 0x00, 0x00],
  '⁴': [0x0a, 0x0a, 0x0e, 0x02, 0x02, 0x00, 0x00],
  '·': [0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00],
  '▶': [0x10, 0x18, 0x1c, 0x1e, 0x1c, 0x18, 0x10],
};

export class DotMatrix {
  constructor(canvas, { cols, rows, dot = 4, margin = 8 }) {
    Object.assign(this, { canvas, cols, rows, dot, margin });
    this.ctx = canvas.getContext('2d');
    this.charW = 6 * dot;   // 5 dots and a gap
    this.charH = 9 * dot;   // 8 rows and a gap
    canvas.width = margin * 2 + cols * this.charW - dot;
    canvas.height = margin * 2 + rows * this.charH - dot;
    this.text = Array.from({ length: rows }, () => ' '.repeat(cols));
    this.inverted = Array.from({ length: rows }, () => new Uint8Array(cols));
    this.dirty = true;
  }

  // A whole row: padded with spaces or cut to the display's width.
  line(row, s) {
    const t = String(s).slice(0, this.cols).padEnd(this.cols, ' ');
    if (this.text[row] !== t) {
      this.text[row] = t;
      this.dirty = true;
    }
  }

  // Text at a position in a row, cut at the right edge.
  print(row, col, s) {
    const old = this.text[row];
    const t = String(s).slice(0, Math.max(0, this.cols - col));
    this.line(row, old.slice(0, col) + t + old.slice(col + t.length));
  }

  clear() {
    for (let r = 0; r < this.rows; r++) this.line(r, '');
    this.uninvert();
  }

  // Draws `count` cells of a row inverted: lit, with dark dots.
  invert(row, col, count = 1) {
    if (row < 0 || row >= this.rows) return;
    for (let c = Math.max(0, col); c < Math.min(this.cols, col + count); c++) {
      if (!this.inverted[row][c]) { this.inverted[row][c] = 1; this.dirty = true; }
    }
  }

  uninvert() {
    for (const r of this.inverted) if (r.some((x) => x)) { r.fill(0); this.dirty = true; }
  }

  glyphRows(code) {
    const rows = new Array(8).fill(0);
    const extra = EXTRA[String.fromCharCode(code)];
    if (extra) {
      extra.forEach((bits, r) => { rows[r] = bits; });
    } else if (code >= 0x20 && code < 0x80) {
      const base = (code - 0x20) * 5;
      for (let c = 0; c < 5; c++) {
        const column = FONT[base + c];
        for (let r = 0; r < 7; r++) if (column & (1 << r)) rows[r] |= 0x10 >> c;
      }
    }
    return rows;
  }

  draw() {
    if (!this.dirty) return;
    this.dirty = false;
    const { ctx, canvas, dot } = this;
    const style = getComputedStyle(canvas);
    const bg = style.getPropertyValue('--lcd-bg').trim() || '#000';
    const dotOn = style.getPropertyValue('--lcd-on').trim() || '#fc0';
    const dotOff = style.getPropertyValue('--lcd-off').trim() || '#111';
    ctx.fillStyle = bg;
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    // a 1-pixel gap between dots, except on small modules (2-pixel dots), where
    // it would halve the light: the gaps between characters still show the grid
    const size = dot > 2 ? dot - 1 : dot;
    for (let row = 0; row < this.rows; row++) {
      for (let col = 0; col < this.cols; col++) {
        const glyph = this.glyphRows(this.text[row].charCodeAt(col));
        const x0 = this.margin + col * this.charW;
        const y0 = this.margin + row * this.charH;
        const inv = this.inverted[row][col];
        if (inv) {
          // the whole cell lit, the glyph's dots dark
          ctx.fillStyle = dotOn;
          ctx.fillRect(x0 - dot / 2, y0 - dot / 2, this.charW, this.charH);
        }
        for (let r = 0; r < 8; r++) {
          for (let c = 0; c < 5; c++) {
            const lit = glyph[r] & (0x10 >> c);
            if (inv) {
              if (!lit) continue;
              ctx.fillStyle = bg;
            } else {
              ctx.fillStyle = lit ? dotOn : dotOff;
            }
            ctx.fillRect(x0 + c * dot, y0 + r * dot, size, size);
          }
        }
      }
    }
    canvas.setAttribute('aria-label', `Display: ${this.toString()}`);
  }

  // The display's contents as plain text, for screen readers.
  toString() {
    return this.text.map((t) => t.trimEnd()).join('\n');
  }
}
