// Spike (PLAN.md Phase 0, step 2): does Dexed's FM engine build and run fast enough for the browser?
// Loads a DX7 cartridge, plays 4-note chords and a 16th-note line on 16 voices for 10 s per voice
// program, times it and writes WAVs. Built natively and as WebAssembly by spikes/build.sh.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "msfa/controllers.h"
#include "msfa/dx7note.h"
#include "msfa/env.h"
#include "msfa/exp2.h"
#include "msfa/fm_core.h"
#include "msfa/freqlut.h"
#include "msfa/lfo.h"
#include "msfa/pitchenv.h"
#include "msfa/porta.h"
#include "msfa/sin.h"
#include "msfa/synth.h"
#include "msfa/tuning.h"
#include "engine/EngineMkI.h"

static void write_wav(const char* path, const std::vector<short>& s, int rate) {
  FILE* f = fopen(path, "wb");
  if (!f) return;
  const unsigned data = s.size() * 2, riff = 36 + data, fmt = 16, br = rate * 2;
  const unsigned short pcm = 1, ch = 1, ba = 2, bits = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
  fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(s.data(), 2, s.size(), f);
  fclose(f);
}

// A 128-byte packed voice from a 32-voice bulk dump into Dexed's 156-byte form
// (after Dexed's Cartridge::unpackProgram).
static void unpack(const uint8_t* bulk, uint8_t* p) {
  for (int op = 0; op < 6; op++) {
    const uint8_t* b = bulk + op * 17;
    uint8_t* o = p + op * 21;
    for (int i = 0; i < 11; i++) o[i] = b[i] & 0x7F;
    o[11] = b[11] & 3; o[12] = (b[11] >> 2) & 3;
    o[13] = b[12] & 7; o[20] = (b[12] >> 3) & 0x0F;
    o[14] = b[13] & 3; o[15] = (b[13] >> 2) & 7;
    o[16] = b[14] & 0x7F;
    o[17] = b[15] & 1; o[18] = (b[15] >> 1) & 0x1F;
    o[19] = b[16] & 0x7F;
  }
  for (int i = 0; i < 8; i++) p[126 + i] = bulk[102 + i] & 0x7F;
  p[134] = bulk[110] & 0x1F;
  p[135] = bulk[111] & 7; p[136] = (bulk[111] >> 3) & 1;
  for (int i = 0; i < 4; i++) p[137 + i] = bulk[112 + i] & 0x7F;
  p[141] = bulk[116] & 1; p[142] = (bulk[116] >> 1) & 7; p[143] = (bulk[116] >> 4) & 7;
  p[144] = bulk[117] & 0x7F;
  for (int i = 0; i < 10; i++) p[145 + i] = bulk[118 + i] & 0x7F;
  p[155] = 0x3F;  // all six operators on
}

struct Voice { std::unique_ptr<Dx7Note> note; int midi = -1; bool live = false; };

