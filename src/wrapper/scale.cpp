// oRings - scales. MIT.
#include "wrapper/scale.h"

#include <math.h>
#include <stdint.h>

namespace orings {

const char *const kScaleNames[kScaleCount] = {
    "Phrygian", "Minor", "Dorian", "Major", "Mixolyd", "Lydian",
    "Locrian", "HarmMin", "PhrygDom", "MinPent", "In", "Chromatic",
};

namespace {

// Bit n = semitone n above the root is in the scale.
const uint16_t kMask[kScaleCount] = {
    (1u<<0)|(1u<<1)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<10),   // Phrygian
    (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<10),   // Minor (Aeolian)
    (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<10),   // Dorian
    (1u<<0)|(1u<<2)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<11),   // Major
    (1u<<0)|(1u<<2)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<10),   // Mixolydian
    (1u<<0)|(1u<<2)|(1u<<4)|(1u<<6)|(1u<<7)|(1u<<9)|(1u<<11),   // Lydian
    (1u<<0)|(1u<<1)|(1u<<3)|(1u<<5)|(1u<<6)|(1u<<8)|(1u<<10),   // Locrian
    (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<11),   // Harmonic minor
    (1u<<0)|(1u<<1)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<10),   // Phrygian dominant
    (1u<<0)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<10),                   // Minor pentatonic
    (1u<<0)|(1u<<1)|(1u<<5)|(1u<<7)|(1u<<8),                    // In (Miyako-bushi)
    0x0FFFu,                                                    // Chromatic
};

void ClampKey(int *root, int *scale) {
  *root = ((*root % 12) + 12) % 12;
  if (*scale < 0 || *scale >= kScaleCount) *scale = 0;
}

bool InScale(int midi, int root, int scale) {
  return (kMask[scale] >> ((midi - root + 120) % 12)) & 1u;
}

}  // namespace

int PitchCount(int root, int scale) {
  ClampKey(&root, &scale);
  int n = 0;
  for (int m = kPitchLo; m <= kPitchHi; ++m) n += InScale(m, root, scale);
  return n;
}

int PitchMidi(int index, int root, int scale) {
  ClampKey(&root, &scale);
  if (index < 0) index = 0;
  int last = kPitchLo;
  for (int m = kPitchLo; m <= kPitchHi; ++m) {
    if (!InScale(m, root, scale)) continue;
    if (index-- == 0) return m;
    last = m;
  }
  return last;                                   // past the top: the top note
}

int PitchIndexOfMidi(float midi, int root, int scale) {
  ClampKey(&root, &scale);
  int best = 0, i = 0;
  float bd = 1e9f;
  for (int m = kPitchLo; m <= kPitchHi; ++m) {
    if (!InScale(m, root, scale)) continue;
    const float d = fabsf(static_cast<float>(m) - midi);
    if (d < bd - 1e-6f) { bd = d; best = i; }    // ascending: a tie keeps the lower
    ++i;
  }
  return best;
}

void NoteName(int midi, char *buf, int size) {
  static const char *const kNames[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
  if (!buf || size <= 0) return;
  if (midi < 0) midi = 0;
  if (midi > 127) midi = 127;
  int n = 0;
  for (const char *s = kNames[midi % 12]; *s && n < size - 1;) buf[n++] = *s++;
  const int oct = midi / 12 - 1;                 // scientific: C1 = MIDI 24
  if (n < size - 1) buf[n++] = static_cast<char>('0' + (oct < 0 ? 0 : oct));
  buf[n] = 0;
}

}  // namespace orings
