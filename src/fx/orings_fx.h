// oRings FX - the Rings module as an audio effect, around the original DSP.
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
//
// The DSP is the same vendored Mutable Instruments Rings code oRings plays
// (src/rings/mi/, scripts/fetch_rings.sh), driven the way the firmware's
// rings.cc FillBuffer() drives it when a cable is in the IN jack: the input
// is noise-gated exactly as rings.cc gates it, the Strummer runs its onset
// detector on it, then Part (or, for Disastrous Peace, StringSynthPart)
// processes it -- one 24-frame block at a time, at 48 kHz. What stands in
// for the hardware AROUND that:
//
//   * the IN jack is Move's audio, summed to mono and resampled 44.1 -> 48 kHz
//     (the codec's clipping at full scale kept as a clamp);
//   * V/OCT is unpatched (internal_note): FREQUENCY alone sets the pitch, as
//     a note name, and FINE stands in for the FM input;
//   * the STRUM jack is unpatched in AUTO (internal_strum: the onset detector
//     strums), patched by MIDI note-ons in MIDI, and patched-but-silent in
//     OFF; the internal exciter is a switch (OFF = what the hardware does with
//     IN patched);
//   * the ODD / EVEN jacks become the wet stereo pair (48 -> 44.1 kHz), which
//     is mixed with the untouched, latency-matched dry signal.
#ifndef ORINGS_FX_ORINGS_FX_H_
#define ORINGS_FX_ORINGS_FX_H_

#include <stddef.h>
#include <stdint.h>

#include "common/rings_models.h"
#include "fx/upsampler.h"
#include "orings_fx_meta.h"
#include "rings/dsp/part.h"
#include "rings/dsp/string_synth_part.h"
#include "rings/dsp/strummer.h"
#include "rings/resampler.h"

namespace oringsfx {

const size_t kBlock = rings::kMaxBlockSize;  // 24 frames, as rings.cc
const size_t kReverbWords = 32768;           // rings.cc reverb_buffer
const int kFadeBlocks = 16;                  // 4 ms out, switch, 4 ms in
const int kMidiQueue = 16;                   // MIDI strums waiting

// The 48 kHz input waits in a FIFO until a whole Rings block is there. It
// starts this full (of silence), which is what makes the round trip a
// constant latency with no underrun (tests/test_fx.cpp runs an hour of it).
const int kFifoPrefill = 29;
const int kFifoSize = 128;  // power of two, > prefill + a block + slack

// The round trip IN -> 48 kHz -> Rings block -> 44.1 kHz, in 44.1 kHz
// samples, measured with Part's own bypass (tests/test_fx.cpp). The dry
// signal is delayed by the same amount so the two line up in the MIX.
const int kDryDelay = 72;
const int kDryRing = 128;  // power of two > kDryDelay

static_assert(kNumModels == orings::kModelCount, "MODEL lists differ");

class RingsFx {
 public:
  void Init();

  bool SetParam(int id, float value);
  float GetParam(int id) const { return values_[id]; }
  bool SetByKey(const char *key, const char *value);
  int GetByKey(const char *key, char *buf, int size) const;

  void Midi(const uint8_t *msg, int len);

  // In place, stereo interleaved, Move's 44.1 kHz.
  void Process(int16_t *io, int frames);
  // The same on floats (full scale = 1.0); `in` and `out` may alias.
  void ProcessFloat(const float *in_l, const float *in_r, float *out_l, float *out_r, int frames);

  // Apply MODEL / POLYPHONY at once and land every smoothed value on its
  // target (a state restore before the first block; the tests).
  void Snap();

  int WriteState(char *buf, int size) const;
  bool ReadState(const char *json);

  // One 48 kHz block of Rings: `in` is the IN jack (already at 48 kHz),
  // out / aux the ODD / EVEN jacks. Public for the tests.
  void RenderBlock(const float *in, float *out, float *aux);

  // Tests only: Part's bypass (IN copied to both outputs), to measure the
  // round trip.
  void set_bypass_for_test(bool on) { part_.set_bypass(on); }

  int model() const { return static_cast<int>(values_[P_MODEL]); }
  int chord() const { return chord_; }
  uint32_t strums() const { return strums_; }
  uint32_t recoveries() const { return recoveries_; }
  uint32_t underruns() const { return underruns_; }
  int polyphony() const { return kPolyVoices[static_cast<int>(values_[P_POLY])]; }

 private:
  void InitParts();
  void ApplyModelNow();
  void WetMatrix(float *m) const;

  float values_[P_COUNT];

  rings::Part part_;
  rings::StringSynthPart string_synth_;
  rings::Strummer strummer_;
  uint16_t reverb_part_[kReverbWords];
  uint16_t reverb_synth_[kReverbWords];

  Upsampler up_;
  orings::Resampler down_;
  float fifo_[kFifoSize];
  uint32_t fifo_read_, fifo_write_;
  float dry_l_[kDryRing], dry_r_[kDryRing];
  uint32_t dry_write_;

  // rings.cc's noise-gate follower (`in_level`).
  float in_level_;
  // The pots after cv_scaler.cc's low-pass; [4] is the FREQUENCY pot.
  float pot_[5];
  int chord_;
  float fm_cv_;

  int midi_strums_;  // MIDI note-ons not yet delivered as a strum

  int applied_model_;
  int applied_poly_;
  int fade_;
  float fade_gain_, fade_prev_;

  float mix_;        // smoothed per sample
  float wet_gain_;   // LEVEL, smoothed per block
  float matrix_[4];  // ROUTING x WIDTH, smoothed per block

  uint32_t strums_;
  uint32_t recoveries_;
  uint32_t underruns_;
};

}  // namespace oringsfx

#endif  // ORINGS_FX_ORINGS_FX_H_
