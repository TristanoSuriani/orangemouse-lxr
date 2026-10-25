// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The ? pad and its overlay card: like the printed cards laid over hardware,
// it explains how to work the device, grouped by task (PLAN.md, "A printed
// legend"). Press ? again, or Escape, to put it away.
//
//   addGuide(deviceEl, headEl, [{ title: 'Play', text: 'Keys Z-M ...' }, ...]);

import { makePad } from './controls.js';

export function addGuide(device, head, sections) {
  const card = document.createElement('div');
  card.className = 'guide-card';
  card.hidden = true;
  card.setAttribute('role', 'dialog');
  card.setAttribute('aria-label', 'How to use this device');
  card.innerHTML = sections.map((s) => `<section><h3>${s.title}</h3><p>${s.text}</p></section>`).join('');
  device.append(card);

  const toggle = (open = card.hidden) => {
    card.hidden = !open;
    pad.classList.toggle('lit', open);
    pad.setAttribute('aria-expanded', String(open));
  };
  const pad = makePad('?', { kind: 'guide', onChange: (down) => down && toggle() });
  pad.setAttribute('aria-label', 'How to use this device');
  pad.setAttribute('aria-expanded', 'false');
  head.append(pad);
  card.addEventListener('pointerdown', () => toggle(false));
  device.addEventListener('keydown', (e) => { if (e.key === 'Escape' && !card.hidden) toggle(false); });
  return { toggle };
}
