// oRings - 48 kHz -> 44.1 kHz stereo polyphase resampler for the Rings DSP.
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
//
// Rings renders at the hardware's own 48 kHz (rings/dsp/dsp.h kSampleRate;
// see scripts/gen_tables.py). 44100 / 48000 is exactly 147 / 160, so output
// sample n sits at input time n * 160 / 147 = q + r / 147. The 64-tap
// Kaiser-windowed sinc is tabulated at 256 fractional positions; a position
// between two rows is the linear interpolation of the two rows' outputs.
// Flat to ~20 kHz, -6 dB at 22.05 kHz, >= 70 dB down from ~23.7 kHz.
// Latency: 32 input samples (0.67 ms) plus at most one Rings block.
//
// Pull-driven: the caller asks NeedsInput() and pushes one Rings block
// (24 frames) at a time until the next output can be formed. The two
// channels share one clock, so ODD and EVEN stay sample-aligned.
#ifndef ORINGS_RESAMPLER_H_
#define ORINGS_RESAMPLER_H_

#include <stdint.h>
#include <string.h>

#include "orings_tables.h"

namespace orings {

class Resampler {
 public:
  static const uint32_t kRing = 256;  // > taps + one Rings block, power of 2

  void Reset() {
    memset(l_, 0, sizeof(l_));
    memset(r_, 0, sizeof(r_));
    // Pretend 2 * HALF zeros were written, so the first output (centred on
    // q = HALF) has a full window of history.
    written_ = 2 * ORINGS_RS_HALF;
    q_ = ORINGS_RS_HALF;
    frac_ = 0;
  }

  // True while the next output needs input that has not been pushed yet.
  // Compared as a signed DIFFERENCE, so the counters wrapping changes nothing.
  bool NeedsInput() const {
    return static_cast<int32_t>(written_ - q_) <= ORINGS_RS_HALF;
  }

  void Push(float l, float r) {
    l_[written_ & (kRing - 1)] = l;
    r_[written_ & (kRing - 1)] = r;
    ++written_;
  }

  // Only call when !NeedsInput().
  void Next(float *l, float *r) {
    const uint32_t scaled = frac_ * ORINGS_RS_PHASES;
    const uint32_t p = scaled / ORINGS_RS_UP;
    const float f = static_cast<float>(scaled - p * ORINGS_RS_UP) * (1.0f / ORINGS_RS_UP);
    const float *h0 = orings_rs_table[p];
    const float *h1 = orings_rs_table[p + 1];
    const uint32_t base = q_ - ORINGS_RS_HALF + 1;
    float l0 = 0.0f, l1 = 0.0f, r0 = 0.0f, r1 = 0.0f;
    for (int j = 0; j < ORINGS_RS_TAPS; ++j) {
      const uint32_t at = (base + j) & (kRing - 1);
      l0 += h0[j] * l_[at];
      l1 += h1[j] * l_[at];
      r0 += h0[j] * r_[at];
      r1 += h1[j] * r_[at];
    }
    frac_ += ORINGS_RS_DOWN;
    while (frac_ >= ORINGS_RS_UP) {
      frac_ -= ORINGS_RS_UP;
      ++q_;
    }
    *l = l0 + (l1 - l0) * f;
    *r = r0 + (r1 - r0) * f;
  }

 private:
  float l_[kRing];
  float r_[kRing];
  uint32_t written_;  // input frames written (wraps harmlessly: unsigned)
  uint32_t q_;        // integer input time of the next output
  uint32_t frac_;     // fractional part, in 1/147 of an input sample
};

}  // namespace orings

#endif  // ORINGS_RESAMPLER_H_
