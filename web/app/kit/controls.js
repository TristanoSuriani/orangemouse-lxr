// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The studio's hardware controls, the same on every device (PLAN.md, "One
// interaction grammar"), in the LXR's skin (style.css: .pad, .pot):
//
//   makePad    a button with an LED, a label and a grey second-function label.
//              Click and hold to hold it; Alt-click latches it down.
//   makeKnob   an endless knob: drag up/right or scroll to turn, Shift for
//              fine steps, double-click to reset, arrow keys when focused.
//   makeRotary the drag and scroll gesture alone, for other rotary controls.

// ---- pads ---------------------------------------------------------------------

// onChange(down) is called on every press and release.
export function makePad(label, { alt = '', kind = '', onChange = () => {} } = {}) {
  const el = document.createElement('button');
  el.type = 'button';
  el.className = `pad ${kind}`.trim();
  el.innerHTML = `<span class="led"></span><span class="label">${label}</span>${alt ? `<span class="alt">${alt}</span>` : ''}`;
  el.setAttribute('aria-label', alt ? `${label} (shift: ${alt})` : label);
  el.setAttribute('aria-pressed', 'false');

  let held = false;     // pressed by pointer or keyboard
  let latched = false;  // held down with Alt-click
  const update = () => {
    const down = held || latched;
    el.classList.toggle('down', down);
    el.classList.toggle('latched', latched);
    el.setAttribute('aria-pressed', String(down));
    onChange(down);
  };
  el.addEventListener('pointerdown', (e) => {
    if (e.button !== 0) return;
    e.preventDefault();
    if (e.altKey) {
      latched = !latched;
      update();
      return;
    }
    if (latched) {
      latched = false;
      update();
      return;
    }
    el.setPointerCapture(e.pointerId);
    held = true;
    update();
  });
  const release = () => {
    if (!held) return;
    held = false;
    update();
  };
  el.addEventListener('pointerup', release);
  el.addEventListener('pointercancel', release);
  el.addEventListener('keydown', (e) => {
    if ((e.key === ' ' || e.key === 'Enter') && !e.repeat) {
      e.preventDefault();
      e.stopPropagation();
      held = true;
      update();
    }
  });
  el.addEventListener('keyup', (e) => {
    if (e.key === ' ' || e.key === 'Enter') {
      e.preventDefault();
      e.stopPropagation();
      release();
    }
  });
  return el;
}

export function setLed(pad, on) {
  pad.classList.toggle('lit', !!on);
}

// ---- rotary controls --------------------------------------------------------------

// Drag up/right or scroll up to turn clockwise. Calls onTurn(amount in pixels, event).
export function makeRotary(el, onTurn) {
  let last = null;
  el.addEventListener('pointerdown', (e) => {
    if (e.button !== 0) return;
    el.setPointerCapture(e.pointerId);
    last = { x: e.clientX, y: e.clientY };
  });
  el.addEventListener('pointermove', (e) => {
    if (!last) return;
    const delta = (e.clientX - last.x) - (e.clientY - last.y);
    last = { x: e.clientX, y: e.clientY };
    if (delta) onTurn(delta, e);
  });
  const end = () => { last = null; };
  el.addEventListener('pointerup', end);
  el.addEventListener('pointercancel', end);
  el.addEventListener('wheel', (e) => {
    e.preventDefault();
    onTurn(-Math.sign(e.deltaY) * 8, e);
  }, { passive: false });
}

const PIXELS_PER_STEP = 4;        // a value step per 4 pixels of drag
const FINE_PIXELS_PER_STEP = 16;  // with Shift

// An endless knob that turns a value in steps. onStep(steps) is called with
// whole steps (positive = up); onReset() on double-click. The knob's pointer
// only shows the turning; the value lives with the caller.
export function makeKnob(label, { onStep, onReset = () => {} }) {
  const wrap = document.createElement('div');
  wrap.className = 'pot-wrap';
  wrap.innerHTML = `<div class="pot" tabindex="0" role="slider" aria-label="${label}"><div class="pot-mark"></div></div>`;
  const pot = wrap.firstElementChild;
  let angle = 0;
  let pixels = 0;
  const spin = (degrees) => {
    angle += degrees;
    pot.style.setProperty('--angle', `${angle}deg`);
  };
  makeRotary(pot, (px, e) => {
    spin(px * 1.5);
    pixels += px;
    const per = e.shiftKey ? FINE_PIXELS_PER_STEP : PIXELS_PER_STEP;
    const steps = Math.trunc(pixels / per);
    if (steps) {
      pixels -= steps * per;
      onStep(steps);
    }
  });
  pot.addEventListener('dblclick', () => onReset());
  pot.addEventListener('keydown', (e) => {
    const up = e.key === 'ArrowUp' || e.key === 'ArrowRight';
    const down = e.key === 'ArrowDown' || e.key === 'ArrowLeft';
    if (!up && !down) return;
    e.preventDefault();
    e.stopPropagation();
    const steps = (up ? 1 : -1) * (e.shiftKey ? 1 : 4);
    spin(steps * 6);
    onStep(steps);
  });
  return { wrap, pot };
}
