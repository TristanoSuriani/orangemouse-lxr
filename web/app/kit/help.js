// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The help strip under a device's display: a three-row dot-matrix display
// that spells out whatever control the pointer or the keyboard focus is on
// (PLAN.md, "Understandable without a manual"). Controls register a text
// with describe(); the text can change with the control's value.
//
//   const help = new HelpStrip(canvas, 'Point at a control to see what it does.');
//   describe(knob, help, () => 'CUT Cutoff 1.2 kHz: the filter frequency.');

import { DotMatrix } from './dotmatrix.js';

// 40 columns of 2-pixel dots: as wide as the 20-column displays (4-pixel dots),
// drawn at its own size so the dots stay sharp
const COLS = 40;
const ROWS = 3;

// Splits text into at most ROWS rows of COLS, at spaces; '>' marks a cut.
export function wrap(text) {
  const rows = [''];
  for (const w of text.split(/\s+/)) {
    const i = rows.length - 1;
    if (!rows[i]) rows[i] = w;
    else if (rows[i].length + 1 + w.length <= COLS) rows[i] = `${rows[i]} ${w}`;
    else if (rows.length < ROWS) rows.push(w);
    else { rows[i] = (rows[i] + ' ' + w).slice(0, COLS - 1) + '>'; break; }
  }
  return rows;
}

export class HelpStrip {
  constructor(canvas, idleText) {
    this.display = new DotMatrix(canvas, { cols: COLS, rows: ROWS, dot: 2, margin: 8 });
    this.idleText = idleText;
    this.source = null;   // the function giving the current text
    canvas.setAttribute('role', 'status');
    canvas.setAttribute('aria-live', 'polite');
    this.show(null);
  }

  // Shows a control's text (a function, read again on refresh), or the idle text.
  show(source) {
    this.source = source;
    this.refresh();
  }

  refresh() {
    const text = this.source ? this.source() : this.idleText;
    const rows = wrap(text);
    for (let r = 0; r < ROWS; r++) this.display.line(r, rows[r] ?? '');
    this.display.draw();
  }
}

// Hover or keyboard focus on `el` shows its text in the strip.
export function describe(el, help, text) {
  const source = typeof text === 'function' ? text : () => text;
  el.addEventListener('pointerenter', () => help.show(source));
  el.addEventListener('focus', () => help.show(source));
  el.addEventListener('pointerleave', () => { if (help.source === source) help.show(null); });
  el.addEventListener('blur', () => { if (help.source === source) help.show(null); });
  el.title = '';   // the strip replaces tooltips
  return source;
}
