// oRings - the Rings module around the original DSP.
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
//
// The DSP is Mutable Instruments' own Rings code (src/rings/mi/, vendored from
// pichenettes/eurorack by scripts/fetch_rings.sh, plus one patch that lets the
// strike strength follow velocity, see patches/). rings::Part, StringSynthPart
// and Strummer are called exactly as the firmware's rings.cc FillBuffer()
// calls them: one 24-frame block at a time, at 48 kHz, with PerformanceState
// and Patch filled in the hardware's own units. This class is what stands in
// for the hardware AROUND them:
//
//   * the pots become parameters (low-passed per block with cv_scaler.cc's
//     own pot coefficient, so a turn glides the way the hardware's does);
//   * the V/OCT input becomes the MIDI note, the STRUM input becomes the
//     note-on (both patched, as on hardware with cables in: internal_note and
//     internal_strum are false, and the IN jack is empty, so the internal
//     exciter is used and `in` is silence, exactly what the hardware's
//     normalization probe produces);
//   * the FM input becomes pitch bend + FINE;
//   * the mode buttons become MODEL and POLYPHONY; Disastrous Peace (the
//     easter egg) is a MODEL rather than a hidden gesture;
//   * the ODD / EVEN jacks become the stereo output, then a 48 -> 44.1 kHz
//     resampler.
#ifndef ORINGS_ORINGS_H_
#define ORINGS_ORINGS_H_

#include <stddef.h>
#include <stdint.h>

#include "common/rings_models.h"
#include "orings_meta.h"
#include "rings/dsp/part.h"
#include "rings/dsp/string_synth_part.h"
#include "rings/dsp/strummer.h"
#include "rings/resampler.h"

namespace orings {

const size_t kBlock = rings::kMaxBlockSize;  // 24 frames, as rings.cc
// rings.cc: uint16_t reverb_buffer[32768], shared by Part and StringSynthPart.
// Each gets its own here, so a switch between the two never plays the other's
// leftover reverb memory as a burst (on hardware they never both run).
const size_t kReverbWords = 32768;
// Strummer's inhibit (Init(0.01f, ...): 10 ms = 20 blocks) swallows a strum
// that arrives sooner; a note is delivered no earlier than this many blocks
// after the previous one, so none is ever swallowed (tests/test_engine.cpp).
const int kMinStrumBlocks = 21;
const int kQueue = 32;          // note-ons waiting for their strum
const int kFadeBlocks = 16;     // 4 ms out, switch, 4 ms in (2 blocks = 1 ms)

static_assert(kNumModels == kModelCount, "metadata.py MODELS and common/rings_models.h differ");

class ORings {
 public:
  void Init();

  // Stored parameters, in their own units (metadata.py).
  bool SetParam(int id, float value);
  float GetParam(int id) const { return values_[id]; }

  // Any key the module serves: stored, alias (label) or trigger. Value text
  // is a number, or an option name for enums. False = unknown key/value.
  bool SetByKey(const char *key, const char *value);
  int GetByKey(const char *key, char *buf, int size) const;

  void Midi(const uint8_t *msg, int len);

  // Out: Move's 44.1 kHz, stereo.
  void Process(float *left, float *right, size_t frames);

  // Silence everything, keep the settings (RESET, MIDI All Sound Off).
  void Reset();

  // Apply MODEL / POLYPHONY at once, without the fade (a state restore before
  // the first block; the tests).
  void ApplyNow() {
    ApplyModelNow();
    fade_ = 0;
    fade_gain_ = fade_prev_ = 1.0f;
  }

  int WriteState(char *buf, int size) const;
  bool ReadState(const char *json);

  // One 48 kHz block of Rings, before the output stage. Public for the tests.
  void RenderBlock(float *out, float *aux);

  // Introspection (tests, get_param).
  int chord() const { return chord_; }
  int model() const { return static_cast<int>(values_[P_MODEL]); }
  uint32_t strums() const { return strums_; }
  uint32_t swallowed() const { return swallowed_; }
  uint32_t recoveries() const { return recoveries_; }
  int pending() const { return queue_count_; }

 private:
  void ApplyStructure();
  void ApplyModelNow();
  void InitParts();
  void Enqueue(uint8_t note, uint8_t velocity);
  float VelocityGain(uint8_t velocity) const;
  void OutputMatrix(float *m) const;

  float values_[P_COUNT];

  rings::Part part_;
  rings::StringSynthPart string_synth_;
  rings::Strummer strummer_;
  uint16_t reverb_part_[kReverbWords];
  uint16_t reverb_synth_[kReverbWords];
  Resampler resampler_;

  // The pots after cv_scaler.cc's low-pass.
  float pot_[4];
  // CvScaler::Read's chord hysteresis (chord_), fed by STRUCTURE.
  int chord_;
  // CvScaler::Read's FM smoothing (fm_cv_), fed by bend + FINE.
  float fm_cv_;
  int bend_;  // -8192..8191

  struct Pending { uint8_t note, velocity; };
  Pending queue_[kQueue];
  int queue_head_;
  int queue_count_;
  int blocks_since_strum_;
  float note_;  // the V/OCT voltage, as a note (relative to tonic)

  // MODEL / POLYPHONY changes and RESET fade out, switch, fade back in.
  int applied_model_;
  int applied_poly_;
  bool reset_requested_;
  int fade_;        // >0 fading out (blocks left), <0 fading in
  float fade_gain_;   // after this block
  float fade_prev_;   // before it

  float out_gain_;     // LEVEL x GAIN, ramped per block
  float matrix_[4];    // OUTPUT: L = m0*odd + m1*even, R = m2*odd + m3*even

  uint32_t strums_;
  uint32_t swallowed_;
  uint32_t recoveries_;

  float in_[kBlock];
};

}  // namespace orings

#endif  // ORINGS_ORINGS_H_