int main(int argc, char** argv) {
  const char* out_dir = argc > 1 ? argv[1] : nullptr;
  const int rate = 48000, seconds = 10, voices_n = 16;
  Exp2::init(); Tanh::init(); Sin::init();
  Freqlut::init(rate); Lfo::init(rate); PitchEnv::init(rate); Env::init_sr(rate); Porta::init_sr(rate);

  EngineMkI engine;
  Controllers ctrls;
  ctrls.core = &engine;
  ctrls.masterTune = 0; ctrls.pitch_range_up = 3; ctrls.pitch_range_dn = 3; ctrls.pitch_step = 0;
  ctrls.pitch_cc = 0x2000; ctrls.modwheel_cc = 0; ctrls.foot_cc = 0; ctrls.breath_cc = 0;
  ctrls.aftertouch_cc = 0; ctrls.portamento_enable_cc = false; ctrls.portamento_cc = 0;
  ctrls.refresh();
  auto tuning = createStandardTuning();

  const char* carts[] = {"spikes/dexed/carts/SynprezFM_01.syx", "spikes/dexed/carts/SynprezFM_03.syx"};
  // chords (stabs, every half bar) and a 16th-note line, at 120 BPM
  static const int chords[4][4] = {{57, 60, 64, 67}, {53, 57, 60, 64}, {48, 52, 55, 59}, {55, 59, 62, 65}};
  static const int line[16] = {45, 57, 52, 57, 45, 57, 52, 60, 41, 53, 48, 53, 43, 55, 50, 59};
  printf("program                us/64 samples  %%core(16 voices, ~8 sounding)\n");
  for (int c = 0; c < 2; c++) {
    std::vector<uint8_t> syx(4104);
    FILE* f = fopen(carts[c], "rb");
    if (!f || fread(syx.data(), 1, syx.size(), f) != syx.size()) { printf("cannot read %s\n", carts[c]); return 1; }
    fclose(f);
    for (int prog = 0; prog < 32; prog += 8) {
      uint8_t patch[156];
      unpack(&syx[6 + prog * 128], patch);
      char name[11]; for (int i = 0; i < 10; i++) name[i] = (char)patch[145 + i]; name[10] = 0;

      Voice voices[voices_n];
      for (auto& v : voices) v.note.reset(new Dx7Note(tuning, nullptr));
      Lfo lfo; lfo.reset(patch + 137);
      int next = 0;
      auto note_on = [&](int midi, int vel) {
        Voice& v = voices[next]; next = (next + 1) % voices_n;
        v.note->init(patch, midi, vel, 1, &ctrls); v.midi = midi; v.live = true; lfo.keydown();
      };
      auto note_off = [&](int midi) { for (auto& v : voices) if (v.live && v.midi == midi) { v.note->keyup(); v.midi = -1; } };

      std::vector<short> wav; wav.reserve(rate * seconds);
      const int blocks = rate * seconds / N, step_blocks = rate / 8 / N;
      double us = 0;
      for (int b = 0; b < blocks; b++) {
        if (b % step_blocks == 0) {
          const int step = b / step_blocks;
          if (step % 8 == 0) { for (int n : chords[(step / 8 + 3) % 4]) note_off(n); for (int n : chords[(step / 8) % 4]) note_on(n, 90); }
          if (step % 8 == 3) for (int n : chords[(step / 8) % 4]) note_off(n);
          note_off(line[(step + 15) % 16] - 12); note_on(line[step % 16] - 12, 100);
        }
        auto t0 = std::chrono::steady_clock::now();
        AlignedBuf<int32_t, N> buf; float sum[N] = {};
        const int32_t lv = lfo.getsample(), ld = lfo.getdelay();
        for (auto& v : voices) {
          if (!v.live) continue;
          for (int j = 0; j < N; j++) buf.get()[j] = 0;
          v.note->compute(buf.get(), lv, ld, &ctrls);
          for (int j = 0; j < N; j++) {
            int32_t val = buf.get()[j] >> 4;
            int clip = val < -(1 << 24) ? -0x8000 : val >= (1 << 24) ? 0x7fff : val >> 9;
            sum[j] += clip / 32768.f;
          }
          if (v.midi < 0 && !v.note->isPlaying()) v.live = false;
        }
        us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        for (int j = 0; j < N; j++) { float s = sum[j] * 0.25f; s = s > 1 ? 1 : s < -1 ? -1 : s; wav.push_back((short)(s * 32767)); }
      }
      const double per = us / blocks, block_us = 1e6 * N / rate;
      printf("%-12s %d/%02d   %8.2f       %6.2f\n", name, c, prog, per, 100 * per / block_us);
      if (out_dir) {
        char path[512]; snprintf(path, sizeof(path), "%s/dexed_%d_%02d.wav", out_dir, c, prog);
        write_wav(path, wav, rate);
      }
    }
  }
  return 0;
}
