// oRings - touch automation and the 1/16 clock. MIT.
//
// A knob's capacitive TOUCH is the write window; playback is an OVERRIDE of
// the knob's value, never a write to it:
//   touch down  (transport running, REC pad on) -> writing starts: the
//               override is dropped (you hear the hand) and the point under
//               the playhead takes the knob's value;
//   held        every point the playhead ENTERS takes the knob's value;
//   touch up    writing stops; the lane plays back from the next update.
// 8 points per 1/16, 12-bit. A point never written plays the knob itself.
// The continuous knobs interpolate between points; PITCH holds a point's
// value. VOLUME, MODEL and the settings are never recorded. Which parameters
// are recorded is scripts/metadata.py's LANES (kLaneParams).
//
// All calls come from the audio callback: no allocation.
#ifndef ORINGS_MOTION_H_
#define ORINGS_MOTION_H_

#include <stdint.h>

#include "orings_meta.h"

namespace orings {

// ---- the 1/16 clock ---------------------------------------------------------
// Owns no clock: each block it is handed the host transport position in beats
// (0.0 = the downbeat of Move's Play, < 0 = stopped) and answers where in the
// block each 1/16 boundary falls. A start (or a backwards jump) fires at
// offset 0; a boundary fires exactly once; a forward gap fires once, late.
struct Clock {
  static const int kMaxEvents = 4;
  bool running;
  int64_t last_abs;               // last 1/16 that fired
  void Init() { running = false; last_abs = -1; }
  // Writes ascending sample offsets; the n-th of `count` is 1/16 number
  // last_abs - (count - 1 - n).
  int Advance(double beat, float bpm, int frames, int *offsets);
};

// ---- touch automation --------------------------------------------------------
class Motion {
 public:
  static const int kLanes = kNumLanes;
  static const int kMaxSteps = 16;
  static const int kSub = 8;                       // points per 1/16
  static const int kPoints = kMaxSteps * kSub;
  static const uint16_t kEmpty = 0xFFFF;
  static const int kQMax = 4094;                   // 12-bit values 0..4094
  static const int kTouchTimeoutSamples = 22050;   // 0.5 s: the UI restates every 150 ms

  // Lane k -> parameter; bit k of every mask. New lanes go at the END.
  static constexpr const int *kParam = kLaneParams;
  static int LaneOf(int param_id);                 // -1: not recorded

  // The owner's parameters: base values and the playback override (NAN = none).
  struct Params {
    float *base;
    float *ovr;
  };

  void Init();
  void set_write(bool on) { write_ = on; }
  bool write() const { return write_; }

  // down = true again on a held lane only restates it (heartbeat).
  void Touch(Params p, int lane, bool down);
  void Age(int frames);
  // pos16 = 1/16 position since the transport's start, < 0 stopped; `len` the
  // loop in steps (8 or 16). Records under a finger, else sets the overrides.
  void Update(Params p, double pos16, int len);
  void Clear(Params p, int lane);                  // -1 = all
  unsigned Mask() const;
  unsigned Recording() const { return (write_ && point_ >= 0) ? touched_ : 0u; }
  int step() const { return step_; }

  // Raw access for the state blob and the RANDOM pad.
  uint16_t *lane(int k) { return pt_[k]; }
  const uint16_t *lane(int k) const { return pt_[k]; }

  static float Norm(int param_id, float v);
  static float Denorm(int param_id, float n);
  static uint16_t Q(float v01);
  static float Dq(uint16_t q) { return static_cast<float>(q) / kQMax; }

 private:
  void Playback(Params p, int k, int ip, int npts, float frac);

  uint16_t pt_[kLanes][kPoints];
  unsigned touched_;
  int last_[kLanes];
  int age_[kLanes];
  int point_, step_;
  bool write_;           // the REC pad: off at load, never saved
};

}  // namespace orings

#endif  // ORINGS_MOTION_H_
