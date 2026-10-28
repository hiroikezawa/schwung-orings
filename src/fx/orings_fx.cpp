// oRings FX - the Rings module as an audio effect. See orings_fx.h.
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
#include "fx/orings_fx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace oringsfx {

using orings::kModels;
using orings::ModelDef;
using rings::Patch;
using rings::PerformanceState;

namespace {

// cv_scaler.cc channel_settings_: every pot is low-passed with 0.01 per block.
const float kPotCoefficient = 0.01f;
// Wet-stage smoothing per Rings block (LEVEL, ROUTING, WIDTH): ~2.5 ms.
const float kOutCoefficient = 0.2f;
// MIX and INPUT GAIN, per 44.1 kHz sample: ~10 ms.
const float kMixCoefficient = 0.0023f;
// rings.cc: the IN jack's noise gate.
const float kNoiseGateThreshold = 0.00003f;
// STRUM SENSITIVITY: the onset detector's input is scaled by up to +-30 dB;
// 50 % (0 dB) is exactly what the hardware's detector hears.
const float kSensRangeDb = 60.0f;

const float kHalfPi = 1.57079632679489662f;

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

bool EnumNames(int id, const char *const **names, int *n) {
  switch (id) {
    case P_FREQ: *names = kFreqNames; *n = kNumFreqNotes; return true;
    case P_MODEL: *names = kModelNames; *n = kNumModels; return true;
    case P_STRUM: *names = kStrumNames; *n = 3; return true;
    case P_EXCITER: *names = kExciterNames; *n = 2; return true;
    case P_POLY: *names = kPolyNames; *n = 3; return true;
    case P_ROUTING: *names = kRoutingNames; *n = 3; return true;
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

bool IsUnused(const char *key) {
  for (int i = 0; i < kNumUnused; ++i)
    if (!strcmp(kUnusedKeys[i], key)) return true;
  return false;
}

int Format(char *buf, int size, int n) {
  if (n < 0 || n >= size) {
    if (size > 0) buf[0] = 0;
    return -1;
  }
  return n;
}

float DbToGain(float db) { return powf(10.0f, db * 0.05f); }

// The wet output's protection: linear to 0.9, then a knee that meets full
// scale asymptotically, so a hot resonance rounds off rather than clipping.
// The dry signal never goes through it.
float SoftClip(float x) {
  const float kKnee = 0.9f;
  const float a = fabsf(x);
  if (a <= kKnee) return x;
  float z = (a - kKnee) * (1.0f / (1.0f - kKnee));
  z = z >= 3.0f ? 1.0f : z * (27.0f + z * z) / (27.0f + 9.0f * z * z);
  const float y = kKnee + (1.0f - kKnee) * z;
  return x < 0.0f ? -y : y;
}

inline float Clamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

}  // namespace

void RingsFx::Init() {
  // The firmware's Part / StringSynthPart are zero-initialised globals and
  // Init() relies on that for a few members: the same start state here.
  memset(static_cast<void *>(this), 0, sizeof(*this));
  for (int i = 0; i < P_COUNT; ++i) values_[i] = kParams[i].def;
  InitParts();
  up_.Reset();
  down_.Reset();
  fifo_write_ = kFifoPrefill;  // the FIFO starts holding this much silence
  Snap();
}

void RingsFx::InitParts() {
  memset(static_cast<void *>(&strummer_), 0, sizeof(strummer_));
  memset(static_cast<void *>(&part_), 0, sizeof(part_));
  memset(static_cast<void *>(&string_synth_), 0, sizeof(string_synth_));
  // rings.cc Init(): the same three calls, the same arguments.
  strummer_.Init(0.01f, rings::kSampleRate / rings::kMaxBlockSize);
  part_.Init(reverb_part_);
  string_synth_.Init(reverb_synth_);
  in_level_ = 0.0f;
}

void RingsFx::ApplyModelNow() {
  applied_model_ = model();
  applied_poly_ = polyphony();
  const ModelDef &m = kModels[applied_model_];
  part_.set_polyphony(applied_poly_);
  string_synth_.set_polyphony(applied_poly_);
  if (m.string_synth) string_synth_.set_fx(static_cast<rings::FxType>(m.index));
  else part_.set_model(static_cast<rings::ResonatorModel>(m.index));
}

void RingsFx::Snap() {
  ApplyModelNow();
  fade_ = 0;
  fade_gain_ = fade_prev_ = 1.0f;
  pot_[0] = values_[P_STRUCTURE];
  pot_[1] = values_[P_BRIGHTNESS];
  pot_[2] = values_[P_DAMPING];
  pot_[3] = values_[P_POSITION];
  pot_[4] = static_cast<float>(kFreqLowNote) + values_[P_FREQ];
  chord_ = orings::ChordHysteresis(values_[P_STRUCTURE], chord_);
  fm_cv_ = values_[P_FINE] * 0.01f;
  mix_ = values_[P_MIX];
  wet_gain_ = DbToGain(values_[P_LEVEL]);
  WetMatrix(matrix_);
}

bool RingsFx::SetParam(int id, float v) {
  if (id < 0 || id >= P_COUNT || !isfinite(v)) return false;
  const ParamDef &d = kParams[id];
  if (v < d.min) v = d.min;
  if (v > d.max) v = d.max;
  if (d.type != PT_FLOAT) v = floorf(v + 0.5f);
  values_[id] = v;
  if (id == P_STRUCTURE) chord_ = orings::ChordHysteresis(v, chord_);
  return true;
}

bool RingsFx::SetByKey(const char *key, const char *text) {
  if (!key || !text) return false;
  float v;
  // An enum is written as its INDEX (the form get_param reports); a name is
  // accepted only when the text is not a number ("1" "2" "4" voices).
  const int id = FindParam(key);
  if (id >= 0) {
    if (ParseNumber(text, &v)) return SetParam(id, v);
    const char *const *names;
    int n;
    if (!EnumNames(id, &names, &n)) return false;
    const int i = OptionIndex(names, n, text);
    return i >= 0 && SetParam(id, static_cast<float>(i));
  }
  if (IsUnused(key)) return true;  // a "--" cell: nothing to change
  const int a = FindAlias(key);
  if (a < 0) return false;
  if (kAliases[a].chord) {
    int i;
    if (ParseNumber(text, &v)) i = static_cast<int>(floorf(v + 0.5f));
    else if ((i = OptionIndex(kChordNames, kNumChords, text)) < 0) return false;
    if (i < 0) i = 0;
    if (i > kNumChords - 1) i = kNumChords - 1;
    return SetParam(P_STRUCTURE, static_cast<float>(i) / (kNumChords - 1));
  }
  return ParseNumber(text, &v) && SetParam(kAliases[a].param, v);
}

int RingsFx::GetByKey(const char *key, char *buf, int size) const {
  if (!key || !buf || size <= 0) return -1;
  if (IsUnused(key)) return Format(buf, size, snprintf(buf, size, "0"));
  int id = FindParam(key);
  if (id < 0) {
    const int a = FindAlias(key);
    if (a < 0) return -1;
    if (kAliases[a].chord) return Format(buf, size, snprintf(buf, size, "%d", chord_));
    id = kAliases[a].param;
  }
  if (kParams[id].type == PT_FLOAT)
    return Format(buf, size, snprintf(buf, size, "%.4f", values_[id]));
  return Format(buf, size, snprintf(buf, size, "%d", static_cast<int>(values_[id])));
}

void RingsFx::Midi(const uint8_t *msg, int len) {
  if (!msg || len < 3) return;
  // STRUM MIDI: a note-on is a pulse into the STRUM jack. The note number
  // changes nothing (FREQUENCY sets the pitch); note-offs are ignored, as
  // Rings has no gate.
  if ((msg[0] & 0xF0) == 0x90 && (msg[2] & 0x7F) > 0 && static_cast<int>(values_[P_STRUM]) == 1) {
    if (midi_strums_ < kMidiQueue) ++midi_strums_;
  }
}

void RingsFx::WetMatrix(float *m) const {
  // L = m0 * ODD + m1 * EVEN, R = m2 * ODD + m3 * EVEN.
  switch (static_cast<int>(values_[P_ROUTING])) {
    case 1:  // ODD on both sides
      m[0] = 1.0f; m[1] = 0.0f; m[2] = 1.0f; m[3] = 0.0f; break;
    case 2:  // EVEN on both sides
      m[0] = 0.0f; m[1] = 1.0f; m[2] = 0.0f; m[3] = 1.0f; break;
    default: {
      // STEREO: ODD left, EVEN right at WIDTH 100 %; at 0 % both sides carry
      // (ODD + EVEN) / 2, the mono sum.
      const float w = values_[P_WIDTH];
      const float a = 0.5f * (1.0f + w), b = 0.5f * (1.0f - w);
      m[0] = a; m[1] = b; m[2] = b; m[3] = a;
      break;
    }
  }
}

void RingsFx::RenderBlock(const float *in_jack, float *out, float *aux) {
  // MODEL / POLYPHONY: Part reinitialises its resonators on either change,
  // which on hardware is a click. Fade out 4 ms, switch, fade back in.
  if (model() != applied_model_ || polyphony() != applied_poly_) fade_ = 1;
  if (fade_ == 1 && fade_gain_ <= 0.0f) {
    ApplyModelNow();
    fade_ = -1;
  }
  const bool string_synth = kModels[applied_model_].string_synth;
  const int strum_mode = static_cast<int>(values_[P_STRUM]);

  // rings.cc FillBuffer: in easter-egg mode the input goes in as it is;
  // otherwise through the noise gate, verbatim.
  float in[kBlock];
  if (string_synth) {
    for (size_t i = 0; i < kBlock; ++i) in[i] = in_jack[i];
  } else {
    float in_level = in_level_;
    for (size_t i = 0; i < kBlock; ++i) {
      const float in_sample = in_jack[i];
      float error, gain;
      error = in_sample * in_sample - in_level;
      in_level += error * (error > 0.0f ? 0.1f : 0.0001f);
      gain = in_level <= kNoiseGateThreshold ? (1.0f / kNoiseGateThreshold) * in_level : 1.0f;
      in[i] = gain * in_sample;
    }
    in_level_ = in_level;
  }

  // What the onset detector hears: the same signal, scaled by SENSITIVITY.
  float detect[kBlock];
  const float sens = DbToGain((values_[P_SENS] - 0.5f) * kSensRangeDb);
  for (size_t i = 0; i < kBlock; ++i) detect[i] = in[i] * sens;

  PerformanceState ps;
  // V/OCT unpatched: cv_scaler.cc sets note 0 and an unquantised tonic.
  ps.internal_note = true;
  ps.note = 0.0f;
  // IN patched. AUTO: STRUM unpatched, so the strummer strums on onsets.
  // MIDI: STRUM patched, a note-on is its pulse. OFF: patched, silent.
  ps.internal_exciter = false;
  ps.internal_strum = strum_mode == 0;
  ps.strum = false;
  if (strum_mode == 1 && midi_strums_ > 0) {
    ps.strum = true;
    midi_strums_ = 0;  // several within one block are one pulse
  } else if (strum_mode != 1) {
    midi_strums_ = 0;
  }

  // CvScaler::Read's FM smoothing, on FINE (the FM input, in semitones).
  const float fm = values_[P_FINE] * 0.01f;
  const float error = fm - fm_cv_;
  if (fabsf(error) >= 0.8f) fm_cv_ = fm;
  else fm_cv_ += 0.02f * error;
  ps.fm = fm_cv_;

  // FREQUENCY: the pot, low-passed as cv_scaler.cc low-passes it; with V/OCT
  // unpatched the hardware uses it unquantised (tonic = 12 + transpose), so
  // a change glides over ~50 ms instead of stepping.
  pot_[4] += kPotCoefficient * (static_cast<float>(kFreqLowNote) + values_[P_FREQ] - pot_[4]);
  ps.tonic = pot_[4];
  chord_ = orings::ChordHysteresis(values_[P_STRUCTURE], chord_);
  ps.chord = chord_;

  Patch patch;
  pot_[0] += kPotCoefficient * (values_[P_STRUCTURE] - pot_[0]);
  pot_[1] += kPotCoefficient * (values_[P_BRIGHTNESS] - pot_[1]);
  pot_[2] += kPotCoefficient * (values_[P_DAMPING] - pot_[2]);
  pot_[3] += kPotCoefficient * (values_[P_POSITION] - pot_[3]);
  patch.structure = pot_[0] < 0.9995f ? pot_[0] : 0.9995f;
  patch.brightness = pot_[1];
  patch.damping = pot_[2];
  patch.position = pot_[3];

  // rings.cc: the strummer before the part. In easter-egg mode the firmware
  // gives it no audio (NULL); in AUTO this one hears the input, so the
  // Disastrous Peace chords are strummed by the drums too (OFF or MIDI give
  // the hardware's behaviour).
  const float *strum_in = string_synth && strum_mode != 0 ? NULL : detect;
  if (fade_ == 1) ps.internal_strum = false;  // no strum into a switch
  strummer_.Process(strum_in, kBlock, &ps);
  if (ps.strum) ++strums_;

  // INTERNAL EXCITER ON: Part also runs its own exciter (pulse / noise
  // burst / FM envelope, fired by a strum), on top of the input.
  ps.internal_exciter = values_[P_EXCITER] >= 0.5f;

  if (string_synth) string_synth_.Process(ps, patch, in, out, aux, kBlock);
  else part_.Process(ps, patch, in, out, aux, kBlock);

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
  // again from silence (a guard; it has never fired in the tests).
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

void RingsFx::ProcessFloat(const float *in_l, const float *in_r, float *out_l, float *out_r, int frames) {
  const float in_gain_target = DbToGain(values_[P_IN_GAIN]);
  const float mix_target = values_[P_MIX];
  for (int n = 0; n < frames; ++n) {
    const float dl = in_l[n], dr = in_r[n];

    // The IN jack: the stereo input summed to mono (a mono source stays at
    // its own level), INPUT GAIN, and the codec's clipping at full scale.
    up_.Push(Clamp((dl + dr) * 0.5f * in_gain_target, -1.0f, 1.0f));
    while (up_.Ready()) fifo_[fifo_write_++ & (kFifoSize - 1)] = up_.Next();

    while (down_.NeedsInput()) {
      float in48[kBlock], out[kBlock], aux[kBlock];
      for (size_t i = 0; i < kBlock; ++i) {
        if (fifo_read_ != fifo_write_) {
          in48[i] = fifo_[fifo_read_++ & (kFifoSize - 1)];
        } else {
          in48[i] = 0.0f;
          ++underruns_;
        }
      }
      RenderBlock(in48, out, aux);

      // Wet stage: ROUTING x WIDTH, LEVEL, the MODEL fade, each ramped
      // linearly across the block.
      const float g0 = wet_gain_ * fade_prev_;
      wet_gain_ += kOutCoefficient * (DbToGain(values_[P_LEVEL]) - wet_gain_);
      const float g1 = wet_gain_ * fade_gain_;
      float m0[4], target[4];
      WetMatrix(target);
      for (int k = 0; k < 4; ++k) {
        m0[k] = matrix_[k];
        matrix_[k] += kOutCoefficient * (target[k] - matrix_[k]);
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
        down_.Push(g * (a * out[i] + b * aux[i]), g * (c * out[i] + d * aux[i]));
      }
    }
    float wl, wr;
    down_.Next(&wl, &wr);

    // The dry signal, delayed by the wet path's round trip.
    dry_l_[dry_write_ & (kDryRing - 1)] = dl;
    dry_r_[dry_write_ & (kDryRing - 1)] = dr;
    const uint32_t at = (dry_write_ - kDryDelay) & (kDryRing - 1);
    ++dry_write_;
    const float xl = dry_l_[at], xr = dry_r_[at];

    // MIX: equal power, exactly dry at 0 and exactly wet at 1.
    float diff = mix_target - mix_;
    mix_ = fabsf(diff) < 1e-4f ? mix_target : mix_ + kMixCoefficient * diff;
    if (mix_ <= 0.0f) {
      out_l[n] = xl;
      out_r[n] = xr;
      continue;
    }
    wl = SoftClip(wl);
    wr = SoftClip(wr);
    if (mix_ >= 1.0f) {
      out_l[n] = wl;
      out_r[n] = wr;
      continue;
    }
    // Dry + wet can add up past full scale: the same knee on the sum (it
    // leaves anything under 0.9 untouched).
    const float cd = cosf(mix_ * kHalfPi), cw = sinf(mix_ * kHalfPi);
    out_l[n] = SoftClip(xl * cd + wl * cw);
    out_r[n] = SoftClip(xr * cd + wr * cw);
  }
}

void RingsFx::Process(int16_t *io, int frames) {
  float l[128], r[128];
  while (frames > 0) {
    const int k = frames < 128 ? frames : 128;
    for (int i = 0; i < k; ++i) {
      l[i] = io[2 * i] * (1.0f / 32768.0f);
      r[i] = io[2 * i + 1] * (1.0f / 32768.0f);
    }
    ProcessFloat(l, r, l, r, k);
    for (int i = 0; i < k; ++i) {
      // Exact for the dry path: x / 32768 * 32768 is x again.
      io[2 * i] = static_cast<int16_t>(lrintf(Clamp(l[i] * 32768.0f, -32768.0f, 32767.0f)));
      io[2 * i + 1] = static_cast<int16_t>(lrintf(Clamp(r[i] * 32768.0f, -32768.0f, 32767.0f)));
    }
    io += 2 * k;
    frames -= k;
  }
}

int RingsFx::WriteState(char *buf, int size) const {
  int n = snprintf(buf, size, "{\"v\":1");
  for (int i = 0; i < P_COUNT && n >= 0 && n < size; ++i) {
    if (kParams[i].type == PT_FLOAT)
      n += snprintf(buf + n, size - n, ",\"%s\":%.4f", kParams[i].key, values_[i]);
    else
      n += snprintf(buf + n, size - n, ",\"%s\":%d", kParams[i].key, static_cast<int>(values_[i]));
  }
  if (n >= 0 && n < size) n += snprintf(buf + n, size - n, "}");
  return Format(buf, size, n);
}

bool RingsFx::ReadState(const char *json) {
  if (!json || !strstr(json, "\"v\":1")) return false;
  for (int i = 0; i < P_COUNT; ++i) {
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

}  // namespace oringsfx
