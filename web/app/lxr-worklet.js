// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// AudioWorklet that runs the LXR firmware (lxr.wasm). The firmware renders
// blocks of 64 frames; process() pulls as many as each render quantum needs.
// The worklet forwards front panel input to the firmware and sends display,
// LED, MIDI and SD card changes back to the page.

const SECTOR_SIZE = 512;
const UI_INTERVAL_S = 1 / 60;
// A button state lasts at least this many blocks (main loop passes), so the
// firmware's front panel scan sees a click even when its press and release
// arrive within one render quantum.
const MIN_BUTTON_BLOCKS = 4;
const SD_FLUSH_INTERVAL_S = 0.5;

class LxrProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const { wasm, sdImage, blankCard, seed } = options.processorOptions;

    this.dirtySectors = new Set();
    this.midiOut = [];
    this.logLines = [];

    const imports = {
      env: {
        web_log: (ptr) => this.logLines.push(this.readCString(ptr)),
        web_sd_written: (first, count) => {
          for (let i = 0; i < count; i++) this.dirtySectors.add(first + i);
        },
        web_midi_out: (byte) => this.midiOut.push(byte),
        emscripten_notify_memory_growth: () => {},
      },
    };
    this.wasm = new WebAssembly.Instance(new WebAssembly.Module(wasm), imports).exports;
    this.wasm._initialize();

    const sectors = sdImage.byteLength / SECTOR_SIZE;
    const sdPtr = this.wasm.web_sd_alloc(sectors);
    this.bytes().set(new Uint8Array(sdImage), sdPtr);
    this.wasm.web_boot(sampleRate, seed);
    this.dirtySectors.clear();

    this.blockSize = this.wasm.web_block_size();
    this.pending = new Float32Array(this.blockSize * 2);  // interleaved stereo left over from the last block
    this.pendingFrames = 0;
    this.pendingRead = 0;

    this.buttonQueue = [];         // pending { id, down }; id 'encoder' = encoder push
    this.buttonChanged = new Map(); // id -> block of the last change
    this.blockCount = 0;

    this.lastLcdVersion = -1;
    this.lastLeds = [-1, -1];
    this.uiTimer = 0;
    this.sdTimer = 0;

    this.port.onmessage = (e) => this.onMessage(e.data);
    // a blank card has just been formatted by the firmware: store all of it
    if (blankCard) this.flushSd(true);
    this.sendUi(true);
  }

  bytes() {
    return new Uint8Array(this.wasm.memory.buffer);
  }

  readCString(ptr) {
    const mem = this.bytes();
    let end = ptr;
    while (mem[end]) end++;
    return String.fromCharCode(...mem.subarray(ptr, end));
  }

  onMessage(msg) {
    const w = this.wasm;
    switch (msg.type) {
      case 'button': this.buttonQueue.push({ id: msg.id, down: msg.down }); break;
      case 'encoderButton': this.buttonQueue.push({ id: 'encoder', down: msg.down }); break;
      case 'encoder': w.web_encoder(msg.detents); break;
      case 'pot': w.web_pot_turn(msg.pot, msg.degrees); break;
      case 'fader': w.web_set_fader(msg.fader, msg.value); break;
      case 'midi': for (const b of msg.bytes) w.web_midi_in(b); break;
      case 'flushSd': this.flushSd(false); break;
    }
  }

  // Sends the sectors written since the last flush (or the whole card).
  flushSd(all) {
    const w = this.wasm;
    const image = this.bytes().subarray(w.web_sd_image(), w.web_sd_image() + w.web_sd_sector_count() * SECTOR_SIZE);
    if (all) {
      this.port.postMessage({ type: 'sdAll', data: image.slice() });
      this.dirtySectors.clear();
      return;
    }
    if (this.dirtySectors.size === 0) return;
    const sectors = Uint32Array.from(this.dirtySectors).sort();
    const data = new Uint8Array(sectors.length * SECTOR_SIZE);
    sectors.forEach((s, i) => data.set(image.subarray(s * SECTOR_SIZE, (s + 1) * SECTOR_SIZE), i * SECTOR_SIZE));
    this.dirtySectors.clear();
    this.port.postMessage({ type: 'sd', sectors, data }, [sectors.buffer, data.buffer]);
  }

  sendUi(force) {
    const w = this.wasm;
    const lcdVersion = w.web_lcd_version();
    const leds = [w.web_leds_low(), w.web_leds_high()];
    const msg = { type: 'ui' };
    if (force || lcdVersion !== this.lastLcdVersion) {
      this.lastLcdVersion = lcdVersion;
      const mem = this.bytes();
      msg.lcd = mem.slice(w.web_lcd_text(), w.web_lcd_text() + 32);
      msg.cgram = mem.slice(w.web_lcd_cgram(), w.web_lcd_cgram() + 64);
      msg.lcdOn = lcdVersion !== 0;
    }
    if (force || leds[0] !== this.lastLeds[0] || leds[1] !== this.lastLeds[1]) {
      this.lastLeds = leds;
      msg.leds = leds;
    }
    if (this.midiOut.length) {
      msg.midi = this.midiOut;
      this.midiOut = [];
    }
    if (this.logLines.length) {
      msg.log = this.logLines;
      this.logLines = [];
    }
    if (Object.keys(msg).length > 1) this.port.postMessage(msg);
  }

  applyButtons() {
    const blocked = new Set();
    this.buttonQueue = this.buttonQueue.filter(({ id, down }) => {
      const last = this.buttonChanged.get(id);
      if (blocked.has(id) || (last !== undefined && this.blockCount - last < MIN_BUTTON_BLOCKS)) {
        blocked.add(id);  // keep this and later events of the same button in order
        return true;
      }
      if (id === 'encoder') this.wasm.web_encoder_button(down ? 1 : 0);
      else this.wasm.web_set_button(id, down ? 1 : 0);
      this.buttonChanged.set(id, this.blockCount);
      return false;
    });
  }

  renderBlock() {
    const w = this.wasm;
    if (this.buttonQueue.length) this.applyButtons();
    this.blockCount++;
    const ptr = w.web_render();
    const n = this.blockSize;
    const out = new Float32Array(w.memory.buffer, ptr, n * 4);  // out1 L, out1 R, out2 L, out2 R
    for (let i = 0; i < n; i++) {
      // both stereo outputs mixed down
      this.pending[i * 2] = out[i] + out[2 * n + i];
      this.pending[i * 2 + 1] = out[n + i] + out[3 * n + i];
    }
    this.pendingFrames = n;
    this.pendingRead = 0;
  }

  process(inputs, outputs) {
    const output = outputs[0];
    const left = output[0];
    const right = output[1] || output[0];
    const frames = left.length;

    let written = 0;
    while (written < frames) {
      if (this.pendingRead >= this.pendingFrames) this.renderBlock();
      const count = Math.min(frames - written, this.pendingFrames - this.pendingRead);
      for (let i = 0; i < count; i++) {
        const j = (this.pendingRead + i) * 2;
        left[written + i] = this.pending[j];
        right[written + i] = this.pending[j + 1];
      }
      written += count;
      this.pendingRead += count;
    }

    this.uiTimer += frames / sampleRate;
    this.sdTimer += frames / sampleRate;
    if (this.uiTimer >= UI_INTERVAL_S) {
      this.uiTimer = 0;
      this.sendUi(false);
    }
    if (this.sdTimer >= SD_FLUSH_INTERVAL_S) {
      this.sdTimer = 0;
      this.flushSd(false);
    }
    return true;
  }
}

registerProcessor('lxr', LxrProcessor);
