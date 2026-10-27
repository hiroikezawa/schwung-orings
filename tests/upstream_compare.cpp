// Drives Rings' DSP exactly as rings.cc FillBuffer() does (Strummer, then
// Part or StringSynthPart, one 24-frame block at a time) through every model,
// FX and polyphony with moving pots, chords, FM and strums, and writes every
// output sample to stdout. scripts/compare_upstream.sh builds it twice --
// against untouched upstream and against src/rings/mi (with patches/) -- and
// requires the two outputs to be byte-identical: the oRings patch changes
// nothing at its neutral setting.
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
#include <stdio.h>
#include <string.h>

#include "rings/dsp/part.h"
#include "rings/dsp/string_synth_part.h"
#include "rings/dsp/strummer.h"
#include "stmlib/utils/random.h"

using namespace rings;

static Part part;
static StringSynthPart synth;
static Strummer strummer;
static uint16_t reverb[32768];

int main() {
  float in[kMaxBlockSize] = {0}, out[kMaxBlockSize], aux[kMaxBlockSize];
  for (int egg = 0; egg < 2; ++egg) {
    for (int model = 0; model < 6; ++model) {
      for (int poly = 1; poly <= 4; ++poly) {
        memset(static_cast<void *>(&part), 0, sizeof(part));
        memset(static_cast<void *>(&synth), 0, sizeof(synth));
        memset(static_cast<void *>(&strummer), 0, sizeof(strummer));
        strummer.Init(0.01f, kSampleRate / kMaxBlockSize);
        part.Init(reverb);
        synth.Init(reverb);
        part.set_polyphony(poly);
        synth.set_polyphony(poly);
        part.set_model(static_cast<ResonatorModel>(model));
        synth.set_fx(static_cast<FxType>(model));
        for (int block = 0; block < 4000; ++block) {
          const float t = block / 4000.0f;
          PerformanceState ps;
          ps.internal_exciter = true;
          ps.internal_strum = false;
          ps.internal_note = false;
          ps.strum = block % 173 == 5;
          ps.note = static_cast<float>((block / 173 * 7) % 37) - 12.0f;
          ps.tonic = 24.0f + (block / 1000) * 5.0f;
          ps.fm = (block % 1000) < 500 ? 0.0f : 0.37f;
          ps.chord = (block / 300) % kNumChords;
          Patch patch;
          patch.structure = 0.05f + 0.9f * t;
          patch.brightness = 0.9f - 0.7f * t;
          patch.damping = 0.3f + 0.6f * (block % 997) / 997.0f;
          patch.position = 0.1f + 0.8f * t * t;
          strummer.Process(NULL, kMaxBlockSize, &ps);
          stmlib::Random::Seed(0x9E3779B9u ^ block);
          if (egg) synth.Process(ps, patch, in, out, aux, kMaxBlockSize);
          else part.Process(ps, patch, in, out, aux, kMaxBlockSize);
          fwrite(out, sizeof(out), 1, stdout);
          fwrite(aux, sizeof(aux), 1, stdout);
        }
      }
    }
  }
  return 0;
}
