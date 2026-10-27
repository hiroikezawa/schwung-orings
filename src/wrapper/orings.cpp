// oRings - the Rings module around the original DSP. See orings.h.
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
#include "wrapper/orings.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace orings {

using rings::PerformanceState;
using rings::Patch;

// MODEL order (metadata.py) -> Rings. Part models are rings::ResonatorModel;
// string-synth entries are rings::FxType, which is what the hardware's model
// buttons select while the easter egg is on (ui.cc: set_fx(model)).
const ModelDef kModels[kNumModels] = {
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

namespace {

// cv_scaler.cc channel_settings_: every pot is low-passed with 0.01 per
// block (ADC_CHANNEL_POT_*).
const float kPotCoefficient = 0.01f;
// Output-stage smoothing per block (LEVEL, GAIN, OUTPUT): ~2.5 ms.
const float kOutCoefficient = 0.2f;
// The V/OCT note sits relative to the tonic; tonic = kTonic + FREQUENCY, so
// at FREQUENCY 0 a MIDI note plays at its own pitch and the sympathetic
// strings' drone (part.cc: notes[0] = tonic) is C2. On hardware the
// FREQUENCY pot sets the same tonic: 12 + 0..60 semitones (cv_scaler.cc).
const int kTonic = 36;

bool ParseNumber(const char *s, float *out) {
  char *end;
  const float v = strtof(s, &end);
  if (end == s) return false;
  while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') ++end;
  if (*end || !isfinite(v)) return false;
  *out = v;
  return true;
}

int OptionIndex(const char *const *names, int n, const char *value) {
  for (int i = 0; i < n; ++i)
    if (!strcmp(names[i], value)) return i;
  return -1;
}

const char *const kResetNames[] = {"-", "RESET"};

// The option names of a stored enum, for name writes.
bool EnumNames(int id, const char *const **names, int *n) {
  switch (id) {
    case P_MODEL: *names = kModelNames; *n = kNumModels; return true;
    case P_POLY: *names = kPolyNames; *n = 4; return true;
    case P_OUTPUT: *names = kOutputNames; *n = 4; return true;
    case P_RESET: *names = kResetNames; *n = 2; return true;
    default: return false;
  }
}

int FindParam(const char *key) {
  for (int i = 0; i < P_COUNT; ++i)
    if (!strcmp(kParams[i].key, key)) return i;
  return -1;
}

int FindAlias(const char *key) {
  for (int i = 0; i < kNumAliases; ++i)
    if (!strcmp(kAliases[i].key, key)) return i;
  return -1;
}

int Format(char *buf, int size, int n) {
  if (n < 0 || n >= size) {
    if (size > 0) buf[0] = 0;
    return -1;
  }
  return n;
}

}  // namespace

void ORings::Init() {
  // The firmware's Part / StringSynthPart are zero-initialised globals, and
  // Init() leaves some members to that (StringSynthPart::clear_fx_,
  // Part::step_counter_). The same start state here.
  memset(static_cast<void *>(this), 0, sizeof(*this));
  for (int i = 0; i < P_COUNT; ++i) values_[i] = kParams[i].def;
  InitParts();
  applied_model_ = model();
  applied_poly_ = static_cast<int>(values_[P_POLY]) + 1;
  ApplyModelNow();
  pot_[0] = values_[P_STRUCTURE];
  pot_[1] = values_[P_BRIGHTNESS];
  pot_[2] = values_[P_DAMPING];
  pot_[3] = values_[P_POSITION];
  chord_ = 0;
  ApplyStructure();
  blocks_since_strum_ = kMinStrumBlocks;
  note_ = 60 - kTonic;
  resampler_.Reset();
  fade_gain_ = 1.0f;
  fade_prev_ = 1.0f;
  out_gain_ = values_[P_LEVEL];
  OutputMatrix(matrix_);
}

void ORings::InitParts() {
  // From the zeroed memory the firmware's globals start from: Init() alone
  // leaves the strings' damping filters holding their last state, which
  // feeds the cleared delay lines a -70 dB residue after a RESET.
  memset(static_cast<void *>(&strummer_), 0, sizeof(strummer_));
  memset(static_cast<void *>(&part_), 0, sizeof(part_));
  memset(static_cast<void *>(&string_synth_), 0, sizeof(string_synth_));
  // rings.cc Init(): the same three calls, the same arguments.
  strummer_.Init(0.01f, rings::kSampleRate / rings::kMaxBlockSize);
  part_.Init(reverb_part_);
  string_synth_.Init(reverb_synth_);
}

void ORings::ApplyModelNow() {
  applied_model_ = model();
  applied_poly_ = static_cast<int>(values_[P_POLY]) + 1;
  const ModelDef &m = kModels[applied_model_];
  // ui.cc sets both parts' polyphony together, and both parts' models.
  part_.set_polyphony(applied_poly_);
  string_synth_.set_polyphony(applied_poly_);
  if (m.string_synth) {
    string_synth_.set_fx(static_cast<rings::FxType>(m.index));
  } else {
    part_.set_model(static_cast<rings::ResonatorModel>(m.index));
  }
}

// CvScaler::Read's chord hysteresis, verbatim, fed by the STRUCTURE value
// (the hardware feeds it the unfiltered STRUCTURE pot + CV).
void ORings::ApplyStructure() {
  float chord = values_[P_STRUCTURE];
  chord *= static_cast<float>(rings::kNumChords - 1);
  const float hysteresis = chord - chord_ > 0.0f ? -0.1f : +0.1f;
  chord_ = static_cast<int32_t>(chord + hysteresis + 0.5f);
  if (chord_ < 0) chord_ = 0;
  if (chord_ > rings::kNumChords - 1) chord_ = rings::kNumChords - 1;
}

bool ORings::SetParam(int id, float v) {
  if (id < 0 || id >= P_COUNT || !isfinite(v)) return false;
  const ParamDef &d = kParams[id];
  if (v < d.min) v = d.min;
  if (v > d.max) v = d.max;
  if (d.type != PT_FLOAT) v = floorf(v + 0.5f);
  if (id == P_RESET) {
    if (v >= 1.0f) Reset();
    return true;
  }
  values_[id] = v;
  if (id == P_STRUCTURE) ApplyStructure();
  return true;
}

bool ORings::SetByKey(const char *key, const char *text) {
  if (!key || !text) return false;
  float v;
  // An enum is written as its INDEX, the form get_param reports (so the
  // grid learns it); a name is accepted only when the text is not a number,
  // because some names are numbers ("1".."4" voices, the "5" and "69" chords).
  int id = FindParam(key);
  if (id >= 0) {
    if (ParseNumber(text, &v)) return SetParam(id, v);
    const char *const *names;
    int n;
    if (!EnumNames(id, &names, &n)) return false;
    const int i = OptionIndex(names, n, text);
    return i >= 0 && SetParam(id, static_cast<float>(i));
  }
  const int a = FindAlias(key);
  if (a < 0) return false;
  if (kAliases[a].chord) {
    int i;
    if (ParseNumber(text, &v)) i = static_cast<int>(floorf(v + 0.5f));
    else if ((i = OptionIndex(kChordNames, kNumChords, text)) < 0) return false;
    if (i < 0) i = 0;
    if (i > kNumChords - 1) i = kNumChords - 1;
    // The STRUCTURE value at the centre of that chord's zone.
    return SetParam(P_STRUCTURE, static_cast<float>(i) / (kNumChords - 1));
  }
  return ParseNumber(text, &v) && SetParam(kAliases[a].param, v);
}

int ORings::GetByKey(const char *key, char *buf, int size) const {
  if (!key || !buf || size <= 0) return -1;
  int id = FindParam(key);
  if (id < 0) {
    const int a = FindAlias(key);
    if (a < 0) return -1;
    if (kAliases[a].chord) return Format(buf, size, snprintf(buf, size, "%d", chord_));
    id = kAliases[a].param;
  }
  if (id == P_RESET) return Format(buf, size, snprintf(buf, size, "0"));
  if (kParams[id].type == PT_FLOAT)
    return Format(buf, size, snprintf(buf, size, "%.4f", values_[id]));
  return Format(buf, size, snprintf(buf, size, "%d", static_cast<int>(values_[id])));
}

void ORings::Enqueue(uint8_t note, uint8_t velocity) {
  if (queue_count_ >= kQueue) return;  // a 32-note pile-up: drop the newest
  queue_[(queue_head_ + queue_count_) % kQueue] = {note, velocity};
  ++queue_count_;
}

void ORings::Midi(const uint8_t *msg, int len) {
  if (!msg || len < 1) return;
  const uint8_t status = msg[0] & 0xF0;
  const uint8_t d1 = len > 1 ? msg[1] & 0x7F : 0;
  const uint8_t d2 = len > 2 ? msg[2] & 0x7F : 0;
  switch (status) {
    case 0x90:
      // Every note-on is a strum, the same note again included. A note-off
      // (or velocity 0) changes nothing: Rings has no gate, its sound decays
      // by DAMPING and its own physics.
      if (len >= 3 && d2 > 0) Enqueue(d1, d2);
      break;
    case 0xE0:
      if (len >= 3) bend_ = ((d2 << 7) | d1) - 8192;
      break;
    case 0xB0:
      if (len < 3) break;
      if (d1 == 120) Reset();                       // All Sound Off
      else if (d1 == 123) queue_count_ = 0;         // All Notes Off: forget waiting strums
      else if (d1 == 121) bend_ = 0;                // Reset All Controllers
      break;
    default:
      break;
  }
}

void ORings::Reset() {
  queue_count_ = 0;
  reset_requested_ = true;
}

float ORings::VelocityGain(uint8_t velocity) const {
  const float v = static_cast<float>(velocity) / 127.0f;
  const float response = values_[P_VELO] * 0.01f;
  return values_[P_EXCITER] * 0.01f * (1.0f - response * (1.0f - v));
}

void ORings::OutputMatrix(float *m) const {
  switch (static_cast<int>(values_[P_OUTPUT])) {
    case 1:  // SUM: what the ODD jack carries with EVEN unpatched (-6 dB)
      m[0] = 0.5f; m[1] = 0.5f; m[2] = 0.5f; m[3] = 0.5f; break;
    case 2:  // ODD on both sides
      m[0] = 1.0f; m[1] = 0.0f; m[2] = 1.0f; m[3] = 0.0f; break;
    case 3:  // EVEN on both sides
      m[0] = 0.0f; m[1] = 1.0f; m[2] = 0.0f; m[3] = 1.0f; break;
    default:  // STEREO: ODD left, EVEN right
      m[0] = 1.0f; m[1] = 0.0f; m[2] = 0.0f; m[3] = 1.0f; break;
  }
}

void ORings::RenderBlock(float *out, float *aux) {
  // MODEL / POLYPHONY / RESET: Part reinitialises its resonators on a model
  // or polyphony change (ConfigureResonators), which on hardware is a click.
  // Here the output fades out over 4 ms, the change is made, and it fades
  // back in over 4 ms.
  const bool change = reset_requested_ || model() != applied_model_ ||
                      static_cast<int>(values_[P_POLY]) + 1 != applied_poly_;
  if (change) fade_ = 1;
  if (fade_ == 1 && fade_gain_ <= 0.0f) {
    if (reset_requested_) {
      InitParts();
      reset_requested_ = false;
    }
    ApplyModelNow();
    fade_ = -1;
  }

  PerformanceState ps;
  ps.internal_exciter = true;   // IN unpatched
  ps.internal_strum = false;    // STRUM patched: the note-on
  ps.internal_note = false;     // V/OCT patched: the note
  ps.strum = false;

  float gain = 1.0f;
  if (queue_count_ && fade_ != 1) {
    int need = static_cast<int>(values_[P_SPREAD]) * 2;  // 2 blocks per ms
    if (need < kMinStrumBlocks) need = kMinStrumBlocks;
    if (blocks_since_strum_ >= need) {
      const Pending p = queue_[queue_head_];
      queue_head_ = (queue_head_ + 1) % kQueue;
      --queue_count_;
      note_ = static_cast<float>(p.note) - kTonic;
      gain = VelocityGain(p.velocity);
      ps.strum = true;
      blocks_since_strum_ = 0;
    }
  }
  if (!ps.strum && blocks_since_strum_ < 1 << 20) ++blocks_since_strum_;

  // CvScaler::Read's FM smoothing (an edge of 0.8 semitones or more is
  // followed at once, anything smaller glides), on bend + FINE.
  const float fm = static_cast<float>(bend_) / 8192.0f * values_[P_BEND] + values_[P_FINE] * 0.01f;
  const float error = fm - fm_cv_;
  if (fabsf(error) >= 0.8f) fm_cv_ = fm;
  else fm_cv_ += 0.02f * error;
  ps.fm = fm_cv_;
  if (ps.fm < -48.0f) ps.fm = -48.0f;
  if (ps.fm > 48.0f) ps.fm = 48.0f;

  ps.note = note_;
  ps.tonic = static_cast<float>(kTonic) + values_[P_FREQ];
  ApplyStructure();
  ps.chord = chord_;

  Patch patch;
  pot_[0] += kPotCoefficient * (values_[P_STRUCTURE] - pot_[0]);
  pot_[1] += kPotCoefficient * (values_[P_BRIGHTNESS] - pot_[1]);
  pot_[2] += kPotCoefficient * (values_[P_DAMPING] - pot_[2]);
  pot_[3] += kPotCoefficient * (values_[P_POSITION] - pot_[3]);
  // cv_scaler.cc ATTENUVERT ranges.
  patch.structure = pot_[0] < 0.9995f ? pot_[0] : 0.9995f;
  patch.brightness = pot_[1];
  patch.damping = pot_[2];
  patch.position = pot_[3];

  const bool strum_sent = ps.strum;
  strummer_.Process(NULL, kBlock, &ps);  // rings.cc: strummer before the part
  if (strum_sent) {
    if (ps.strum) ++strums_;
    else ++swallowed_;
  }

  if (kModels[applied_model_].string_synth) {
    string_synth_.set_exciter_gain(gain);
    string_synth_.Process(ps, patch, in_, out, aux, kBlock);
  } else {
    part_.set_exciter_gain(gain);
    part_.Process(ps, patch, in_, out, aux, kBlock);
  }

  fade_prev_ = fade_gain_;
  if (fade_ == 1) {
    fade_gain_ -= 2.0f / kFadeBlocks;
    if (fade_gain_ < 0.0f) fade_gain_ = 0.0f;
  } else if (fade_ == -1) {
    fade_gain_ += 2.0f / kFadeBlocks;
    if (fade_gain_ >= 1.0f) {
      fade_gain_ = 1.0f;
      fade_ = 0;
    }
  }

  // A non-finite sample would stay in the resonators for good: start them
  // again from silence (it has never happened in the tests; this is a guard).
  for (size_t i = 0; i < kBlock; ++i) {
    if (!isfinite(out[i]) || !isfinite(aux[i])) {
      memset(out, 0, kBlock * sizeof(float));
      memset(aux, 0, kBlock * sizeof(float));
      InitParts();
      ApplyModelNow();
      ++recoveries_;
      break;
    }
  }
}

void ORings::Process(float *left, float *right, size_t frames) {
  float out[kBlock], aux[kBlock];
  for (size_t n = 0; n < frames; ++n) {
    while (resampler_.NeedsInput()) {
      RenderBlock(out, aux);

      // Output stage: OUTPUT routing, LEVEL x GAIN, the fade, each ramped
      // linearly across the block so nothing steps.
      const float g0 = out_gain_ * fade_prev_;
      out_gain_ += kOutCoefficient * (values_[P_LEVEL] * powf(10.0f, values_[P_GAIN] / 20.0f) - out_gain_);
      const float g1 = out_gain_ * fade_gain_;
      float m0[4], target[4];
      OutputMatrix(target);
      for (int k = 0; k < 4; ++k) {
        m0[k] = matrix_[k];
        matrix_[k] += kOutCoefficient * (target[k] - matrix_[k]);
        // Land exactly, so SUM / ODD / EVEN are exactly mono.
        if (fabsf(target[k] - matrix_[k]) < 1e-5f) matrix_[k] = target[k];
      }
      const float step = 1.0f / kBlock;
      for (size_t i = 0; i < kBlock; ++i) {
        const float t = (i + 1) * step;
        const float g = g0 + (g1 - g0) * t;
        const float a = m0[0] + (matrix_[0] - m0[0]) * t;
        const float b = m0[1] + (matrix_[1] - m0[1]) * t;
        const float c = m0[2] + (matrix_[2] - m0[2]) * t;
        const float d = m0[3] + (matrix_[3] - m0[3]) * t;
        resampler_.Push(g * (a * out[i] + b * aux[i]), g * (c * out[i] + d * aux[i]));
      }
    }
    resampler_.Next(&left[n], &right[n]);
  }
}

int ORings::WriteState(char *buf, int size) const {
  int n = snprintf(buf, size, "{\"v\":1");
  for (int i = 0; i < P_COUNT && n >= 0 && n < size; ++i) {
    if (i == P_RESET) continue;
    if (kParams[i].type == PT_FLOAT)
      n += snprintf(buf + n, size - n, ",\"%s\":%.4f", kParams[i].key, values_[i]);
    else
      n += snprintf(buf + n, size - n, ",\"%s\":%d", kParams[i].key, static_cast<int>(values_[i]));
  }
  if (n >= 0 && n < size) n += snprintf(buf + n, size - n, "}");
  return Format(buf, size, n);
}

bool ORings::ReadState(const char *json) {
  if (!json || !strstr(json, "\"v\":1")) return false;
  for (int i = 0; i < P_COUNT; ++i) {
    if (i == P_RESET) continue;
    char pattern[40];
    snprintf(pattern, sizeof(pattern), "\"%s\":", kParams[i].key);
    const char *at = strstr(json, pattern);
    float v = kParams[i].def;  // a key the state lacks returns to its default
    if (at) {
      char *end;
      const float parsed = strtof(at + strlen(pattern), &end);
      if (end != at + strlen(pattern) && isfinite(parsed)) v = parsed;
    }
    SetParam(i, v);
  }
  return true;
}

}  // namespace orings
