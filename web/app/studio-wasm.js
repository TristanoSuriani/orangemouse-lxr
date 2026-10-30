// Copyright (C) 2026 Erica Synths
// SPDX-License-Identifier: GPL-3.0-only
//
// What every user of studio.wasm (web/port/web_studio.c) shares: its imports,
// and the page's messages as calls into it. The worklet plays the studio
// live; the render worker (render-worker.js) renders a song offline.

// The imports. host: { memory() (the instance's memory), log(text),
// sdWritten(first, count), midiOut(byte) }.
export function studioImports(host) {
  return {
    env: {
      web_log: (ptr) => host.log(readCString(host.memory(), ptr)),
      web_sd_written: (first, count) => host.sdWritten(first, count),
      web_midi_out: (byte) => host.midiOut(byte),
      emscripten_notify_memory_growth: () => {},
    },
    // the C++ runtime of the send effects links stdio and the clock, which a
    // standalone module imports; output goes to the log, the clock is 0
    wasi_snapshot_preview1: {
      fd_write: (fd, iovs, count, written) => {
        const buffer = host.memory().buffer;
        const view = new DataView(buffer);
        let total = 0, text = '';
        for (let i = 0; i < count; i++) {
          const ptr = view.getUint32(iovs + i * 8, true), len = view.getUint32(iovs + i * 8 + 4, true);
          text += String.fromCharCode(...new Uint8Array(buffer, ptr, len));
          total += len;
        }
        if (text.trim()) host.log(text.trim());
        view.setUint32(written, total, true);
        return 0;
      },
      fd_close: () => 0,
      fd_seek: () => 70,   // ESPIPE
      // an empty environment (getenv, linked by the channel filters)
      environ_sizes_get: (count, size) => {
        const view = new DataView(host.memory().buffer);
        view.setUint32(count, 0, true);
        view.setUint32(size, 0, true);
        return 0;
      },
      environ_get: () => 0,
      clock_time_get: (id, precision, out) => {
        new DataView(host.memory().buffer).setBigUint64(out, 0n, true);
        return 0;
      },
    },
  };
}

export function readCString(memory, ptr) {
  const mem = new Uint8Array(memory.buffer);
  let end = ptr;
  while (mem[end]) end++;
  return String.fromCharCode(...mem.subarray(ptr, end));
}

// A message from the page (web/app/tracker/song.js, mixer/mix.js) as a call
// into the studio: notes, parameters, the tracker, the mixer, the effects.
// Returns false for any other message.
export function applyMessage(w, msg) {
  switch (msg.type) {
    case 'note':
      if (msg.on) w.om_web_note_on(msg.engine, msg.part, msg.note, msg.velocity);
      else w.om_web_note_off(msg.engine, msg.part, msg.note);
      return true;
    case 'param': w.om_web_set_param(msg.engine, msg.part, msg.param, msg.value); return true;
    case 'mix': w.om_web_mix(msg.target, msg.param, msg.value); return true;
    case 'fx': w.om_web_fx(msg.bus, msg.param, msg.value); return true;
    case 'tracker':
      switch (msg.op) {
        case 'clear': w.om_web_tracker_clear(); break;
        case 'tempo': w.om_web_tracker_tempo(msg.bpm); break;
        case 'pattern':
          w.om_web_tracker_length(msg.p, msg.rows);
          w.om_web_tracker_pattern_tempo(msg.p, msg.bpm);
          w.om_web_tracker_pattern_swing(msg.p, msg.swing ?? 50);
          break;
        case 'cell': w.om_web_tracker_cell(msg.p, msg.row, msg.track, ...msg.notes, msg.vel, msg.retrig, msg.fx ?? 0, msg.fxv ?? 0, msg.len ?? 0, msg.glide ?? 0); break;
        case 'order': w.om_web_tracker_order(msg.position, msg.p); break;
        case 'route': w.om_web_tracker_route(msg.track, msg.engine, msg.part); break;
        case 'mute': w.om_web_tracker_mute(msg.track, msg.on ? 1 : 0); break;
        case 'solo': w.om_web_tracker_solo(msg.track, msg.on ? 1 : 0); break;
        case 'play': if (msg.mode === 'song') w.om_web_tracker_play_song(msg.from); else w.om_web_tracker_play_pattern(msg.from); break;
        case 'stop': w.om_web_tracker_stop(); break;
      }
      return true;
  }
  return false;
}
