// oRings FX - Move's 44.1 kHz -> Rings' 48 kHz, mono (the IN jack).
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
//
// A fixed polyphase FIR (scripts/gen_tables.py, orings_up_table): each output
// is an ORINGS_UP_K-tap dot product with one of ORINGS_UP_L phases. The
// phase advances by ORINGS_UP_M per output and the input position by
// floor((p + M) / L): integer arithmetic only, so the ratio is exact and
// nothing drifts. Input-driven: Push() one 44.1 kHz sample, then
// while (Ready()) Next(). No allocation: the history lives in the object.
#ifndef ORINGS_FX_UPSAMPLER_H_
#define ORINGS_FX_UPSAMPLER_H_

#include "orings_tables.h"

namespace oringsfx {

class Upsampler {
 public:
  void Reset() {
    for (int i = 0; i < 2 * ORINGS_UP_K; ++i) hist_[i] = 0.0f;
    w_ = 0;
    phase_ = 0;
    lag_ = 1;  // one input must arrive before the first output
  }

  inline bool Ready() const { return lag_ <= 0; }

  inline void Push(float x) {
    // Written twice, so the K most recent samples are always contiguous at
    // hist_ + w_ (oldest .. newest).
    hist_[w_] = hist_[w_ + ORINGS_UP_K] = x;
    if (++w_ == ORINGS_UP_K) w_ = 0;
    --lag_;
  }

  inline float Next() {
    const float *c = orings_up_table + phase_ * ORINGS_UP_K;
    const float *x = hist_ + w_;
    float a0 = 0.0f, a1 = 0.0f;
    for (int k = 0; k < ORINGS_UP_K; k += 2) {
      a0 += c[k] * x[k];
      a1 += c[k + 1] * x[k + 1];
    }
    phase_ += ORINGS_UP_M;
    while (phase_ >= ORINGS_UP_L) {
      phase_ -= ORINGS_UP_L;
      ++lag_;
    }
    return a0 + a1;
  }

 private:
  float hist_[2 * ORINGS_UP_K];
  int w_;
  int phase_;
  int lag_;
};

}  // namespace oringsfx

#endif  // ORINGS_FX_UPSAMPLER_H_
