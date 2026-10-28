// Shared by oRings and oRings FX: what each MODEL value runs, and STRUCTURE's
// chord hysteresis. Both modules list the same MODELs in the same order
// (scripts/metadata.py MODELS), so this table is the one place that maps
// them onto the Rings DSP.
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
#ifndef ORINGS_COMMON_RINGS_MODELS_H_
#define ORINGS_COMMON_RINGS_MODELS_H_

#include <stdint.h>

#include "rings/dsp/dsp.h"
#include "rings/dsp/part.h"
#include "rings/dsp/string_synth_part.h"

namespace orings {

// A Part model, or the string synth (the Disastrous Peace easter egg) with
// one of its FX.
struct ModelDef { bool string_synth; int index; };

// MODEL order (metadata.py) -> Rings. Part models are rings::ResonatorModel;
// string-synth entries are rings::FxType, which is what the hardware's model
// buttons select while the easter egg is on (ui.cc: set_fx(model)).
const int kModelCount = 12;
const ModelDef kModels[kModelCount] = {
  {false, rings::RESONATOR_MODEL_MODAL},
  {false, rings::RESONATOR_MODEL_SYMPATHETIC_STRING},
  {false, rings::RESONATOR_MODEL_STRING},
  {false, rings::RESONATOR_MODEL_STRING_AND_REVERB},           // Karplusverb
  {false, rings::RESONATOR_MODEL_FM_VOICE},                    // 2-op FM
  {false, rings::RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED},  // Western Chords
  {true, rings::FX_FORMANT},
  {true, rings::FX_CHORUS},
  {true, rings::FX_REVERB},
  {true, rings::FX_FORMANT_2},
  {true, rings::FX_ENSEMBLE},
  {true, rings::FX_REVERB_2},
};

// CvScaler::Read's chord hysteresis, verbatim (cv_scaler.cc), fed by the
// STRUCTURE value; `chord` is the previous result.
inline int ChordHysteresis(float structure, int chord) {
  float c = structure * static_cast<float>(rings::kNumChords - 1);
  const float hysteresis = c - chord > 0.0f ? -0.1f : +0.1f;
  chord = static_cast<int32_t>(c + hysteresis + 0.5f);
  if (chord < 0) chord = 0;
  if (chord > rings::kNumChords - 1) chord = rings::kNumChords - 1;
  return chord;
}

}  // namespace orings

#endif  // ORINGS_COMMON_RINGS_MODELS_H_
