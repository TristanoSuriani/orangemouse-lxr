// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The parameter registry's UI side (web/app/registry/): every value of every
// parameter of every engine formats to readable text that fits a display, and
// a few values match the engines' own formulas. Run by tests/run.sh:
//   node tests/registry_test.mjs

import lxr from '../web/app/registry/lxr.js';
import va from '../web/app/registry/va.js';
import { formatValue, formatShort, helpLine } from '../web/app/registry/format.js';

const VALUE_MAX = 12;   // the value field of a display row
let failures = 0, checked = 0;
const fail = (msg) => { failures++; if (failures <= 20) console.log('FAIL ' + msg); };

for (const reg of [lxr, va]) {
  reg.params.forEach((p, i) => {
    for (let v = p.min; v <= p.max; v++) {
      const text = formatValue(p, v);
      checked++;
      if (typeof text !== 'string' || !text || /NaN|Infinity|undefined/.test(text))
        fail(`${reg.engine} ${p.id} = ${v}: "${text}"`);
      else if (text.length > VALUE_MAX)
        fail(`${reg.engine} ${p.id} = ${v}: "${text}" is longer than ${VALUE_MAX} characters`);
      const short = formatShort(p, v);
      if (!short || short.length > 5 || /NaN|Infinity|undefined/.test(short))
        fail(`${reg.engine} ${p.id} = ${v}: short form "${short}"`);
    }
    if (!helpLine(p, p.default).startsWith(p.short.toUpperCase()))
      fail(`${reg.engine} ${p.id}: help line "${helpLine(p, p.default)}"`);
  });
}

// spot checks against the formulas in VaEngine.c (va_blockSetup)
const vaParam = (id) => va.params.find((p) => p.id === id);
const expect = (id, v, want) => {
  const got = formatValue(vaParam(id), v);
  if (got !== want) fail(`va ${id} = ${v}: "${got}", expected "${want}"`);
};
expect('VA_P_CUTOFF', 127, '20.5 kHz');   // 20 Hz * 2^10
expect('VA_P_CUTOFF', 0, '20.0 Hz');
expect('VA_P_SEMI2', 70, '+7 st');
expect('VA_P_OCT', 3, '0 oct');
expect('VA_P_OCT', 5, '+2 oct');
expect('VA_P_UNISON', 6, '7 voices');
expect('VA_P_GATE', 15, '1 step');
expect('VA_P_GATE', 3, '1/4 step');
expect('VA_P_AREL', 0, '0.5 ms');         // 0.5 ms * 2^0
expect('VA_P_FENV_AMT', 126, '+8.0 oct');
expect('VA_P_LDELAY', 0, 'off');
expect('VA_P_FTYPE', 0, 'LP24 ladder');

if (failures) {
  console.log(`${failures} registry check(s) failed`);
  process.exit(1);
}
console.log(`registry: ${checked} values of ${lxr.params.length + va.params.length} parameters format and fit`);
