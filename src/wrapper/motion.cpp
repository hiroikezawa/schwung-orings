// oRings - touch automation and the 1/16 clock. MIT.
#include "wrapper/motion.h"

#include <math.h>

namespace orings {

// Interpolated between points; PITCH (a scale note) holds a point's value.
static bool Continuous(int k) { return kLaneParams[k] != P_PITCH; }

int Clock::Advance(double beat, float bpm, int frames, int *offsets) {
  if (!(beat >= 0.0) || frames <= 0) {          // stopped (or NaN)
    running = false;
    return 0;
  }
  if (!(bpm >= 20.0f && bpm <= 999.0f)) bpm = 120.0f;
  const double spb = 44100.0 * 60.0 / bpm;     // samples per beat
  int n = 0;
  const int64_t cur = static_cast<int64_t>(floor(beat * 4.0 + 1e-9));
  if (!running || cur < last_abs || cur > last_abs + 1) {
    running = true;
    last_abs = cur;
    offsets[n++] = 0;
  }
  while (n < kMaxEvents) {
    const int64_t next = last_abs + 1;
    const double off = (static_cast<double>(next) / 4.0 - beat) * spb;
    if (off >= frames) break;
    const int o = off <= 0.0 ? 0 : static_cast<int>(off);
    if (o >= frames) break;
    last_abs = next;
    offsets[n++] = o;
  }
  return n;
}

int Motion::LaneOf(int param_id) {
  for (int k = 0; k < kLanes; ++k) if (kParam[k] == param_id) return k;
  return -1;
}

float Motion::Norm(int id, float v) {
  return (v - kParams[id].min) / (kParams[id].max - kParams[id].min);
}

float Motion::Denorm(int id, float n) {
  const float v = kParams[id].min + n * (kParams[id].max - kParams[id].min);
  return kParams[id].type == PT_FLOAT ? v : floorf(v + 0.5f);
}

uint16_t Motion::Q(float v01) {
  if (!(v01 >= 0.0f)) v01 = 0.0f;
  if (v01 > 1.0f) v01 = 1.0f;
  return static_cast<uint16_t>(v01 * kQMax + 0.5f);
}

void Motion::Init() {
  for (int k = 0; k < kLanes; ++k) {
    for (int i = 0; i < kPoints; ++i) pt_[k][i] = kEmpty;
    last_[k] = -1;
    age_[k] = 0;
  }
  touched_ = 0;
  point_ = step_ = -1;
  write_ = false;
}

void Motion::Touch(Params p, int k, bool down) {
  if (k < 0 || k >= kLanes) return;
  const int id = kParam[k];
  age_[k] = 0;
  if (down && (touched_ & (1u << k))) return;   // heartbeat
  last_[k] = -1;
  if (!down) { touched_ &= ~(1u << k); return; }
  touched_ |= 1u << k;
  if (!write_) return;                          // REC off: touch is read-only
  // Pick up what is PLAYING, so taking hold of an automated knob does not
  // make the sound jump to wherever the knob was left.
  if (!isnan(p.ovr[id])) p.base[id] = p.ovr[id];
  p.ovr[id] = NAN;
  if (point_ >= 0) {
    pt_[k][point_] = Q(Norm(id, p.base[id]));
    last_[k] = point_;
  }
}

void Motion::Age(int frames) {
  for (int k = 0; k < kLanes; ++k) {
    if (!(touched_ & (1u << k))) continue;
    if ((age_[k] += frames) > kTouchTimeoutSamples) {
      touched_ &= ~(1u << k);
      last_[k] = -1;
    }
  }
}

void Motion::Playback(Params p, int k, int ip, int npts, float frac) {
  const int id = kParam[k];
  const uint16_t a = pt_[k][ip];
  if (a == kEmpty) { p.ovr[id] = NAN; return; }
  float v = Dq(a);
  if (Continuous(k)) {
    const uint16_t b = pt_[k][ip + 1 == npts ? 0 : ip + 1];
    if (b != kEmpty) v += (Dq(b) - v) * frac;
  }
  p.ovr[id] = Denorm(id, v);
}

void Motion::Update(Params p, double pos16, int len) {
  if (!(pos16 >= 0.0)) {                        // stopped: the knobs play
    for (int k = 0; k < kLanes; ++k) { p.ovr[kParam[k]] = NAN; last_[k] = -1; }
    point_ = step_ = -1;
    return;
  }
  if (len != 8 && len != 16) len = 8;
  const int npts = len * kSub;
  const double P = pos16 * kSub;
  const double fl = floor(P + 1e-9);
  float frac = static_cast<float>(P - fl);
  if (frac < 0.0f) frac = 0.0f;
  if (frac > 1.0f) frac = 1.0f;
  const int64_t ip64 = static_cast<int64_t>(fl);
  const int ip = static_cast<int>(((ip64 % npts) + npts) % npts);

  for (int k = 0; k < kLanes; ++k) {
    if (write_ && (touched_ & (1u << k))) {
      const int id = kParam[k];
      p.ovr[id] = NAN;
      const uint16_t q = Q(Norm(id, p.base[id]));
      // Every point ENTERED since the last update takes the hand's value; the
      // current point is not rewritten while inside it. Host beat jitter can
      // step back a hair across a point edge: that is not a new point.
      int j = last_[k];
      if (j >= 0 && j < npts) {
        const int back = (j - ip + npts) % npts;
        if (back > 0 && back < kSub) continue;
      }
      if (j < 0 || j >= npts) {
        pt_[k][ip] = q;
      } else {
        for (int n = 0; j != ip && n < npts; ++n) {
          j = j + 1 == npts ? 0 : j + 1;
          pt_[k][j] = q;
        }
      }
      last_[k] = ip;
    } else {
      Playback(p, k, ip, npts, frac);
    }
  }
  point_ = ip;
  step_ = ip / kSub;
}

void Motion::Clear(Params p, int lane) {
  for (int k = 0; k < kLanes; ++k) {
    if (lane >= 0 && k != lane) continue;
    for (int i = 0; i < kPoints; ++i) pt_[k][i] = kEmpty;
    last_[k] = -1;
    p.ovr[kParam[k]] = NAN;
  }
}

unsigned Motion::Mask() const {
  unsigned mask = 0;
  for (int k = 0; k < kLanes; ++k)
    for (int i = 0; i < kPoints; ++i)
      if (pt_[k][i] != kEmpty) { mask |= 1u << k; break; }
  return mask;
}

}  // namespace orings
