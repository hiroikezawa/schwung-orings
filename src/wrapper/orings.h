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
//
// Around that, a performance layer (the same one as oBraids / oPlaits):
// Rings is struck on every 1/16 of Move's transport whose trigger pad is on,
// at the note the PITCH knob names in the Scale page's root + scale; knob
// touches record motion while the REC pad is on; RANDOM writes a Phrase Gen
// phrase into PITCH. MIDI notes still strike it as before.
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
#include "stmlib/dsp/filter.h"
#include "wrapper/motion.h"
#include "wrapper/random_phrase.h"

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
const uint8_t kSeqVelocity = 100;   // an auto 1/16 strike

// A stored parameter's id from its key or one of its labels (-1: none).
int ParamIdOf(const char *key);

static_assert(kNumModels == kModelCount, "metadata.py MODELS and common/rings_models.h differ");

class ORings {
 public:
  void Init(uint32_t seed = 0x5A17C0DEu);

  // Stored parameters, in their own units (metadata.py): the BASE value, the
  // knob's. Effective() is what plays (motion playback overrides the base).
  bool SetParam(int id, float value);
  float GetParam(int id) const { return values_[id]; }
  float Effective(int id) const { return ovr_[id] == ovr_[id] ? ovr_[id] : values_[id]; }

  // Any key the module serves: stored, alias (label) or trigger. Value text
  // is a number, or an option name for enums. False = unknown key/value.
  // `effective`: the value playing rather than the knob's.
  bool SetByKey(const char *key, const char *value);
  int GetByKey(const char *key, char *buf, int size, bool effective = false) const;

  // Notes, pitch bend, CC 120/121/123, and MIDI clock (used only when the
  // host has no transport position).
  void Midi(const uint8_t *msg, int len);

  // Out: Move's 44.1 kHz, stereo. `beat` = the host transport position in
  // beats (< 0 stopped). The short form is a stopped transport.
  void Process(float *left, float *right, size_t frames, double beat, float bpm,
               bool host_has_transport);
  void Process(float *left, float *right, size_t frames) {
    Process(left, right, frames, -1.0, 120.0f, true);
  }

  // The pads and the knobs' touch sensors (ui_core.mjs).
  void SetWrite(bool on) { motion_.set_write(on); }
  bool write() const { return motion_.write(); }
  void ToggleGate(int step);
  uint16_t gates() const { return gates_; }
  void Randomize();
  void Touch(int param_id, bool down);
  void ClearMotion(int param_id);   // -1 = all
  int steps() const { return values_[P_STEPS] >= 0.5f ? 16 : 8; }
  unsigned motion_mask() const { return motion_.Mask(); }
  unsigned motion_recording() const { return motion_.Recording(); }
  int motion_step() const { return motion_.step(); }
  int PitchNote(float index) const;   // PITCH index -> MIDI note

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
  void ApplyModelNow();
  void InitParts();
  void Enqueue(uint8_t note, uint8_t velocity, bool seq = false);
  void Render(float *left, float *right, size_t frames);
  Motion::Params MotionParams() { return Motion::Params{values_, ovr_}; }
  void SetKey(int id, float value);
  void MotionAt(double pos16);
  void Hit(int64_t abs16);
  double FallbackBeat() const;
  float VelocityGain(uint8_t velocity) const;
  void OutputMatrix(float *m) const;

  float values_[P_COUNT];
  float ovr_[P_COUNT];  // motion playback, NAN = none

  rings::Part part_;
  rings::StringSynthPart string_synth_;
  rings::Strummer strummer_;
  uint16_t reverb_part_[kReverbWords];
  uint16_t reverb_synth_[kReverbWords];
  Resampler resampler_;

  // The pots after cv_scaler.cc's low-pass.
  float pot_[5];
  int chord_;  // the CHORD parameter, as the last block played it
  stmlib::Svf cutoff_[2];  // CUTOFF, Disastrous Peace only
  // CvScaler::Read's FM smoothing (fm_cv_), fed by bend + FINE.
  float fm_cv_;
  int bend_;  // -8192..8191

  // `seq`: an auto 1/16 strike, which waits only for the Strummer's inhibit
  // (SPREAD is for chords played from MIDI, and a 1/16 apart is far more).
  struct Pending { uint8_t note, velocity; bool seq; };
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

  Motion motion_;
  Clock clock_;
  RandomPhrase phrase_;
  uint16_t gates_;        // bit n = step n+1 of 16 strikes
  double last_pos16_;     // monotonic playhead (host beat jitter)
  uint32_t frames_;       // since create: entropy for RANDOM
  bool clk_running_, clk_awaiting_first_;
  long long clk_ticks_;
};

}  // namespace orings

#endif  // ORINGS_ORINGS_H_
