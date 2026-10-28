// oRings CPU bench: time Process() for one 128-frame Move block, per MODEL and
// polyphony, with Move's transport running at 120 BPM: a strike on every 1/16
// (all 16 trigger pads on), a RANDOM phrase playing in PITCH and a recorded
// STRUCTURE lane playing back -- the module's busiest ordinary use. Built static for the Move
// by scripts/build_bench.sh; run it there (scripts/bench_move.sh).
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
#include <stdio.h>
#include <time.h>

#include "wrapper/orings.h"

using namespace orings;

static ORings e;

static double Now() {
  timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

int main() {
  float l[128], r[128];
  const int kBlocks = 3445;  // 10 s
  printf("model  poly  mean_us  p99_us  max_us  (budget 2902 us per block)\n");
  double worst_mean = 0;
  for (int m = 0; m < kNumModels; ++m) {
    for (int poly = 1; poly <= 4; poly *= 2) {
      e.Init();
      char t[16];
      snprintf(t, sizeof(t), "%d", m);
      e.SetByKey("model", t);
      snprintf(t, sizeof(t), "%d", poly - 1);
      e.SetByKey("polyphony", t);
      e.ApplyNow();
      e.Randomize();
      double beat = 0.0;
      // One bar of STRUCTURE motion, written by a held touch.
      e.SetWrite(true);
      for (int b = 0; b < 690; ++b) {
        e.SetParam(P_STRUCTURE, (b % 345) / 345.0f);
        e.Touch(P_STRUCTURE, true);
        e.Process(l, r, 128, beat, 120.0f, true);
        beat += 128.0 / 44100.0 * 2.0;
      }
      e.Touch(P_STRUCTURE, false);
      e.SetWrite(false);
      static double us[kBlocks];
      for (int b = 0; b < kBlocks; ++b) {
        const double t0 = Now();
        e.Process(l, r, 128, beat, 120.0f, true);
        us[b] = Now() - t0;
        beat += 128.0 / 44100.0 * 2.0;
      }
      double sum = 0, max = 0;
      for (int b = 0; b < kBlocks; ++b) {
        sum += us[b];
        if (us[b] > max) max = us[b];
      }
      // p99 by counting.
      double p99 = max;
      for (double cut = 0; cut < max; cut += 1.0) {
        int over = 0;
        for (int b = 0; b < kBlocks; ++b) over += us[b] > cut;
        if (over <= kBlocks / 100) { p99 = cut; break; }
      }
      const double mean = sum / kBlocks;
      if (mean > worst_mean) worst_mean = mean;
      printf("%-6s %4d %8.1f %7.0f %7.0f\n", kModelNames[m], poly, mean, p99, max);
    }
  }
  printf("worst mean %.1f us = %.1f %% of one block\n", worst_mean, worst_mean / 2902.5 * 100);
  return 0;
}
