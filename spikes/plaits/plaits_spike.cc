// Spike (PLAN.md Phase 0, step 2): does Plaits build and run fast enough for the browser?
// Renders 10 s of a 16th-note riff on each of Plaits' 24 engines, times it and writes a WAV
// per engine. Built natively and as WebAssembly (run under node) by spikes/build.sh.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include "plaits/dsp/voice.h"

using namespace plaits;

static const char* kNames[24] = {
  "va_vcf", "phase_dist", "six_op_1", "six_op_2", "six_op_3", "wave_terrain", "string_machine",
  "chiptune", "va", "waveshaping", "fm", "grain", "additive", "wavetable", "chords", "speech",
  "swarm", "noise", "particle", "string", "modal", "bass_drum", "snare_drum", "hi_hat"};

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

int main(int argc, char** argv) {
  const char* out_dir = argc > 1 ? argv[1] : nullptr;
  const int rate = 48000, seconds = 10, block = kBlockSize;
  const int total_blocks = rate * seconds / block;
  const int step_blocks = rate / 8 / block;  // 16ths at 120 BPM
  static const float riff[16] = {45, 45, 57, 45, 48, 45, 52, 45, 43, 43, 55, 43, 47, 43, 50, 52};

  static char ram[16384];
  double total_us = 0;
  printf("engine          us/block  %%core(1 voice)\n");
  for (int e = 0; e < 24; ++e) {
    stmlib::BufferAllocator allocator(ram, sizeof(ram));
    Voice* voice = new Voice;
    voice->Init(&allocator);

    Patch patch = {};
    patch.engine = e;
    patch.harmonics = 0.5f; patch.timbre = 0.5f; patch.morph = 0.5f;
    patch.decay = 0.5f; patch.lpg_colour = 0.5f;
    Modulations mod = {};
    mod.trigger_patched = true;
    mod.level_patched = true;
    mod.level = 0.8f;

    std::vector<short> wav;
    wav.reserve(rate * seconds);
    Voice::Frame frames[kMaxBlockSize];
    auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < total_blocks; ++b) {
      const int step = b / step_blocks;
      patch.note = riff[step % 16] + 12.0f;
      mod.trigger = (b % step_blocks) < 2 ? 1.0f : 0.0f;  // 0.5 ms trigger pulse
      patch.timbre = 0.3f + 0.4f * (float)(b % (step_blocks * 16)) / (step_blocks * 16);
      voice->Render(patch, mod, frames, block);
      for (int i = 0; i < block; ++i) wav.push_back(frames[i].out);
    }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    const double per_block = us / total_blocks;
    const double block_us = 1e6 * block / rate;
    printf("%-14s %8.2f  %6.2f\n", kNames[e], per_block, 100.0 * per_block / block_us);
    total_us += us;
    if (out_dir) {
      char path[512];
      snprintf(path, sizeof(path), "%s/plaits_%02d_%s.wav", out_dir, e, kNames[e]);
      write_wav(path, wav, rate);
    }
    delete voice;
  }
  printf("all 24 engines: %.1f ms for %d s of audio each\n", total_us / 1000, seconds);
  return 0;
}
