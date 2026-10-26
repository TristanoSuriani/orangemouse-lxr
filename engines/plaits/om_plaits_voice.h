// Copyright 2016 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Orange Mouse's copy of Plaits' voice (plaits/dsp/voice.h, voice.cc): the
// same trigger, envelope, low-pass gate and post-processing, changed in three
// ways for the studio (engines/plaits/plaits_engine.cpp):
//   - only the engines the genres use are registered (OmPlaitsEngine), which
//     saves the RAM and the start-up time of the others;
//   - the output stays float: Plaits' own voice clips to 16 bits, and several
//     engines reach full scale (spikes/README.md);
//   - no 1 ms trigger delay: the studio's gates and notes arrive together.
// Changes (C) 2026 Erica Synths, under the same MIT terms.

#ifndef OM_PLAITS_VOICE_H_
#define OM_PLAITS_VOICE_H_

#include "stmlib/stmlib.h"
#include "stmlib/dsp/limiter.h"
#include "stmlib/utils/buffer_allocator.h"

#include "plaits/dsp/engine/chord_engine.h"
#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/modal_engine.h"
#include "plaits/dsp/engine/particle_engine.h"
#include "plaits/dsp/engine/speech_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/wavetable_engine.h"
#include "plaits/dsp/engine2/phase_distortion_engine.h"
#include "plaits/dsp/engine2/string_machine_engine.h"
#include "plaits/dsp/engine2/virtual_analog_vcf_engine.h"
#include "plaits/dsp/envelope.h"
#include "plaits/dsp/fx/low_pass_gate.h"
#include "plaits/dsp/voice.h"   // Patch, Modulations

namespace plaits {

// the engines, in the order of registry/plaits.json PL_P_ENGINE
enum OmPlaitsEngine {
  OM_PL_VA_VCF, OM_PL_VA, OM_PL_WAVESHAPING, OM_PL_FM, OM_PL_PHASE_DISTORTION, OM_PL_WAVETABLE,
  OM_PL_CHORDS, OM_PL_STRING_MACHINE, OM_PL_SPEECH, OM_PL_MODAL, OM_PL_PARTICLE, OM_PL_ENGINES
};

class FloatPostProcessor {
 public:
  void Init() { lpg_.Init(); Reset(); }
  void Reset() { limiter_.Init(); }
  void Process(float gain, bool bypass_lpg, float lpg_gain, float lpg_frequency, float lpg_hf_bleed,
               float* in_out, size_t size) {
    if (gain < 0.0f) {
      limiter_.Process(-gain, in_out, size);
    }
    // Plaits inverts its output (-32767 to 16 bits); so do we, at full scale 1
    const float post_gain = (gain < 0.0f ? 1.0f : gain) * -1.0f;
    if (!bypass_lpg) {
      lpg_.Process(post_gain * lpg_gain, lpg_frequency, lpg_hf_bleed, in_out, size);
    } else {
      for (size_t i = 0; i < size; ++i) in_out[i] *= post_gain;
    }
  }
 private:
  stmlib::Limiter limiter_;
  LowPassGate lpg_;
};

class OmPlaitsVoice {
 public:
  void Init(stmlib::BufferAllocator* allocator);
  // out and aux: size frames (at most kMaxBlockSize), written
  void Render(const Patch& patch, const Modulations& modulations, float* out, float* aux, size_t size);

 private:
  inline float ApplyModulations(float base_value, float modulation_amount, bool use_external_modulation,
                                float external_modulation, bool use_internal_envelope, float envelope,
                                float default_internal_modulation, float minimum_value, float maximum_value) {
    float value = base_value;
    modulation_amount *= std::max(fabsf(modulation_amount) - 0.05f, 0.05f);
    modulation_amount *= 1.05f;
    float modulation = use_external_modulation
        ? external_modulation
        : (use_internal_envelope ? envelope : default_internal_modulation);
    value += modulation_amount * modulation;
    CONSTRAIN(value, minimum_value, maximum_value);
    return value;
  }

  VirtualAnalogVCFEngine virtual_analog_vcf_engine_;
  VirtualAnalogEngine virtual_analog_engine_;
  WaveshapingEngine waveshaping_engine_;
  FMEngine fm_engine_;
  PhaseDistortionEngine phase_distortion_engine_;
  WavetableEngine wavetable_engine_;
  ChordEngine chord_engine_;
  StringMachineEngine string_machine_engine_;
  SpeechEngine speech_engine_;
  ModalEngine modal_engine_;
  ParticleEngine particle_engine_;

  int previous_engine_index_;
  float previous_note_;
  bool trigger_state_;
  DecayEnvelope decay_envelope_;
  LPGEnvelope lpg_envelope_;
  FloatPostProcessor out_post_processor_;
  FloatPostProcessor aux_post_processor_;
  EngineRegistry<OM_PL_ENGINES> engines_;
};

}  // namespace plaits

#endif  // OM_PLAITS_VOICE_H_
