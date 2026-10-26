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
// See om_plaits_voice.h: Plaits' voice.cc, with its engines trimmed to the
// genres', float output and no trigger delay.
// Changes (C) 2026 Erica Synths, under the same MIT terms.

#include "om_plaits_voice.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void OmPlaitsVoice::Init(BufferAllocator* allocator) {
  engines_.Init();
  // the order of OmPlaitsEngine; gains and envelope flags as in voice.cc
  engines_.RegisterInstance(&virtual_analog_vcf_engine_, false, 1.0f, 1.0f);
  engines_.RegisterInstance(&virtual_analog_engine_, false, 0.8f, 0.8f);
  engines_.RegisterInstance(&waveshaping_engine_, false, 0.7f, 0.6f);
  engines_.RegisterInstance(&fm_engine_, false, 0.6f, 0.6f);
  engines_.RegisterInstance(&phase_distortion_engine_, false, 0.7f, 0.7f);
  engines_.RegisterInstance(&wavetable_engine_, false, 0.6f, 0.6f);
  engines_.RegisterInstance(&chord_engine_, false, 0.8f, 0.8f);
  engines_.RegisterInstance(&string_machine_engine_, false, 0.8f, 0.8f);
  engines_.RegisterInstance(&speech_engine_, false, -0.7f, 0.8f);
  engines_.RegisterInstance(&modal_engine_, true, -1.0f, 0.8f);
  engines_.RegisterInstance(&particle_engine_, false, -2.0f, 1.0f);

  for (int i = 0; i < engines_.size(); ++i) {
    // All engines share the same RAM space.
    allocator->Free();
    engines_.get(i)->Init(allocator);
  }
  previous_engine_index_ = -1;
  out_post_processor_.Init();
  aux_post_processor_.Init();
  decay_envelope_.Init();
  lpg_envelope_.Init();
  trigger_state_ = false;
  previous_note_ = 0.0f;
}

void OmPlaitsVoice::Render(const Patch& patch, const Modulations& modulations, float* out, float* aux, size_t size) {
  const bool previous_trigger_state = trigger_state_;
  if (!previous_trigger_state) {
    if (modulations.trigger > 0.3f) {
      trigger_state_ = true;
      if (!modulations.level_patched) {
        lpg_envelope_.Trigger();
      }
      decay_envelope_.Trigger();
    }
  } else if (modulations.trigger < 0.1f) {
    trigger_state_ = false;
  }

  int engine_index = patch.engine;
  CONSTRAIN(engine_index, 0, OM_PL_ENGINES - 1);
  Engine* e = engines_.get(engine_index);
  if (engine_index != previous_engine_index_) {
    e->LoadUserData(NULL);
    e->Reset();
    out_post_processor_.Reset();
    previous_engine_index_ = engine_index;
  }

  EngineParameters p;
  const bool rising_edge = trigger_state_ && !previous_trigger_state;
  const float note = (modulations.note + previous_note_) * 0.5f;
  previous_note_ = modulations.note;
  const PostProcessingSettings& pp_s = e->post_processing_settings;

  if (modulations.trigger_patched) {
    p.trigger = (rising_edge ? TRIGGER_RISING_EDGE : TRIGGER_LOW) | (trigger_state_ ? TRIGGER_HIGH : TRIGGER_LOW);
  } else {
    p.trigger = TRIGGER_UNPATCHED;
  }

  const float short_decay = (200.0f * kBlockSize) / kSampleRate * SemitonesToRatio(-96.0f * patch.decay);
  decay_envelope_.Process(short_decay * 2.0f);

  float compressed_level = 1.3f * modulations.level / (0.3f + fabsf(modulations.level));
  CONSTRAIN(compressed_level, 0.0f, 1.0f);
  p.accent = modulations.level_patched ? compressed_level : 0.8f;

  const bool use_internal_envelope = modulations.trigger_patched;

  p.harmonics = patch.harmonics + modulations.harmonics;
  CONSTRAIN(p.harmonics, 0.0f, 1.0f);

  float internal_envelope_amplitude = 1.0f;
  if (engine_index == OM_PL_SPEECH) {
    internal_envelope_amplitude = 2.0f - p.harmonics * 6.0f;
    CONSTRAIN(internal_envelope_amplitude, 0.0f, 1.0f);
    speech_engine_.set_prosody_amount(
        !modulations.trigger_patched || modulations.frequency_patched ? 0.0f : patch.frequency_modulation_amount);
    speech_engine_.set_speed(
        !modulations.trigger_patched || modulations.morph_patched ? 0.0f : patch.morph_modulation_amount);
  }

  p.note = ApplyModulations(patch.note + note, patch.frequency_modulation_amount, modulations.frequency_patched,
                            modulations.frequency, use_internal_envelope,
                            internal_envelope_amplitude * decay_envelope_.value() * decay_envelope_.value() * 48.0f,
                            1.0f, -119.0f, 120.0f);
  p.timbre = ApplyModulations(patch.timbre, patch.timbre_modulation_amount, modulations.timbre_patched,
                              modulations.timbre, use_internal_envelope, decay_envelope_.value(), 0.0f, 0.0f, 1.0f);
  p.morph = ApplyModulations(patch.morph, patch.morph_modulation_amount, modulations.morph_patched,
                             modulations.morph, use_internal_envelope,
                             internal_envelope_amplitude * decay_envelope_.value(), 0.0f, 0.0f, 1.0f);

  bool already_enveloped = pp_s.already_enveloped;
  e->Render(p, out, aux, size, &already_enveloped);

  const bool lpg_bypass = already_enveloped || (!modulations.level_patched && !modulations.trigger_patched);
  if (!lpg_bypass) {
    const float hf = patch.lpg_colour;
    const float decay_tail = (20.0f * kBlockSize) / kSampleRate *
        SemitonesToRatio(-72.0f * patch.decay + 12.0f * hf) - short_decay;
    if (modulations.level_patched) {
      lpg_envelope_.ProcessLP(compressed_level, short_decay, decay_tail, hf);
    } else {
      const float attack = NoteToFrequency(p.note) * float(kBlockSize) * 2.0f;
      lpg_envelope_.ProcessPing(attack, short_decay, decay_tail, hf);
    }
  } else {
    lpg_envelope_.Init();
  }

  out_post_processor_.Process(pp_s.out_gain, lpg_bypass, lpg_envelope_.gain(), lpg_envelope_.frequency(),
                              lpg_envelope_.hf_bleed(), out, size);
  aux_post_processor_.Process(pp_s.aux_gain, lpg_bypass, lpg_envelope_.gain(), lpg_envelope_.frequency(),
                              lpg_envelope_.hf_bleed(), aux, size);
}

}  // namespace plaits
