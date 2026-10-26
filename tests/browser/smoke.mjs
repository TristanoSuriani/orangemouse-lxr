// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// The studio in a real browser (PLAN.md Testing, tier 2): headless Chrome,
// driven over the DevTools protocol with node's own WebSocket (no packages).
// It serves web/app, switches the studio on, and checks that:
//   - nothing throws or logs an error, and the worklet keeps running;
//   - a VA key makes sound, and the LXR plays too;
//   - the VA panel follows its pads (page, preset) on its display;
//   - the help strip explains the control under the pointer;
//   - the ? pad opens and closes the device's guide card;
//   - in a 1280x800 window, every device fits without scrolling (one is
//     expanded at a time; the others are cards in the top bar);
//   - the panel lint passes for every device: each control has a name and
//     help, none overlap or stick out of their panel, no pad label is cut;
//   - each device looks as in tests/golden/screens/ (UPDATE_GOLDEN=1 accepts
//     the new look).
// Writes screenshots to tests/browser/out/ (<device>.png, guide.png).
//
//   node tests/browser/smoke.mjs        (needs Google Chrome and web/app/studio.wasm)

import { spawn } from 'node:child_process';
import { mkdirSync, mkdtempSync, writeFileSync, readFileSync, existsSync, copyFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { extname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateSync } from 'node:zlib';

const ROOT = fileURLToPath(new URL('../..', import.meta.url));
const APP = join(ROOT, 'web/app');
const OUT = join(ROOT, 'tests/browser/out');
const GOLDEN = join(ROOT, 'tests/golden/screens');
const CHROME = process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.wasm': 'application/wasm' };

let failures = 0;
const check = (ok, msg) => { console.log(`   ${ok ? 'ok' : 'FAIL'}: ${msg}`); if (!ok) failures++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// The panel lint, run in the page on one device's panel.
const LINT = `((device) => {
  const problems = [];
  const name = (el) => el.getAttribute('aria-label') || el.textContent.trim() || (el.labels && el.labels[0] && el.labels[0].textContent.trim());
  const controls = [...device.querySelectorAll('button, [role=slider], input, select')].filter((el) => el.offsetParent !== null);
  for (const el of controls) {
    const what = name(el) || el.className;
    if (!name(el)) problems.push('no accessible name: ' + el.outerHTML.slice(0, 60));
    if (!el.closest('[data-described]') && !el.querySelector('[data-described]')) problems.push('no help: ' + what);
  }
  const box = device.getBoundingClientRect();
  const solid = controls.filter((el) => !el.classList.contains('key'));   // keys overlap by design
  for (const el of solid) {
    const r = el.getBoundingClientRect();
    if (r.left < box.left - 1 || r.right > box.right + 1 || r.top < box.top - 1 || r.bottom > box.bottom + 1)
      problems.push('outside its panel: ' + name(el));
  }
  for (let i = 0; i < solid.length; i++) for (let j = i + 1; j < solid.length; j++) {
    const a = solid[i].getBoundingClientRect(), b = solid[j].getBoundingClientRect();
    const w = Math.min(a.right, b.right) - Math.max(a.left, b.left), h = Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top);
    if (w > 1 && h > 1 && !solid[i].contains(solid[j]) && !solid[j].contains(solid[i])) problems.push('overlap: ' + name(solid[i]) + ' / ' + name(solid[j]));
  }
  for (const pad of device.querySelectorAll('.pad')) {
    for (const t of pad.querySelectorAll('.label, .alt')) if (t.scrollWidth > pad.clientWidth + 1) problems.push('label cut: ' + t.textContent);
  }
  return { controls: controls.length, problems };
})`;

// ---- a static server for web/app ----
const server = createServer((req, res) => {
  const path = join(APP, decodeURIComponent(new URL(req.url, 'http://x').pathname).replace(/\/$/, '/index.html'));
  if (!path.startsWith(APP) || !existsSync(path)) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': TYPES[extname(path)] || 'application/octet-stream' });
  res.end(readFileSync(path));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const url = `http://127.0.0.1:${server.address().port}/index.html`;

// ---- Chrome, and a DevTools session on one page ----
const profile = mkdtempSync(join(tmpdir(), 'om-chrome-'));
const chrome = spawn(CHROME, ['--headless=new', '--remote-debugging-port=0', `--user-data-dir=${profile}`,
  '--autoplay-policy=no-user-gesture-required', '--window-size=1280,800', '--hide-scrollbars', 'about:blank'],
  { stdio: ['ignore', 'ignore', 'pipe'] });
const wsUrl = await new Promise((resolve, reject) => {
  let err = '';
  chrome.stderr.on('data', (d) => {
    err += d;
    const m = err.match(/DevTools listening on (ws:\/\/\S+)/);
    if (m) resolve(m[1]);
  });
  setTimeout(() => reject(new Error('Chrome did not start')), 15000);
});
const port = new URL(wsUrl).port;
const target = await (await fetch(`http://127.0.0.1:${port}/json/new?${encodeURIComponent(url)}`, { method: 'PUT' })).json();
const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((r) => ws.addEventListener('open', r));
let nextId = 1;
const pending = new Map();
const errors = [];
ws.addEventListener('message', (e) => {
  const msg = JSON.parse(e.data);
  if (msg.id && pending.has(msg.id)) { pending.get(msg.id)(msg); pending.delete(msg.id); }
  if (msg.method === 'Runtime.exceptionThrown') errors.push(msg.params.exceptionDetails.exception?.description || msg.params.exceptionDetails.text);
  if (msg.method === 'Runtime.consoleAPICalled' && msg.params.type === 'error') errors.push(msg.params.args.map((a) => a.value ?? a.description).join(' '));
  if (msg.method === 'Log.entryAdded' && msg.params.entry.level === 'error') errors.push(msg.params.entry.text);
});
// Two screenshots compared pixel by pixel: how many pixels differ and by how
// much at most. A few pixels of anti-aliasing on a rounded corner vary from
// run to run; a real change to a panel touches many more.
function pngPixels(buf) {
  let i = 8, width = 0, height = 0, type = 0;
  const idat = [];
  while (i < buf.length) {
    const n = buf.readUInt32BE(i), tag = buf.toString('latin1', i + 4, i + 8);
    if (tag === 'IHDR') { width = buf.readUInt32BE(i + 8); height = buf.readUInt32BE(i + 12); type = buf[i + 17]; }
    if (tag === 'IDAT') idat.push(buf.subarray(i + 8, i + 8 + n));
    i += 12 + n;
  }
  const raw = inflateSync(Buffer.concat(idat)), bpp = type === 6 ? 4 : 3, stride = width * bpp;
  const out = Buffer.alloc(stride * height);
  for (let y = 0, p = 0; y < height; y++, p += stride + 1) {
    const f = raw[p], line = out.subarray(y * stride, (y + 1) * stride), prev = y ? out.subarray((y - 1) * stride, y * stride) : null;
    for (let x = 0; x < stride; x++) {
      const a = x >= bpp ? line[x - bpp] : 0, b = prev ? prev[x] : 0, c = prev && x >= bpp ? prev[x - bpp] : 0;
      const pa = Math.abs(b - c), pb = Math.abs(a - c), pc = Math.abs(a + b - 2 * c);
      const pred = [0, a, b, (a + b) >> 1, pa <= pb && pa <= pc ? a : pb <= pc ? b : c][f];
      line[x] = (raw[p + 1 + x] + pred) & 255;
    }
  }
  return { width, height, bpp, data: out };
}
function pngDiff(a, b) {
  const A = pngPixels(a), B = pngPixels(b);
  if (A.width !== B.width || A.height !== B.height) return { size: false };
  let count = 0, worst = 0;
  for (let i = 0; i < A.width * A.height; i++) {
    let d = 0;
    for (let c = 0; c < 3; c++) d = Math.max(d, Math.abs(A.data[i * A.bpp + c] - B.data[i * B.bpp + c]));
    if (d) { count++; worst = Math.max(worst, d); }
  }
  return { size: true, count, worst };
}

const cdp = (method, params = {}) => new Promise((resolve, reject) => {
  const id = nextId++;
  pending.set(id, (msg) => (msg.error ? reject(new Error(`${method}: ${msg.error.message}`)) : resolve(msg.result)));
  ws.send(JSON.stringify({ id, method, params }));
});
const evaluate = async (expression) => {
  const r = await cdp('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
  if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text);
  return r.result.value;
};
const key = async (code, keyName, down, modifiers = 0) => cdp('Input.dispatchKeyEvent', { type: down ? 'keyDown' : 'keyUp', code, key: keyName, modifiers });
// a key press and release; modifiers: 1 Alt, 2 Ctrl, 4 Meta, 8 Shift
const press = async (code, keyName, modifiers = 0) => { await key(code, keyName, true, modifiers); await key(code, keyName, false, modifiers); await sleep(30); };
const trackerRow = async (r) => (await evaluate(`document.querySelector('.tracker-lcd').getAttribute('aria-label')`)).split('\n')[2 + r];
const vaDisplay = () => evaluate(`document.querySelector('.va-lcd').getAttribute('aria-label')`);
// the strip's rows joined with spaces, as one line of text
const helpText = async () => (await evaluate(`document.getElementById('help').getAttribute('aria-label')`)).replace(/\s+/g, ' ');
const clickPad = (selector) => evaluate(`(() => { const el = document.querySelector(${JSON.stringify(selector)});
  el.dispatchEvent(new PointerEvent('pointerdown', { button: 0, bubbles: true, pointerId: 1 }));
  el.dispatchEvent(new PointerEvent('pointerup', { button: 0, bubbles: true, pointerId: 1 })); })()`);
const peak = (ms) => evaluate(`(async () => {
  const { context, node } = window.orangeMouse;
  const a = context.createAnalyser(); a.fftSize = 2048; node.connect(a);
  const buf = new Float32Array(a.fftSize); let p = 0;
  const end = performance.now() + ${ms};
  while (performance.now() < end) { a.getFloatTimeDomainData(buf); for (const v of buf) p = Math.max(p, Math.abs(v)); await new Promise((r) => setTimeout(r, 20)); }
  node.disconnect(a); return p; })()`);

try {
  await cdp('Runtime.enable');
  await cdp('Log.enable');
  await cdp('Page.enable');
  await sleep(1500);   // the page and its modules load

  console.log('== switch on');
  await evaluate(`document.getElementById('powerButton').click()`);
  for (let i = 0; i < 50 && !(await evaluate('!!window.orangeMouse')); i++) await sleep(200);
  check(await evaluate('!!window.orangeMouse') && errors.length === 0, `the studio started${errors.length ? ` (errors: ${errors.slice(0, 3).join(' / ')})` : ''}`);
  check((await evaluate(`document.getElementById('help').getAttribute('aria-label')`)).includes('SD card'), 'the SD card is ready (the help strip says so)');
  await sleep(800);
  check((await vaDisplay()).includes('#01'), `the VA display shows a preset: "${(await vaDisplay()).replace(/\n/g, ' | ')}"`);

  console.log('== one device at a time, each fits 1280x800');
  for (const id of await evaluate('window.orangeMouseUi.devices')) {
    await evaluate(`window.orangeMouseUi.showDevice(window.orangeMouseUi.devices.indexOf(${JSON.stringify(id)}))`);
    await sleep(200);
    const size = await evaluate(`({ w: document.documentElement.scrollWidth, h: document.documentElement.scrollHeight,
      vw: innerWidth, vh: innerHeight, visible: [...document.querySelectorAll('.device')].filter((d) => !d.hidden).length })`);
    check(size.visible === 1, `${id}: one device expanded (${size.visible})`);
    check(size.w <= size.vw && size.h <= size.vh, `${id}: fits without scrolling (${size.w}x${size.h} in ${size.vw}x${size.vh})`);
    const lint = await evaluate(`${LINT}([...document.querySelectorAll('.device')].find((d) => !d.hidden))`);
    check(lint.problems.length === 0, `${id}: panel lint over ${lint.controls} controls${lint.problems.length ? ': ' + lint.problems.slice(0, 4).join('; ') : ''}`);
    mkdirSync(OUT, { recursive: true });
    await evaluate(`document.activeElement && document.activeElement.blur()`);
    await cdp('Input.dispatchMouseEvent', { type: 'mouseMoved', x: 1, y: 799 });   // no hover in the picture
    await sleep(100);
    const png = Buffer.from((await cdp('Page.captureScreenshot', { format: 'png' })).data, 'base64');
    writeFileSync(join(OUT, `${id}.png`), png);
    const golden = join(GOLDEN, `${id}.png`);
    if (process.env.UPDATE_GOLDEN) {
      mkdirSync(GOLDEN, { recursive: true });
      copyFileSync(join(OUT, `${id}.png`), golden);
      console.log(`   updated tests/golden/screens/${id}.png`);
    } else {
      const diff = existsSync(golden) ? pngDiff(readFileSync(golden), png) : null;
      check(diff && diff.size && diff.count <= 16 && diff.worst <= 8,
        `${id}: looks as in tests/golden/screens/${id}.png${diff ? (diff.size ? ` (${diff.count} pixels differ, by up to ${diff.worst})` : ' (another size)') : ' (no golden yet)'} (new: tests/browser/out/${id}.png)`);
    }
  }
  await evaluate(`window.orangeMouseUi.showDevice(window.orangeMouseUi.devices.indexOf('va'))`);

  console.log('== VA plays');
  const silent = await peak(300);
  await key('KeyZ', 'z', true);
  const played = await peak(500);
  await key('KeyZ', 'z', false);
  check(played > 0.02 && played > silent * 4, `key Z makes sound (peak ${played.toFixed(3)}, before ${silent.toFixed(3)})`);

  console.log('== the VA panel follows its pads');
  await clickPad('.va-pages .pad:nth-child(3)');   // FLT
  check((await vaDisplay()).includes('FILTER') && (await vaDisplay()).includes('CUT'), 'FLT shows the filter page with CUT');
  const before = await vaDisplay();
  await clickPad('.va-nav .pad:nth-child(2)');     // PRE >
  await sleep(300);
  const after = await vaDisplay();
  check(after.includes('#02') && after !== before, `PRE > loads preset 2: "${after.split('\n')[3]}"`);
  await evaluate(`document.querySelector('.va-knobs .pot').dispatchEvent(new PointerEvent('pointerenter'))`);
  check((await helpText()).includes('CUT Cutoff'), `the help strip explains knob 1: "${(await helpText()).replace(/\n/g, ' ')}"`);

  console.log('== the ? guide');
  await clickPad('.va .pad.guide');
  check(await evaluate(`!document.querySelector('.va .guide-card').hidden`), 'the ? pad opens the guide card');
  mkdirSync(OUT, { recursive: true });
  writeFileSync(join(OUT, 'guide.png'), Buffer.from((await cdp('Page.captureScreenshot', { format: 'png' })).data, 'base64'));
  await clickPad('.va .pad.guide');
  check(await evaluate(`document.querySelector('.va .guide-card').hidden`), 'pressing ? again closes it');

  console.log('== the LXR explained');
  await evaluate(`window.orangeMouseUi.showDevice(window.orangeMouseUi.devices.indexOf('lxr'))`);
  await evaluate(`document.querySelector('#pots .pot').dispatchEvent(new PointerEvent('pointerenter'))`);
  const lxrHelp = await helpText();
  check(lxrHelp.includes('Pot 1') && lxrHelp.includes('Coarse tune') && lxrHelp.includes(':'),
    `the help strip explains the LXR's pot 1: "${lxrHelp.replace(/\n/g, ' ')}"`);
  await evaluate(`document.querySelector('#pots .pot').dispatchEvent(new PointerEvent('pointerleave'))`);
  // the registry formats the pots' values as the LXR's own display does
  const lcdValues = (await evaluate(`document.getElementById('lcd').getAttribute('aria-label')`))
    .split('\n')[1].trim().split(/\s+/);
  const ours = await evaluate(`(async () => {
    const reg = (await import('./registry/lxr.js')).default;
    const { formatShort } = await import('./registry/format.js');
    const pots = window.orangeMouseUi.lxrPots();
    return pots.params.map((p, i) => (reg.params[p] ? formatShort(reg.params[p], pots.values[i]).trim() : '?'));
  })()`);
  check(JSON.stringify(lcdValues) === JSON.stringify(ours), `the registry shows the pots' values as the LCD does: LCD ${lcdValues.join(' ')}, registry ${ours.join(' ')}`);

  console.log('== the tracker');
  await evaluate(`window.orangeMouseUi.showDevice(window.orangeMouseUi.devices.indexOf('tracker'))`);
  await press('KeyZ', 'z');
  check((await trackerRow(0)).startsWith('00') && (await trackerRow(0)).includes('C-4'), `Z writes C-4 on row 00: "${await trackerRow(0)}"`);
  await press('ArrowUp', 'ArrowUp');
  await press('KeyC', 'C', 8);
  check((await trackerRow(0)).includes('C-4²'), `Shift+C stacks a chord: "${await trackerRow(0)}"`);
  await press('KeyZ', 'z', 4);
  check((await trackerRow(0)).includes('C-4 ') && !(await trackerRow(0)).includes('²'), `Cmd+Z undoes the chord: "${await trackerRow(0)}"`);
  await press('KeyC', 'c', 4);
  await press('ArrowDown', 'ArrowDown');
  await press('ArrowDown', 'ArrowDown');
  await press('ArrowDown', 'ArrowDown');
  await press('ArrowDown', 'ArrowDown');
  await press('KeyV', 'v', 4);
  check((await trackerRow(4)).includes('C-4'), `Cmd+C, Cmd+V repeat the cell on row 04: "${await trackerRow(4)}"`);
  await press('Enter', 'Enter');
  const loud = await peak(1200);
  check(loud > 0.02, `Enter loops the pattern and the drum plays (peak ${loud.toFixed(3)})`);
  check((await evaluate(`document.querySelector('.tracker-lcd').getAttribute('aria-label')`)).includes('▶'), 'the display shows it playing');
  await press('Enter', 'Enter');
  await sleep(200);
  check((await evaluate(`document.querySelector('.tracker-lcd').getAttribute('aria-label')`)).includes('STOPPED'), 'Enter again stops');

  console.log('== the song library');
  const trackerText = () => evaluate(`document.querySelector('.tracker-lcd').getAttribute('aria-label')`);
  await sleep(1000);   // the demo song saves itself
  await clickPad('.tracker-pads .pad:last-child');   // SONGS
  await sleep(300);
  check((await trackerText()).toUpperCase().includes('FIRST STEPS'), 'SONGS lists the demo song');
  const listed = await trackerText();
  check(['TRANCE', 'ELECTRO', 'SYNTHWAVE', 'DUB TECHNO'].every((g) => listed.toUpperCase().includes(g)), 'and the four genre songs');
  const songCount = Number(/(\d+) SONGS/.exec(listed)?.[1]);
  await press('KeyN', 'n');
  await sleep(400);
  check((await trackerText()).includes(`${songCount + 1} SONGS`), `N makes another song (${songCount} before)`);
  await press('Escape', 'Escape');
  await press('KeyZ', 'z');
  check((await trackerRow(0)).includes('C-4'), `the new song takes a note: "${await trackerRow(0)}"`);
  // the instruments' sounds are the song's too: a VA knob, and the LXR's kit (drum 2's coarse tune, as its panel changes it)
  // VA1's cutoff (VA_P_CUTOFF, 16) as its knob sets it: the panel's values, then the message
  await evaluate(`(() => { const params = [...window.orangeMouse.tracker.song().va[0].params]; params[16] = 33;
    window.orangeMouse.va.onMessage({ type: 'vaParams', slot: 0, params });
    window.orangeMouse.tracker.send({ type: 'param', engine: 1, part: 0, param: 16, value: 33 }); })()`);
  await evaluate(`window.orangeMouse.node.port.postMessage({ type: 'param', engine: 0, part: 0, param: 10, value: 77 })`);
  await sleep(1200);   // the LXR reports its kit, and the song saves
  const kit3 = await evaluate(`JSON.stringify(window.orangeMouse.tracker.song().lxr?.slice(8, 12))`);
  check(JSON.parse(kit3 ?? 'null')?.[2] === 77, `the song keeps the LXR's kit: ${kit3}`);
  await sleep(1200);   // autosave
  const code = await evaluate(`(async () => {
    const lib = await import('./tracker/library.js');
    const s = window.orangeMouse.tracker.song();
    const back = await lib.fromCode(await lib.toCode(s));
    const old = lib.migrate({ patterns: [{ rows: 16, cells: { '0:0': { notes: [60] } } }] });
    return { same: JSON.stringify(back.patterns) === JSON.stringify(s.patterns), old: old.tracks.length === 16 && old.order.length === 1 && old.patterns[0].rows === 16 };
  })()`);
  check(code.same, 'a share code gives back the same song');
  check(code.old, 'an old song without tracks or order loads, completed');
  await cdp('Page.reload');
  await sleep(2000);
  check((await evaluate(`document.querySelector('.rack-lcd').getAttribute('aria-label')`)).includes(`SONG ${songCount + 1}`), 'after a reload the studio opens the last song');
  check((await trackerRow(0)).includes('C-4'), 'and its note was saved');
  await evaluate(`document.getElementById('powerButton').click()`);
  for (let i = 0; i < 50 && !(await evaluate('!!window.orangeMouse')); i++) await sleep(200);
  await sleep(1500);   // the LXR reports its kit again: the song's, if loading the song set it
  check((await evaluate(`window.orangeMouse.tracker.song().lxr?.[10]`)) === 77, 'after a reload the LXR plays the song\'s kit');
  check((await evaluate(`window.orangeMouse.tracker.song().va[0].params?.[16]`)) === 33, 'and VA1 its sound (VA reports its cutoff back)');

  console.log('== the LXR plays');
  await evaluate(`window.orangeMouse.node.port.postMessage({ type: 'note', engine: 0, part: 0, note: 36, velocity: 120, on: true })`);
  check((await peak(300)) > 0.02, 'an LXR drum 1 hit makes sound');

  console.log('== the mixer');
  await evaluate(`window.orangeMouseUi.showDevice(window.orangeMouseUi.devices.indexOf('mixer'))`);
  const mixText = () => evaluate(`document.querySelector('.mixer-lcd').getAttribute('aria-label')`);
  const mixRow = async (r) => (await mixText()).replace(/^Display: /, '').split('\n')[r] ?? '';
  const mixOf = (path) => evaluate(`window.orangeMouse.tracker.song().mix.${path}`);
  check((await mixRow(1)).includes('DR1') && (await mixRow(1)).includes('OUT'), `the strips are named: "${await mixRow(1)}"`);
  // the demo song, so every strip has something to show
  await evaluate(`(async () => { const { demoSong } = await import('./tracker/demo.js'); window.orangeMouse.tracker.load(demoSong()); })()`);
  await evaluate(`window.orangeMouse.node.port.postMessage({ type: 'tracker', op: 'play', mode: 'pattern', from: 1 })`);
  let moved = false;   // the song's one kick, on row 0: the meters rise, then fall back
  for (let i = 0; i < 20 && !moved; i++, await sleep(50)) moved = /[▁▂▃▄▅▆▇█]/.test((await mixText()).split('\n').slice(2, 10).join(''));
  check(moved, 'the meters move while the song plays');
  const shot = async (name) => writeFileSync(join(OUT, name), Buffer.from((await cdp('Page.captureScreenshot', { format: 'png' })).data, 'base64'));
  await shot('mixer-playing.png');   // to look at, not compared
  await evaluate(`window.orangeMouse.node.port.postMessage({ type: 'tracker', op: 'stop' })`);
  for (let i = 0; i < 8; i++) await press('ArrowRight', 'ArrowRight');
  check((await mixRow(0)).startsWith('VA2 POLY'), `the arrows choose a strip: "${await mixRow(0)}"`);
  await evaluate(`document.querySelectorAll('.mixer-knobs .pot')[2].focus()`);
  for (let i = 0; i < 5; i++) await press('ArrowUp', 'ArrowUp');
  check((await mixOf('channels.va2.sendA')) === 20 && (await mixRow(0)).includes('A -'), `knob 3 turns send A up: ${await mixOf('channels.va2.sendA')}, "${await mixRow(0)}"`);
  await evaluate(`document.activeElement.blur()`);
  await press('KeyM', 'm');
  check((await mixOf('channels.va2.mute')) === 1 && (await mixRow(0)).includes('MUTED'), 'M mutes the strip');
  await press('KeyM', 'm');
  // the channel's Surge filter: PAGE, then FLT (Shift+Up: one step) and CUT
  await clickPad('.mixer-pads .pad:nth-child(9)');   // PAGE
  check((await evaluate(`[...document.querySelectorAll('.mixer-knobs .caption')].map((c) => c.textContent).join(' ')`)) === 'FLT CUT RES -', 'PAGE puts the filter on the knobs');
  await evaluate(`document.querySelectorAll('.mixer-knobs .pot')[0].focus()`);
  await press('ArrowUp', 'ArrowUp', 8);
  await press('ArrowUp', 'ArrowUp', 8);
  await evaluate(`document.querySelectorAll('.mixer-knobs .pot')[1].focus()`);
  for (let i = 0; i < 10; i++) await press('ArrowDown', 'ArrowDown');
  check((await mixOf('channels.va2.filter')) === 2 && (await mixOf('channels.va2.cutoff')) === 87 && (await mixRow(0)).includes('LP24'),
    `the knobs set a 24 dB low-pass and lower its cutoff: "${await mixRow(0)}"`);
  check((await mixText()).split('\n')[14].includes('l24'), 'the strip shows its filter');
  await evaluate(`document.activeElement.blur()`);
  await clickPad('.mixer-pads .pad:nth-child(9)');   // PAGE: back to the mix
  await clickPad('.mixer-pads .pad:nth-child(5)');   // FX A
  check((await mixRow(0)).startsWith('SEND A  HALL'), `FX A shows the hall: "${await mixRow(0)}"`);
  await shot('mixer-fx.png');
  await clickPad('.mixer-pads .pad:nth-child(8)');   // TYPE +
  check((await mixOf('buses[0].type')) === 2 && (await mixRow(0)).startsWith('SEND A  ROOM'), 'TYPE + makes it a room');
  await clickPad('.mixer-pads .pad:nth-child(7)');   // TYPE -
  await sleep(1200);   // autosave
  const savedMix = await evaluate(`(async () => {
    const lib = await import('./tracker/library.js');
    return (await lib.load(lib.currentId())).mix; })()`);
  check(savedMix.channels.va2.sendA === 20 && savedMix.buses[0].type === 1 && savedMix.channels.va2.filter === 2, 'the mix saves with the song, its filters too');

  console.log('== Dexed');
  await evaluate(`window.orangeMouseUi.showDevice(window.orangeMouseUi.devices.indexOf('dexed'))`);
  const dxText = async () => (await evaluate(`document.querySelector('.dexed-lcd').getAttribute('aria-label')`)).replace(/^Display: /, '');
  check((await dxText()).includes('SYNPREZFM 01') && (await dxText()).includes('PIANO'), `the bank's sounds are listed: "${(await dxText()).split('\n')[2]}"`);
  await evaluate(`(() => { const c = document.querySelector('.dexed-lcd'), r = c.getBoundingClientRect();
    const at = { clientX: r.left + r.width * (12 / 44) + 4, clientY: r.top + r.height * (3.5 / 10) };
    c.dispatchEvent(new PointerEvent('pointerdown', { ...at, button: 0, bubbles: true })); })()`);
  check((await evaluate(`window.orangeMouse.tracker.song().dexed[0].program`)) === 9, 'clicking a name chooses that sound (column 2, row 2: #10)');
  await press('ArrowRight', 'ArrowRight');
  check((await dxText()).startsWith('DX1 SYNPREZFM 02'), `the right arrow changes the bank: "${(await dxText()).split('\n')[0]}"`);
  const quiet = await peak(150);
  await key('KeyZ', 'z', true);
  const dxPeak = await peak(400);
  await key('KeyZ', 'z', false);
  check(dxPeak > 0.01 && dxPeak > quiet, `key Z plays DX1 (peak ${dxPeak.toFixed(3)}, before ${quiet.toFixed(3)})`);
  await sleep(1200);   // autosave
  const savedDx = await evaluate(`(async () => {
    const lib = await import('./tracker/library.js');
    return (await lib.load(lib.currentId())).dexed; })()`);
  check(savedDx[0].bank === 2 && savedDx[0].program === 9, 'Dexed\'s choice saves with the song');

  console.log('== Plaits');
  await evaluate(`window.orangeMouseUi.showDevice(window.orangeMouseUi.devices.indexOf('plaits'))`);
  const plText = async () => (await evaluate(`document.querySelector('.plaits-lcd').getAttribute('aria-label')`)).replace(/^Display: /, '');
  const plRows = async () => (await plText()).split('\n');
  check((await plRows())[1].startsWith('TRANCE') && (await plText()).includes('Dub chord'), `the presets are listed by genre: "${(await plRows())[1]}"`);
  await evaluate(`(() => { const c = document.querySelector('.plaits-lcd'), r = c.getBoundingClientRect();
    const at = { clientX: r.left + r.width * (34 / 44), clientY: r.top + r.height * (2.5 / 12) };
    c.dispatchEvent(new PointerEvent('pointerdown', { ...at, button: 0, bubbles: true })); })()`);
  check((await plRows())[0].includes('DUB CHORD') && (await plRows())[0].endsWith('CHORDS'), `clicking a preset loads it: "${(await plRows())[0]}"`);
  check((await plRows())[9].includes('HRM chord'), `the knobs say what they do on this engine: "${(await plRows())[9]}"`);
  await clickPad('.plaits-nav .pad:first-child');   // PAGE
  check((await plRows())[9].startsWith('ATK'), 'PAGE puts the envelope on the knobs');
  const plQuiet = await peak(150);
  await key('KeyZ', 'z', true);
  const plPeak = await peak(400);
  await key('KeyZ', 'z', false);
  check(plPeak > 0.01 && plPeak > plQuiet, `key Z plays PL1 (peak ${plPeak.toFixed(3)})`);
  await sleep(1200);   // autosave
  const savedPl = await evaluate(`(async () => {
    const lib = await import('./tracker/library.js');
    return (await lib.load(lib.currentId())).plaits; })()`);
  check(savedPl[0].p[0] === 6, 'Plaits\' settings save with the song');

  console.log('== a genre song plays');
  await evaluate(`(async () => {
    const lib = await import('./tracker/library.js');
    window.orangeMouse.tracker.load(await lib.load('genre-dubtechno'));
  })()`);
  await evaluate(`window.orangeMouse.node.port.postMessage({ type: 'tracker', op: 'play', mode: 'song', from: 0 })`);
  const genrePeak = await peak(1500);
  await evaluate(`window.orangeMouse.node.port.postMessage({ type: 'tracker', op: 'stop' })`);
  check(genrePeak > 0.05 && (await evaluate(`window.orangeMouse.tracker.song().name`)).startsWith('Dub Techno'),
    `the dub techno song loads from the library and plays (peak ${genrePeak.toFixed(3)})`);

  console.log('== health');
  await sleep(300);
  check(errors.length === 0, errors.length ? `errors: ${errors.slice(0, 3).join(' / ')}` : 'no errors or exceptions');

} catch (e) {
  check(false, e.message);
} finally {
  ws.close();
  chrome.kill();
  server.close();
}
console.log(failures ? `${failures} browser check(s) failed` : 'browser: all checks passed');
process.exit(failures ? 1 : 0);
