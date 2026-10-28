// Signal measurements shared by the oRings and oRings FX engine tests.
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
#ifndef ORINGS_TESTS_ANALYSIS_H_
#define ORINGS_TESTS_ANALYSIS_H_

#include <math.h>

#include <vector>

static const float kRate = 44100.0f;

inline double Rms(const std::vector<float> &x, size_t from = 0, size_t to = 0) {
  if (!to || to > x.size()) to = x.size();
  double s = 0;
  for (size_t i = from; i < to; ++i) s += x[i] * x[i];
  return to > from ? sqrt(s / (to - from)) : 0.0;
}

inline double Peak(const std::vector<float> &x) {
  double p = 0;
  for (float v : x) p = fmax(p, fabs(v));
  return p;
}

// Goertzel power of one frequency over [from, to).
inline double Tone(const std::vector<float> &x, double hz, size_t from, size_t to) {
  const double w = 2.0 * M_PI * hz / kRate;
  const double c = 2.0 * cos(w);
  double s1 = 0, s2 = 0;
  for (size_t i = from; i < to && i < x.size(); ++i) {
    const double s0 = x[i] + c * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return sqrt(fmax(0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (to - from);
}

// Fundamental by normalised autocorrelation (first peak above 0.8 of the
// best one, parabolic interpolation).
inline double Pitch(const std::vector<float> &x, size_t from, size_t len, double lo, double hi) {
  const int min_lag = static_cast<int>(kRate / hi), max_lag = static_cast<int>(kRate / lo) + 2;
  std::vector<double> r(max_lag + 2, 0.0);
  double best = 0;
  for (int lag = min_lag; lag <= max_lag + 1; ++lag) {
    double s = 0, e0 = 0, e1 = 0;
    for (size_t i = from; i < from + len; ++i) {
      s += x[i] * x[i + lag];
      e0 += x[i] * x[i];
      e1 += x[i + lag] * x[i + lag];
    }
    r[lag] = s / sqrt(e0 * e1 + 1e-30);
    best = fmax(best, r[lag]);
  }
  for (int lag = min_lag + 1; lag <= max_lag; ++lag) {
    if (r[lag] >= 0.8 * best && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1]) {
      const double a = r[lag - 1], b = r[lag], c = r[lag + 1];
      const double d = a - 2 * b + c;
      const double off = fabs(d) > 1e-12 ? 0.5 * (a - c) / d : 0.0;
      return kRate / (lag + off);
    }
  }
  return 0.0;
}

inline double Cents(double f, double ref) { return 1200.0 * log2(f / ref); }
inline double MidiHz(double n) { return 440.0 * pow(2.0, (n - 69.0) / 12.0); }

inline double Correlation(const std::vector<float> &a, const std::vector<float> &b) {
  double ab = 0, aa = 0, bb = 0;
  for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
    ab += a[i] * b[i];
    aa += a[i] * a[i];
    bb += b[i] * b[i];
  }
  return ab / sqrt(aa * bb + 1e-30);
}

// Spectral centroid (Hz) by a coarse DFT over 2048 samples.
inline double Centroid(const std::vector<float> &x, size_t from) {
  const int n = 2048;
  double num = 0, den = 0;
  for (int k = 1; k < n / 2; k += 2) {
    double re = 0, im = 0;
    for (int i = 0; i < n; ++i) {
      const double w = 0.5 - 0.5 * cos(2 * M_PI * i / n);
      re += w * x[from + i] * cos(2 * M_PI * k * i / n);
      im -= w * x[from + i] * sin(2 * M_PI * k * i / n);
    }
    const double m = sqrt(re * re + im * im);
    num += m * k * kRate / n;
    den += m;
  }
  return den > 0 ? num / den : 0;
}

// Late energy against early: RMS of 2-3 s over RMS of the first 0.5 s.
inline double Sustain(const std::vector<float> &x) {
  return Rms(x, 88200, 132300) / (Rms(x, 0, 22050) + 1e-12);
}

// Seconds until the 10 ms RMS falls 40 dB under its peak.
inline double DecayTime(const std::vector<float> &x) {
  const size_t w = 441;
  double peak = 0;
  for (size_t i = 0; i + w <= x.size(); i += w) peak = fmax(peak, Rms(x, i, i + w));
  size_t last = 0;
  for (size_t i = 0; i + w <= x.size(); i += w)
    if (Rms(x, i, i + w) > peak * 0.01) last = i;
  return last / kRate;
}

#endif  // ORINGS_TESTS_ANALYSIS_H_
