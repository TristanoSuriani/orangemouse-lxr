// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// How a parameter's value reads on a display and in the help line, from the
// "display" entry of the parameter registry (registry/<engine>.json):
//
//   formatValue(param, v)   "1.2 kHz", "+7 st", "saw", "1/4 step" ...
//   helpLine(param, v)      "CUT Cutoff 1.2 kHz: The filter's frequency: lower is darker."

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

function fixed(x, digits) {
  const s = x.toFixed(digits);
  return s === '-0' || /^-0\.0*$/.test(s) ? s.slice(1) : s;
}

function signed(x, digits) {
  const s = fixed(x, digits);
  return x > 0 && Number(s) !== 0 ? '+' + s : s;
}

// a physical quantity with a readable prefix: 1.2 kHz, 85 Hz, 14 ms, 2.1 s
function quantity(x, unit) {
  if (unit === 'Hz') {
    if (x >= 1000) return fixed(x / 1000, x >= 10000 ? 1 : 2) + ' kHz';
    return fixed(x, x >= 100 ? 0 : x >= 10 ? 1 : 2) + ' Hz';
  }
  if (unit === 's') {
    if (x < 1) return fixed(x * 1000, x >= 0.01 ? 0 : 1) + ' ms';
    return fixed(x, x >= 10 ? 1 : 2) + ' s';
  }
  return fixed(x, 2) + (unit ? ' ' + unit : '');
}

function withUnit(text, unit) {
  return unit ? `${text} ${unit}` : text;
}

export function formatValue(param, v) {
  const d = param.display;
  switch (d.kind) {
    case 'raw': return String(v);
    case 'percent': return `${Math.round(((v - param.min) / (param.max - param.min)) * 100)} %`;
    case 'bipolar': return withUnit(signed((v - d.center) * d.scale, d.digits ?? 0), d.unit);
    case 'exp': return quantity(d.base * 2 ** ((v * d.octaves) / 127), d.unit);
    case 'off_exp': return v === 0 ? 'off' : quantity(d.base * 2 ** ((v * d.octaves) / 127), d.unit);
    case 'enum': return d.values[v - param.min];
    case 'offset': {
      const x = v + d.offset;
      return withUnit(d.signed === false ? String(x) : signed(x, 0), d.unit);
    }
    case 'affine': return withUnit(fixed(d.a + d.b * v, d.digits ?? 0), d.unit);
    case 'curve': return withUnit(fixed(d.scale * (v / 127) ** d.power, d.digits ?? 1), d.unit);
    case 'steps': {
      const steps = (v + 1) / 16;   // 16 = one sequencer step
      if (steps < 1) return Number.isInteger(16 / (v + 1)) ? `1/${16 / (v + 1)} step` : `${fixed(steps, 2)} step`;
      return `${Number.isInteger(steps) ? steps : fixed(steps, 2)} step${steps === 1 ? '' : 's'}`;
    }
    case 'preset': return `#${v + 1}`;
    case 'note': return `${NOTE_NAMES[v % 12]}${Math.floor(v / 12) - 1}`;
    default: return String(v);
  }
}

// The value in at most 5 characters, for a display column: "1.2k", "850ms",
// "+7st", "saw", "1/4".
export function formatShort(param, v) {
  const d = param.display;
  const cut = (s) => s.slice(0, 5);
  const hz = (x) => (x >= 1000 ? `${fixed(x / 1000, x >= 9950 ? 0 : 1)}k`
    : x >= 100 ? String(Math.round(x)) : x >= 10 ? `${Math.round(x)}Hz` : x >= 1 ? `${fixed(x, 1)}Hz` : fixed(x, 2));
  const sec = (x) => (x < 1 ? `${Math.round(x * 1000)}ms` : `${fixed(x, x >= 9.95 ? 0 : 1)}s`);
  const physical = (x) => (d.unit === 'Hz' ? hz(x) : d.unit === 's' ? sec(x) : fixed(x, 1));
  const unit = { st: 'st', ct: 'c', '%': '%' }[d.unit] ?? '';
  switch (d.kind) {
    case 'percent': return `${Math.round(((v - param.min) / (param.max - param.min)) * 100)}%`;
    case 'bipolar': return cut(signed((v - d.center) * d.scale, d.digits ?? 0) + unit);
    case 'exp': return cut(physical(d.base * 2 ** ((v * d.octaves) / 127)));
    case 'off_exp': return v === 0 ? 'off' : cut(physical(d.base * 2 ** ((v * d.octaves) / 127)));
    case 'enum': return cut((d.short ?? d.values)[v - param.min]);
    case 'offset': return cut(d.signed === false ? String(v + d.offset) : signed(v + d.offset, 0));
    case 'affine': return cut(fixed(d.a + d.b * v, d.digits ?? 0) + unit);
    case 'curve': return cut(fixed(d.scale * (v / 127) ** d.power, 1));
    case 'steps': {
      const steps = (v + 1) / 16;
      if (steps < 1 && Number.isInteger(16 / (v + 1))) return `1/${16 / (v + 1)}`;
      return cut(Number.isInteger(steps) ? String(steps) : fixed(steps, 2));
    }
    default: return cut(formatValue(param, v));
  }
}

export function helpLine(param, v) {
  const head = `${param.short.toUpperCase()} ${param.name} ${formatValue(param, v)}`;
  return param.help ? `${head}: ${param.help}` : head;
}
