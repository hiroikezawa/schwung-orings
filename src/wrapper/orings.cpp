// oRings - the Rings module around the original DSP. See orings.h.
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
#include "wrapper/orings.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wrapper/json_mini.h"
#include "wrapper/scale.h"

namespace orings {

using rings::PerformanceState;
using rings::Patch;

namespace {

// cv_scaler.cc channel_settings_: every pot is low-passed with 0.01 per
// block (ADC_CHANNEL_POT_*).
const float kPotCoefficient = 0.01f;
// On the resonator models (not FM) the BRIGHTNESS knob covers 0.2..1: below
// ~0.2 the strike is filtered 15-20 dB down and the voice nearly mutes. FM keeps
// 0..1 (0 = a pure sine) and the string synth is not muted by it.
const float kBrightnessFloor = 0.2f;
// Output-stage smoothing per block (LEVEL, GAIN, OUTPUT): ~2.5 ms.
const float kOutCoefficient = 0.2f;
// The V/OCT note sits relative to the tonic; tonic = kTonic + FREQUENCY, so
// at FREQUENCY 0 a MIDI note plays at its own pitch and the sympathetic
// strings' drone (part.cc: notes[0] = tonic) is C2. On hardware the
// FREQUENCY pot sets the same tonic: 12 + 0..60 semitones (cv_scaler.cc).
const int kTonic = 36;
const int kClocksPerBeat = 24;
// Automation runs at control rate: every 16 samples (0.36 ms) and exactly on
// each 1/16.
const int kCtrlFrames = 16;
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int B64Val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-') return 62;
  if (c == '_') return 63;
  return -1;
}

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

int ParamIdOf(const char *key) {
  if (!key) return -1;
  const int id = FindParam(key);
  if (id >= 0) return id;
  const int a = FindAlias(key);
  return a < 0 ? -1 : kAliases[a].param;
}

void ORings::Init(uint32_t seed) {
  // The firmware's Part / StringSynthPart are zero-initialised globals, and
  // Init() leaves some members to that (StringSynthPart::clear_fx_,
  // Part::step_counter_). The same start state here.
  memset(static_cast<void *>(this), 0, sizeof(*this));
  for (int i = 0; i < P_COUNT; ++i) {
    values_[i] = kParams[i].def;
    ovr_[i] = NAN;
  }
  motion_.Init();
  clock_.Init();
  RandomPhrase::LibraryInit();   // once per process; a no-op after
  phrase_.Init(seed);
  gates_ = 0xFFFFu;
  last_pos16_ = -1.0;
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

// CvScaler::Read's chord hysteresis (common/rings_models.h), fed by the
// STRUCTURE value (the hardware feeds it the unfiltered STRUCTURE pot + CV).
void ORings::ApplyStructure() { chord_ = ChordHysteresis(Effective(P_STRUCTURE), chord_); }

int ORings::PitchNote(float index) const {
  return PitchMidi(static_cast<int>(index + 0.5f), static_cast<int>(values_[P_ROOT]),
                   static_cast<int>(values_[P_SCALE]));
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
  if (id == P_ROOT || id == P_SCALE) {
    SetKey(id, v);
    return true;
  }
  if (id == P_PITCH) {
    const float top = static_cast<float>(PitchCount(static_cast<int>(values_[P_ROOT]),
                                                    static_cast<int>(values_[P_SCALE])) - 1);
    if (v > top) v = top;
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
    if (!kEnumOptions[id].names) return false;
    const int i = OptionIndex(kEnumOptions[id].names, kEnumOptions[id].count, text);
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

int ORings::GetByKey(const char *key, char *buf, int size, bool effective) const {
  if (!key || !buf || size <= 0) return -1;
  int id = FindParam(key);
  if (id < 0) {
    const int a = FindAlias(key);
    if (a < 0) return -1;
    if (kAliases[a].chord) {
      // What plays is the hysteresis' answer; the knob, the nearest chord.
      const int c = effective ? chord_
                              : static_cast<int>(values_[P_STRUCTURE] * (kNumChords - 1) + 0.5f);
      return Format(buf, size, snprintf(buf, size, "%d", c));
    }
    id = kAliases[a].param;
  }
  if (id == P_RESET) return Format(buf, size, snprintf(buf, size, "0"));
  const float v = effective ? Effective(id) : values_[id];
  if (kParams[id].type == PT_FLOAT) return Format(buf, size, snprintf(buf, size, "%.4f", v));
  return Format(buf, size, snprintf(buf, size, "%d", static_cast<int>(v)));
}

// A new root or scale re-snaps PITCH -- the knob, what is playing, and every
// recorded pitch point -- to the nearest note of the new scale, so the
// strikes do not jump to whatever note now sits at the old index.
void ORings::SetKey(int id, float value) {
  const int r0 = static_cast<int>(values_[P_ROOT]), s0 = static_cast<int>(values_[P_SCALE]);
  values_[id] = value;
  const int r1 = static_cast<int>(values_[P_ROOT]), s1 = static_cast<int>(values_[P_SCALE]);
  if (r0 == r1 && s0 == s1) return;
  auto resnap = [&](float idx) {
    return static_cast<float>(PitchIndexOfMidi(
        static_cast<float>(PitchMidi(static_cast<int>(idx + 0.5f), r0, s0)), r1, s1));
  };
  values_[P_PITCH] = resnap(values_[P_PITCH]);
  if (!isnan(ovr_[P_PITCH])) ovr_[P_PITCH] = resnap(ovr_[P_PITCH]);
  uint16_t *lane = motion_.lane(Motion::LaneOf(P_PITCH));
  for (int i = 0; i < Motion::kPoints; ++i) {
    if (lane[i] == Motion::kEmpty) continue;
    const float x = Motion::Denorm(P_PITCH, Motion::Dq(lane[i]));
    lane[i] = Motion::Q(Motion::Norm(P_PITCH, resnap(x)));
  }
}

void ORings::ToggleGate(int step) {
  if (step >= 0 && step < 16) gates_ ^= static_cast<uint16_t>(1u << step);
}

void ORings::Touch(int param_id, bool down) {
  const int k = Motion::LaneOf(param_id);
  if (k >= 0) motion_.Touch(MotionParams(), k, down);
}

void ORings::ClearMotion(int param_id) {
  const int k = param_id < 0 ? -1 : Motion::LaneOf(param_id);
  if (param_id >= 0 && k < 0) return;
  motion_.Clear(MotionParams(), k);
}

// The RANDOM pad: a new Phrase Gen phrase, written as PITCH automation --
// each note set as a knob value of the scale in use (nearest note, moved to
// ROOT), never quantized afterwards. Phrase Gen's sources span ~6 octaves,
// so each note is folded by octaves into a window around the knob's own
// note (a fifth below to 17 semitones above): the melody keeps its note
// names, the PITCH knob sets its register. Every point of a step holds its
// note.
void ORings::Randomize() {
  const int len = steps();
  int notes[16];
  if (!phrase_.Generate(len, frames_, notes)) return;
  const int r = static_cast<int>(values_[P_ROOT]), sc = static_cast<int>(values_[P_SCALE]);
  const int knob = PitchNote(values_[P_PITCH]);
  int lo = knob - 7, hi = knob + 17;
  if (lo < kPitchLo) { hi += kPitchLo - lo; lo = kPitchLo; }
  if (hi > kPitchHi) { lo -= hi - kPitchHi; hi = kPitchHi; }
  uint16_t *lane = motion_.lane(Motion::LaneOf(P_PITCH));
  for (int st = 0; st < len; ++st) {
    int n = notes[st];
    if (n < 0) n = knob - r;
    n += r;                                   // the phrases are in C
    while (n > hi) n -= 12;
    while (n < lo) n += 12;
    while (n > kPitchHi) n -= 12;
    while (n < kPitchLo) n += 12;
    const uint16_t q = Motion::Q(Motion::Norm(
        P_PITCH, static_cast<float>(PitchIndexOfMidi(static_cast<float>(n), r, sc))));
    for (int j = 0; j < Motion::kSub; ++j) lane[st * Motion::kSub + j] = q;
  }
}

void ORings::Enqueue(uint8_t note, uint8_t velocity, bool seq) {
  if (queue_count_ >= kQueue) return;  // a 32-note pile-up: drop the newest
  queue_[(queue_head_ + queue_count_) % kQueue] = {note, velocity, seq};
  ++queue_count_;
}

void ORings::Midi(const uint8_t *msg, int len) {
  if (!msg || len < 1) return;
  if (msg[0] >= 0xF8) {                    // realtime: the fallback clock
    switch (msg[0]) {
      case 0xFA: clk_running_ = true; clk_awaiting_first_ = true; clk_ticks_ = 0; break;
      case 0xFB: clk_running_ = true; break;
      case 0xFC: clk_running_ = false; break;
      case 0xF8:
        if (!clk_running_) { clk_running_ = true; clk_awaiting_first_ = true; }
        if (clk_awaiting_first_) { clk_awaiting_first_ = false; clk_ticks_ = 0; }
        else ++clk_ticks_;
        break;
      default: break;
    }
    return;
  }
  if (msg[0] >= 0xF0) return;
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
  return Effective(P_EXCITER) * 0.01f * (1.0f - response * (1.0f - v));
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
    int need = queue_[queue_head_].seq ? 0 : static_cast<int>(values_[P_SPREAD]) * 2;  // 2 blocks per ms
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
  pot_[0] += kPotCoefficient * (Effective(P_STRUCTURE) - pot_[0]);
  pot_[1] += kPotCoefficient * (Effective(P_BRIGHTNESS) - pot_[1]);
  pot_[2] += kPotCoefficient * (Effective(P_DAMPING) - pot_[2]);
  pot_[3] += kPotCoefficient * (Effective(P_POSITION) - pot_[3]);
  // cv_scaler.cc ATTENUVERT ranges.
  patch.structure = pot_[0] < 0.9995f ? pot_[0] : 0.9995f;
  const ModelDef &md = kModels[applied_model_];
  const bool floored = !md.string_synth && md.index != rings::RESONATOR_MODEL_FM_VOICE;
  patch.brightness = floored
      ? kBrightnessFloor + (1.0f - kBrightnessFloor) * pot_[1] : pot_[1];
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

// ---- the auto 1/16 -------------------------------------------------------------

double ORings::FallbackBeat() const {
  if (!clk_running_ || clk_awaiting_first_) return -1.0;
  return static_cast<double>(clk_ticks_) / kClocksPerBeat;
}

// The playhead never steps backwards by less than a 1/16 (that is host
// jitter, not a new Play).
void ORings::MotionAt(double pos16) {
  if (pos16 >= 0.0 && last_pos16_ >= 0.0 && pos16 < last_pos16_ && pos16 > last_pos16_ - 1.0)
    pos16 = last_pos16_;
  last_pos16_ = pos16;
  motion_.Update(MotionParams(), pos16, steps());
}

// A trigger step: a strike at the note PITCH names, at the start of this
// 1/16. Rings has no gate, so a step whose pad is off simply does not strike.
void ORings::Hit(int64_t abs16) {
  const int len = steps();
  const int step = static_cast<int>(((abs16 % len) + len) % len);
  if (!((gates_ >> step) & 1u)) return;
  Enqueue(static_cast<uint8_t>(PitchNote(Effective(P_PITCH))), kSeqVelocity, true);
}

void ORings::Process(float *left, float *right, size_t frames, double beat, float bpm,
                     bool host_has_transport) {
  const int n = static_cast<int>(frames);
  if (!host_has_transport) beat = FallbackBeat();
  if (!(bpm >= 20.0f && bpm <= 999.0f)) bpm = 120.0f;
  const double spb = 44100.0 * 60.0 / bpm;
  frames_ += static_cast<uint32_t>(n);
  motion_.Age(n);

  const bool was_running = clock_.running;
  int offsets[Clock::kMaxEvents];
  const int events = clock_.Advance(beat, bpm, n, offsets);
  if (!clock_.running) {
    last_pos16_ = -1.0;
    MotionAt(-1.0);
  } else if (!was_running) {
    last_pos16_ = -1.0;                  // a new Play
  }

  // Split the block at each 1/16 so a strike lands on its sample, and step
  // the automation to the strike's exact 1/16 first, so PITCH and the knobs
  // are the values at the start of that step.
  int pos = 0;
  auto span = [&](int to) {
    while (pos < to) {
      int m = to - pos;
      if (m > kCtrlFrames) m = kCtrlFrames;
      if (clock_.running) MotionAt((beat + pos / spb) * 4.0);
      Render(left + pos, right + pos, static_cast<size_t>(m));
      pos += m;
    }
  };
  for (int k = 0; k < events; ++k) {
    span(offsets[k]);
    const int64_t abs16 = clock_.last_abs - (events - 1 - k);
    if (last_pos16_ > static_cast<double>(abs16)) last_pos16_ = -1.0;   // restart
    MotionAt(static_cast<double>(abs16));
    Hit(abs16);
  }
  span(n);
}

void ORings::Render(float *left, float *right, size_t frames) {
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

// ---- state ----------------------------------------------------------------------
//
// {"v":1,"level":0.7000,"structure":0.2500,...,"model":0,...,
//  "m":"<key>:<data>;...","g":65535}
// "m" carries only lanes that hold automation: Motion::kPoints 12-bit values
// normalized over the parameter's range, two base64url characters each
// (4095 = not written); "g" the trigger pads. A key the state lacks returns
// to its default (a state written by 0.1.0 has no motion and every pad on).

int ORings::WriteState(char *buf, int size) const {
  if (!buf || size <= 0) return -1;
  int n = snprintf(buf, size, "{\"v\":1");
  for (int i = 0; i < P_COUNT && n >= 0 && n < size; ++i) {
    if (i == P_RESET) continue;
    if (kParams[i].type == PT_FLOAT)
      n += snprintf(buf + n, size - n, ",\"%s\":%.4f", kParams[i].key, values_[i]);
    else
      n += snprintf(buf + n, size - n, ",\"%s\":%d", kParams[i].key, static_cast<int>(values_[i]));
  }
  if (n >= 0 && n < size) n += snprintf(buf + n, size - n, ",\"m\":\"");
  const unsigned mask = motion_.Mask();
  bool first = true;
  for (int k = 0; k < Motion::kLanes && n >= 0 && n < size; ++k) {
    if (!(mask & (1u << k))) continue;
    n += snprintf(buf + n, size - n, "%s%s:", first ? "" : ";", kParams[Motion::kParam[k]].key);
    first = false;
    if (n < 0 || n + 2 * Motion::kPoints >= size) return Format(buf, size, -1);
    const uint16_t *lane = motion_.lane(k);
    for (int i = 0; i < Motion::kPoints; ++i) {
      const int q = lane[i] == Motion::kEmpty ? 4095 : lane[i];
      buf[n++] = kB64[q >> 6];
      buf[n++] = kB64[q & 63];
    }
    buf[n] = 0;
  }
  if (n >= 0 && n < size) n += snprintf(buf + n, size - n, "\",\"g\":%u}", gates_);
  return Format(buf, size, n);
}

bool ORings::ReadState(const char *text) {
  if (!text) return false;
  const size_t len = strnlen(text, 65536);
  if (len == 0 || len >= 65536) return false;
  const char *end = text + len;
  const char *p = json::SkipWs(text, end);
  if (p >= end || *p != '{') return false;
  const char *root_end = json::ContainerEnd(p, end);
  if (!root_end) return false;
  float f;
  const char *vp = json::FindMember(p, root_end, "v");
  if (!vp || !json::ReadNumber(vp, root_end, &f) || f != 1.0f) return false;

  // Motion first, into scratch: a malformed lane refuses the whole "m".
  static uint16_t scratch[Motion::kLanes][Motion::kPoints];   // one SPI thread only
  for (int k = 0; k < Motion::kLanes; ++k)
    for (int i = 0; i < Motion::kPoints; ++i) scratch[k][i] = Motion::kEmpty;
  bool have_m = false;
  const char *mp = json::FindMember(p, root_end, "m");
  if (mp && *mp == '"') {
    const char *q = mp + 1;
    have_m = true;
    unsigned seen = 0;
    while (q < root_end && *q != '"') {
      const char *colon = static_cast<const char *>(memchr(q, ':', root_end - q));
      if (!colon || colon - q >= 24) { have_m = false; break; }
      char key[24];
      memcpy(key, q, colon - q);
      key[colon - q] = 0;
      const int id = FindParam(key);
      const int k = id < 0 ? -1 : Motion::LaneOf(id);
      if (id < 0 || colon + 1 + 2 * Motion::kPoints > root_end) { have_m = false; break; }
      if (k >= 0 && (seen & (1u << k))) { have_m = false; break; }
      q = colon + 1;
      if (k < 0) {
        // A parameter that is not recorded (a lane from a later version):
        // its lane is dropped, and the others are kept.
        q += 2 * Motion::kPoints;
        if (*q == ';') ++q;
        else if (*q != '"') { have_m = false; break; }
        continue;
      }
      seen |= 1u << k;
      for (int i = 0; i < Motion::kPoints; ++i) {
        const int hi = B64Val(q[0]), lo = hi < 0 ? -1 : B64Val(q[1]);
        if (hi < 0 || lo < 0) { have_m = false; break; }
        const int x = hi * 64 + lo;
        scratch[k][i] = x == 4095 ? Motion::kEmpty
                                  : static_cast<uint16_t>(x > Motion::kQMax ? Motion::kQMax : x);
        q += 2;
      }
      if (!have_m) break;
      if (*q == ';') ++q;
      else if (*q != '"') { have_m = false; break; }
    }
  }

  auto value = [&](int id) {
    const char *val = json::FindMember(p, root_end, kParams[id].key);
    float x;
    return val && json::ReadNumber(val, root_end, &x) ? x : kParams[id].def;
  };
  // Root and scale first, as they are (no re-snap): PITCH is clamped to the
  // scale in force.
  static const int kKey[2] = {P_ROOT, P_SCALE};
  for (int id : kKey) {
    float x = value(id);
    if (!isfinite(x)) x = kParams[id].def;
    x = floorf(x + 0.5f);
    if (x < kParams[id].min) x = kParams[id].min;
    if (x > kParams[id].max) x = kParams[id].max;
    values_[id] = x;
  }
  for (int i = 0; i < P_COUNT; ++i) {
    if (i == P_RESET || i == P_ROOT || i == P_SCALE) continue;
    SetParam(i, value(i));
  }
  const char *gp = json::FindMember(p, root_end, "g");
  gates_ = 0xFFFFu;
  if (gp && json::ReadNumber(gp, root_end, &f) && f == floorf(f) && f >= 0.0f && f <= 65535.0f)
    gates_ = static_cast<uint16_t>(f);
  motion_.Clear(MotionParams(), -1);
  if (have_m)
    for (int k = 0; k < Motion::kLanes; ++k) memcpy(motion_.lane(k), scratch[k], sizeof(scratch[k]));
  return true;
}

}  // namespace orings
