// oRings FX CPU bench: time Process() for one 128-frame Move block, per MODEL
// and polyphony, with a drum loop running through it (STRUM AUTO, MIX 50 %,
// exciter OFF and ON). Built static for the Move by scripts/build_bench.sh;
// run it there (BENCH=fx scripts/bench_move.sh).
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
#include <math.h>
#include <stdio.h>
#include <time.h>

#include "fx/orings_fx.h"

using namespace oringsfx;

static RingsFx e;

static double Now() {
  timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

// Kick on the beat, hat on the 8ths, 120 BPM, int16 stereo.
static int16_t Drum(long n) {
  const double t = n / 44100.0, tb = fmod(t, 0.5), t8 = fmod(t, 0.25);
  const double s = 0.6 * sin(2 * M_PI * 55 * tb) * exp(-tb * 10) + 0.15 * sin(2 * M_PI * 7919 * t8) * exp(-t8 * 150);
  return static_cast<int16_t>(s * 32767);
}

int main() {
  int16_t io[256];
  const int kBlocks = 3445;  // 10 s
  printf("model  poly exc  mean_us  p99_us  max_us  (budget 2902 us per block)\n");
  double worst_mean = 0;
  for (int m = 0; m < kNumModels; ++m) {
    for (int pi = 0; pi < 3; ++pi) {
      for (int exc = 0; exc < 2; ++exc) {
        if (exc && orings::kModels[m].string_synth) continue;  // no exciter there
        e.Init();
        char t[16];
        snprintf(t, sizeof(t), "%d", m);
        e.SetByKey("model", t);
        snprintf(t, sizeof(t), "%d", pi);
        e.SetByKey("polyphony", t);
        e.SetByKey("exciter", exc ? "1" : "0");
        e.Snap();
        static double us[kBlocks];
        long n = 0;
        for (int b = 0; b < kBlocks; ++b) {
          for (int i = 0; i < 128; ++i) io[2 * i] = io[2 * i + 1] = Drum(n++);
          const double t0 = Now();
          e.Process(io, 128);
          us[b] = Now() - t0;
        }
        double sum = 0, max = 0;
        for (int b = 0; b < kBlocks; ++b) {
          sum += us[b];
          if (us[b] > max) max = us[b];
        }
        double p99 = max;
        for (double cut = 0; cut < max; cut += 1.0) {
          int over = 0;
          for (int b = 0; b < kBlocks; ++b) over += us[b] > cut;
          if (over <= kBlocks / 100) {
            p99 = cut;
            break;
          }
        }
        const double mean = sum / kBlocks;
        if (mean > worst_mean) worst_mean = mean;
        printf("%-6s %4d %3s %8.1f %7.0f %7.0f\n", kModelNames[m], kPolyVoices[pi], exc ? "ON" : "OFF", mean, p99,
               max);
      }
    }
  }
  printf("worst mean %.1f us = %.1f %% of one block; engine %zu bytes\n", worst_mean, worst_mean / 2902.5 * 100,
         sizeof(RingsFx));
  return 0;
}
