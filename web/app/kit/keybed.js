// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// A two-octave keybed. Each key shows the computer key that plays it (the
// TRIS layout: Z..M the lower octave, Q..I the upper) and lights while it
// sounds, whether it was clicked or typed (PLAN.md, "The keybed shows its keys").
//
//   const keys = makeKeybed({ onNote: (semitone, on) => ... });
//   keys.light(semitone, on);           // from the computer keyboard
//   KEY_SEMITONES['KeyZ'] === 0

export const KEY_SEMITONES = {
  KeyZ: 0, KeyS: 1, KeyX: 2, KeyD: 3, KeyC: 4, KeyV: 5, KeyG: 6, KeyB: 7, KeyH: 8, KeyN: 9, KeyJ: 10, KeyM: 11,
  Comma: 12, KeyQ: 12, Digit2: 13, KeyW: 14, Digit3: 15, KeyE: 16, KeyR: 17, Digit5: 18, KeyT: 19, Digit6: 20,
  KeyY: 21, Digit7: 22, KeyU: 23, KeyI: 24,
};

// the label printed on each key: the first computer key that plays it
const LABELS = {};
for (const [code, semi] of Object.entries(KEY_SEMITONES)) {
  if (!(semi in LABELS)) LABELS[semi] = code.replace(/^Key|^Digit/, '').replace('Comma', ',');
}

const BLACK = new Set([1, 3, 6, 8, 10]);

// describe(key, semitone) is called for each key, to explain it on a help strip.
export function makeKeybed({ onNote, describe = () => {} }) {
  const el = document.createElement('div');
  el.className = 'keybed';
  el.setAttribute('role', 'group');
  el.setAttribute('aria-label', 'Keys');
  const keys = [];
  let whiteIndex = 0;
  for (let semi = 0; semi <= 24; semi++) {
    const black = BLACK.has(semi % 12);
    const key = document.createElement('button');
    key.type = 'button';
    key.className = black ? 'key black' : 'key white';
    key.tabIndex = -1;   // played from the computer keyboard, not focused one by one
    key.textContent = LABELS[semi] ?? '';
    key.setAttribute('aria-label', `Key ${semi}, computer key ${LABELS[semi] ?? 'none'}`);
    if (black) key.style.left = `calc(${whiteIndex} * var(--white-key) - var(--black-key) / 2)`;
    else whiteIndex++;
    let down = false;
    key.addEventListener('pointerdown', (e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      key.setPointerCapture(e.pointerId);
      down = true;
      light(semi, true);
      onNote(semi, true);
    });
    const up = () => {
      if (!down) return;
      down = false;
      light(semi, false);
      onNote(semi, false);
    };
    key.addEventListener('pointerup', up);
    key.addEventListener('pointercancel', up);
    keys[semi] = key;
    describe(key, semi, LABELS[semi]);
    el.append(key);
  }
  el.style.setProperty('--whites', String(whiteIndex));

  function light(semi, on) {
    keys[semi]?.classList.toggle('down', on);
  }
  return { el, light };
}
