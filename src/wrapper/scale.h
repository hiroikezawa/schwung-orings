// oRings - the PITCH knob IS a note of the scale. MIT.
//
// PITCH is an integer INDEX into the notes of (ROOT, SCALE) between C1 and C6
// (five octaves, as the user asked): every detent is the next scale note,
// nothing is quantized afterwards. 36 notes in C Phrygian, 61 chromatic.
#ifndef ORINGS_SCALE_H_
#define ORINGS_SCALE_H_

namespace orings {

const int kPitchLo = 24;          // C1
const int kPitchHi = 84;          // C6
const int kScaleCount = 12;

extern const char *const kScaleNames[kScaleCount];

int PitchCount(int root, int scale);                 // notes in C1..C6
int PitchMidi(int index, int root, int scale);       // clamps the index
int PitchIndexOfMidi(float midi, int root, int scale);   // nearest; a tie goes down
void NoteName(int midi, char *buf, int size);        // "C1", "Db2": flats, scientific octaves

}  // namespace orings

#endif  // ORINGS_SCALE_H_
