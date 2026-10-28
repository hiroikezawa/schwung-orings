// oRings - the RANDOM pad: Phrase Gen's generator. MIT.
//
// The same generator and the same six source MIDIs as the Phrase Gen MIDI FX
// (src/phrase/ is a copy of it): each press writes a variation of one source,
// never the one just used, mutation 50 %, no rests. The phrase comes back as
// MIDI notes; the caller turns each into a PITCH knob value (a scale-note
// index) -- set on the knob, never quantized afterwards.
#ifndef ORINGS_RANDOM_PHRASE_H_
#define ORINGS_RANDOM_PHRASE_H_

#include <stdint.h>

namespace orings {

class RandomPhrase {
 public:
  // Builds the shared, read-only pattern library once (safe to call again).
  static void LibraryInit();
  void Init(uint32_t seed);
  // Writes `len` (8 or 16) notes. False if there is no library.
  bool Generate(int len, uint32_t entropy, int *notes);

 private:
  uint32_t rng_;
  int last_source_;       // -1 none yet
  int prev_[8];           // the last first half, so a new one differs
  bool have_prev_;
};

}  // namespace orings

#endif  // ORINGS_RANDOM_PHRASE_H_
