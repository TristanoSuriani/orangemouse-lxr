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
//     expanded at a time; the others are cards in the top bar).
// Writes screenshots to tests/browser/out/ (<device>.png, guide.png).
//
//   node tests/browser/smoke.mjs        (needs Google Chrome and web/app/studio.wasm)

import { spawn } from 'node:child_process';
import { mkdirSync, mkdtempSync, writeFileSync, readFileSync, existsSync } from 'node:fs';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { extname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = fileURLToPath(new URL('../..', import.meta.url));
const APP = join(ROOT, 'web/app');
const OUT = join(ROOT, 'tests/browser/out');
const CHROME = process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.wasm': 'application/wasm' };

let failures = 0;
const check = (ok, msg) => { console.log(`   ${ok ? 'ok' : 'FAIL'}: ${msg}`); if (!ok) failures++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

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
const key = async (code, keyName, down) => cdp('Input.dispatchKeyEvent', { type: down ? 'keyDown' : 'keyUp', code, key: keyName });
const vaDisplay = () => evaluate(`document.querySelector('.va-lcd').getAttribute('aria-label')`);
const helpText = () => evaluate(`document.querySelector('.va .help-strip').getAttribute('aria-label')`);
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
  check(await evaluate('!!window.orangeMouse'), 'the studio started');
  check((await evaluate(`document.getElementById('status').textContent`)).includes('SD card'), 'the SD card is ready');
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
    mkdirSync(OUT, { recursive: true });
    writeFileSync(join(OUT, `${id}.png`), Buffer.from((await cdp('Page.captureScreenshot', { format: 'png' })).data, 'base64'));
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

  console.log('== the LXR plays');
  await evaluate(`window.orangeMouse.node.port.postMessage({ type: 'note', engine: 0, part: 0, note: 36, velocity: 120, on: true })`);
  check((await peak(300)) > 0.02, 'an LXR drum 1 hit makes sound');

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
