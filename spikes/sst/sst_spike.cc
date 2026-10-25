// Spike (PLAN.md Phase 0, step 2): do the Surge XT effects (sst-effects, SSE via SIMDE) build and run
// fast enough in the browser? Feeds 10 s of plucked chords through each effect at its defaults (wet),
// times it and writes stereo WAVs. Built natively and as WebAssembly by spikes/build.sh.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "sst/basic-blocks/simd/setup.h"
#include "sst/effects/ConcreteConfig.h"
#include "sst/effects/Delay.h"
#include "sst/effects/Flanger.h"
#include "sst/effects/Phaser.h"
#include "sst/effects/Reverb1.h"
#include "sst/effects/Reverb2.h"

namespace sfx = sst::effects;
using Cfg = sfx::core::ConcreteConfig;

static void write_wav(const char* path, const std::vector<short>& s, int rate) {
  FILE* f = fopen(path, "wb");
  if (!f) return;
  const unsigned data = s.size() * 2, riff = 36 + data, fmt = 16, br = rate * 4;
  const unsigned short pcm = 1, ch = 2, ba = 4, bits = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
  fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(s.data(), 2, s.size(), f);
  fclose(f);
}

static const char* g_out = nullptr;

template <typename FX> static void run(const char* name) {
  const int rate = 48000, seconds = 10, bs = Cfg::blockSize;
  auto gs = Cfg::GlobalStorage(rate);
  auto es = Cfg::EffectStorage();
  auto fx = std::make_unique<FX>(&gs, &es, nullptr);
  for (int i = 0; i < FX::numParams; ++i) fx->paramStorage[i] = fx->paramAt(i).defaultVal;
  fx->initialize();

  // Input: a plucked minor chord every half second, then 2 s of silence for the tail.
  std::vector<short> wav; wav.reserve(rate * seconds * 2);
  float L alignas(16)[bs], R alignas(16)[bs];
  float ph[3] = {0, 0, 0};
  const float hz[3] = {220.f, 261.63f, 329.63f};
  double us = 0; long n = 0;
  const int blocks = rate * seconds / bs;
  for (int b = 0; b < blocks; ++b) {
    for (int s = 0; s < bs; ++s, ++n) {
      const long t = n % (rate / 2);
      const float env = n < rate * 8 ? std::exp(-t / (0.08f * rate)) : 0.f;
      float x = 0;
      for (int v = 0; v < 3; ++v) { ph[v] += hz[v] / rate; if (ph[v] >= 1) ph[v] -= 1; x += 2 * ph[v] - 1; }
      L[s] = R[s] = 0.25f * env * x;
    }
    auto t0 = std::chrono::steady_clock::now();
    fx->processBlock(L, R);
    us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    for (int s = 0; s < bs; ++s) {
      auto c = [](float v) { v = v > 1 ? 1 : v < -1 ? -1 : v; return (short)(v * 32767); };
      wav.push_back(c(L[s])); wav.push_back(c(R[s]));
    }
  }
  const double per = us / blocks, block_us = 1e6 * bs / rate;
  printf("%-10s %6.2f us/16 samples  %6.2f %%core\n", name, per, 100 * per / block_us);
  if (g_out) {
    char path[512]; snprintf(path, sizeof(path), "%s/sst_%s.wav", g_out, name);
    write_wav(path, wav, rate);
  }
}

int main(int argc, char** argv) {
  g_out = argc > 1 ? argv[1] : nullptr;
  run<sfx::reverb2::Reverb2<Cfg>>("reverb2");
  run<sfx::reverb1::Reverb1<Cfg>>("reverb1");
  run<sfx::delay::Delay<Cfg>>("delay");
  run<sfx::phaser::Phaser<Cfg>>("phaser");
  run<sfx::flanger::Flanger<Cfg>>("flanger");
  return 0;
}
