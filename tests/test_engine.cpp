// oRings engine tests (run under ASan/UBSan by scripts/test.sh).
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "stmlib/utils/random.h"
#include "wrapper/orings.h"

using namespace orings;

static int g_failures = 0;
#define CHECK(cond, ...)                                  \
  do {                                                    \
    if (cond) {                                           \
      printf("ok   ");                                    \
    } else {                                              \
      printf("FAIL ");                                    \
      ++g_failures;                                       \
    }                                                     \
    printf(__VA_ARGS__);                                  \
    printf("\n");                                         \
  } while (0)

static const float kRate = 44100.0f;

// The engines are big (two reverb memories each): static, never on the stack.
static ORings g_a, g_b;

struct Audio {
  std::vector<float> l, r;
};

static void Note(ORings &e, int note, int velocity = 127) {
  const uint8_t m[3] = {0x90, static_cast<uint8_t>(note), static_cast<uint8_t>(velocity)};
  e.Midi(m, 3);
}
static void NoteOff(ORings &e, int note) {
  const uint8_t m[3] = {0x80, static_cast<uint8_t>(note), 0};
  e.Midi(m, 3);
}
static void Bend(ORings &e, int value) {  // -8192..8191
  const int v = value + 8192;
  const uint8_t m[3] = {0xE0, static_cast<uint8_t>(v & 0x7F), static_cast<uint8_t>(v >> 7)};
  e.Midi(m, 3);
}
static void Set(ORings &e, const char *key, const char *value) {
  if (!e.SetByKey(key, value)) {
    printf("FAIL set %s=%s refused\n", key, value);
    ++g_failures;
  }
}

static Audio Render(ORings &e, float seconds, Audio *append = NULL) {
  Audio a;
  Audio &out = append ? *append : a;
  const size_t n = static_cast<size_t>(seconds * kRate);
  float l[128], r[128];
  for (size_t done = 0; done < n;) {
    const size_t k = n - done < 128 ? n - done : 128;
    e.Process(l, r, k);
    out.l.insert(out.l.end(), l, l + k);
    out.r.insert(out.r.end(), r, r + k);
    done += k;
  }
  return a;
}

static double Rms(const std::vector<float> &x, size_t from = 0, size_t to = 0) {
  if (!to || to > x.size()) to = x.size();
  double s = 0;
  for (size_t i = from; i < to; ++i) s += x[i] * x[i];
  return to > from ? sqrt(s / (to - from)) : 0.0;
}

static double Peak(const std::vector<float> &x) {
  double p = 0;
  for (float v : x) p = fmax(p, fabs(v));
  return p;
}

static bool AllFinite(const Audio &a) {
  for (size_t i = 0; i < a.l.size(); ++i)
    if (!isfinite(a.l[i]) || !isfinite(a.r[i])) return false;
  return true;
}

// Goertzel power of one frequency over [from, to).
static double Tone(const std::vector<float> &x, double hz, size_t from, size_t to) {
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
static double Pitch(const std::vector<float> &x, size_t from, size_t len, double lo, double hi) {
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

static double Cents(double f, double ref) { return 1200.0 * log2(f / ref); }
static double MidiHz(double n) { return 440.0 * pow(2.0, (n - 69.0) / 12.0); }

static double Correlation(const std::vector<float> &a, const std::vector<float> &b) {
  double ab = 0, aa = 0, bb = 0;
  for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
    ab += a[i] * b[i];
    aa += a[i] * a[i];
    bb += b[i] * b[i];
  }
  return ab / sqrt(aa * bb + 1e-30);
}

// Spectral centroid (Hz) by a coarse DFT over 2048 samples.
static double Centroid(const std::vector<float> &x, size_t from) {
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
static double Sustain(const std::vector<float> &x) {
  return Rms(x, 88200, 132300) / (Rms(x, 0, 22050) + 1e-12);
}

// Seconds until the 10 ms RMS falls 40 dB under its peak.
static double DecayTime(const std::vector<float> &x) {
  const size_t w = 441;
  double peak = 0;
  for (size_t i = 0; i + w <= x.size(); i += w) peak = fmax(peak, Rms(x, i, i + w));
  size_t last = 0;
  for (size_t i = 0; i + w <= x.size(); i += w)
    if (Rms(x, i, i + w) > peak * 0.01) last = i;
  return last / kRate;
}

static void Fresh(ORings &e) { e.Init(); }

// ---------------------------------------------------------------------------
// 1. The wrapper adds nothing: ORings::RenderBlock against rings.cc's own
//    FillBuffer sequence (Strummer, then Part or StringSynthPart) driven by
//    hand with the same PerformanceState and Patch, bit for bit.
// ---------------------------------------------------------------------------
struct Hardware {
  rings::Part part;
  rings::StringSynthPart synth;
  rings::Strummer strummer;
  uint16_t reverb_a[32768], reverb_b[32768];
};
static Hardware g_hw;

static void TestReference() {
  const int kNotes[] = {48, 55, 60, 43, 67, 60, 60, 36};
  int identical = 0, models = 0;
  for (int model = 0; model < kNumModels; ++model) {
    for (int poly = 1; poly <= 4; ++poly) {
      // The wrapper at its defaults (pots constant: the low-pass is idle),
      // MODEL and POLYPHONY applied the way ui.cc applies them at boot.
      ORings &e = g_a;
      Fresh(e);
      char text[16];
      snprintf(text, sizeof(text), "%d", model);
      Set(e, "model", text);
      snprintf(text, sizeof(text), "%d", poly - 1);
      Set(e, "polyphony", text);
      e.ApplyNow();

      Hardware &h = g_hw;
      memset(static_cast<void *>(&h), 0, sizeof(h));
      h.strummer.Init(0.01f, rings::kSampleRate / rings::kMaxBlockSize);
      h.part.Init(h.reverb_a);
      h.synth.Init(h.reverb_b);
      const ModelDef &md = kModels[model];
      // ui.cc at boot applies the stored polyphony (the default, 2 here),
      // then the user picks another: the same two calls the wrapper made.
      h.part.set_polyphony(2);
      h.synth.set_polyphony(2);
      h.part.set_polyphony(poly);
      h.synth.set_polyphony(poly);
      if (md.string_synth) h.synth.set_fx(static_cast<rings::FxType>(md.index));
      else h.part.set_model(static_cast<rings::ResonatorModel>(md.index));

      rings::Patch patch;
      patch.structure = kParams[P_STRUCTURE].def;
      patch.brightness = kParams[P_BRIGHTNESS].def;
      patch.damping = kParams[P_DAMPING].def;
      patch.position = kParams[P_POSITION].def;
      float in[kBlock] = {0};
      bool same = true;
      float note = 60 - 36;
      int next = 0, since = 1000;
      for (int block = 0; block < 6000 && same; ++block) {
        // Same schedule into both: a note every 300 blocks (150 ms).
        bool strum = false;
        if (block % 300 == 10 && next < 8) {
          Note(e, kNotes[next]);
        }
        rings::PerformanceState ps;
        ps.internal_exciter = true;
        ps.internal_strum = false;
        ps.internal_note = false;
        if (block % 300 == 10 && next < 8 && since >= kMinStrumBlocks) {
          note = kNotes[next++] - 36.0f;
          strum = true;
          since = 0;
        } else {
          ++since;
        }
        ps.strum = strum;
        ps.note = note;
        ps.tonic = 36.0f;
        ps.fm = 0.0f;
        ps.chord = 2;  // STRUCTURE 0.25 -> chord 2.5 -> 2 (hysteresis from 0)
        h.strummer.Process(NULL, kBlock, &ps);
        float ho[kBlock], ha[kBlock], eo[kBlock], ea[kBlock];
        // stmlib's RNG is one global: both sides draw the same noise.
        stmlib::Random::Seed(0x1234567u + block);
        if (md.string_synth) h.synth.Process(ps, patch, in, ho, ha, kBlock);
        else h.part.Process(ps, patch, in, ho, ha, kBlock);
        stmlib::Random::Seed(0x1234567u + block);
        e.RenderBlock(eo, ea);
        same = !memcmp(ho, eo, sizeof(ho)) && !memcmp(ha, ea, sizeof(ha));
        if (!same) printf("     model %d poly %d differs at block %d (%.9g/%.9g vs %.9g/%.9g)\n", model, poly, block, ho[0], ha[0], eo[0], ea[0]);
      }
      ++models;
      if (same) ++identical;
    }
  }
  CHECK(identical == models, "reference: %d/%d model x polyphony runs bit-identical to hand-driven Rings DSP",
        identical, models);
  CHECK(g_a.chord() == 2, "reference: STRUCTURE 0.25 is chord %d (2 = sus4)", g_a.chord());
}

// ---------------------------------------------------------------------------
static void TestEveryModelSounds() {
  int ok = 0;
  for (int m = 0; m < kNumModels; ++m) {
    ORings &e = g_a;
    Fresh(e);
    char t[16];
    snprintf(t, sizeof(t), "%d", m);
    Set(e, "model", t);
    Render(e, 0.05f);
    Note(e, 60);
    Audio a = Render(e, 1.0f);
    const double rms = fmax(Rms(a.l, 0, 22050), Rms(a.r, 0, 22050));
    const bool good = rms > 0.003 && AllFinite(a) && Peak(a.l) < 1.2 && Peak(a.r) < 1.2;
    if (!good) printf("     model %s: rms %.4f peak %.3f/%.3f\n", kModelNames[m], rms, Peak(a.l), Peak(a.r));
    ok += good;
  }
  CHECK(ok == kNumModels, "models: %d/%d sound after a note-on, finite, below full scale", ok, kNumModels);
}

// 2-op FM, ratio 1:1 (lut_fm_frequency_quantizer is 0 semitones at 30/128),
// FM index 0, long decay, mono: a sine at the note. Pots settled.
static void FmSine(ORings &e) {
  Fresh(e);
  Set(e, "model", "FM");
  Set(e, "ratio", "0.234");
  Set(e, "fm_index", "0");
  Set(e, "decay", "0.85");
  Set(e, "output", "SUM");
  Render(e, 0.6f);
}

static void TestPitch() {
  // 2-op FM with BRIGHTNESS (FM index) 0 is a pure sine at the note: the
  // cleanest probe of the whole pitch path (MIDI -> V/OCT -> tonic -> Rings
  // frequency -> 48 k -> 44.1 k).
  double worst = 0;
  for (int note = 24; note <= 96; note += 12) {
    ORings &e = g_a;
    Fresh(e);
    FmSine(e);
    Note(e, note);
    Audio a = Render(e, 0.6f);
    const double f = Pitch(a.l, 8820, 8820, MidiHz(note) * 0.8, MidiHz(note) * 1.25);
    const double c = Cents(f, MidiHz(note));
    worst = fmax(worst, fabs(c));
    if (fabs(c) > 2) printf("     FM note %d: %.2f Hz (%.2f cents)\n", note, f, c);
  }
  CHECK(worst < 2.0, "pitch: FM sine C1..C7 within %.3f cents (limit 2)", worst);

  // The physical models across C1..C7: in tune, stable, finite.
  const char *models[] = {"STRNG", "MODAL", "KVERB"};
  for (const char *m : models) {
    double w = 0;
    bool finite = true;
    for (int note = 24; note <= 96; note += 12) {
      ORings &e = g_a;
      Fresh(e);
      Set(e, "model", m);
      Set(e, "structure", "0.25");  // string: no dispersion; modal: harmonic-ish
      Set(e, "brightness", "0.3");
      Set(e, "damping", "0.8");
      Set(e, "position", "0.25");
      Set(e, "output", "SUM");
      Note(e, note);
      Audio a = Render(e, 0.8f);
      finite = finite && AllFinite(a) && Peak(a.l) < 1.2;
      const double f = Pitch(a.l, 13230, 8820, MidiHz(note) * 0.8, MidiHz(note) * 1.25);
      const double c = f > 0 ? Cents(f, MidiHz(note)) : 9999;
      if (fabs(c) > 20) printf("     %s note %d: %.2f Hz (%.1f cents)\n", m, note, f, c);
      w = fmax(w, fabs(c));
    }
    if (!strcmp(m, "MODAL"))
      CHECK(finite, "pitch: %s C1..C7 finite and bounded (worst fundamental offset %.1f cents, "
            "modes are inharmonic by design)", m, w);
    else
      CHECK(finite && w < 20.0, "pitch: %s C1..C7 within %.1f cents (limit 20), finite", m, w);
  }

  // FREQUENCY transposes, bend bends, FINE detunes.
  {
    ORings &e = g_a;
    FmSine(e);
    Set(e, "frequency", "12");
    Note(e, 57);
    Audio a = Render(e, 0.5f);
    const double f = Pitch(a.l, 8820, 8820, 360, 560);
    CHECK(fabs(Cents(f, 440.0)) < 2, "pitch: FREQUENCY +12 plays note A3 at %.2f Hz (440 expected)", f);
  }
  {
    ORings &e = g_a;
    FmSine(e);
    Note(e, 69);
    Render(e, 0.1f);
    Bend(e, 8191);
    Audio a = Render(e, 0.5f);
    const double f = Pitch(a.l, 13230, 4410, 400, 560);
    CHECK(fabs(Cents(f, 440.0) - 200.0 * 8191 / 8192) < 3, "pitch: full bend, range 2 -> %.1f cents",
          Cents(f, 440.0));
    Bend(e, 0);
    Set(e, "fine", "50");
    Audio b = Render(e, 0.5f);
    const double g = Pitch(b.l, 13230, 4410, 400, 560);
    CHECK(fabs(Cents(g, 440.0) - 50.0) < 3, "pitch: FINE +50 -> %.1f cents", Cents(g, 440.0));
  }
}

static void TestStrums() {
  // The same note again: every note-on strums.
  {
    ORings &e = g_a;
    Fresh(e);
    Set(e, "model", "STRNG");
    for (int i = 0; i < 12; ++i) {
      Note(e, 60);
      Render(e, 0.03f);
    }
    Render(e, 0.1f);
    CHECK(e.strums() == 12 && e.swallowed() == 0, "strum: 12 repeats of one note 30 ms apart -> %u strums, %u swallowed",
          e.strums(), e.swallowed());
  }
  // A chord arriving at once: each note is its own strum, 10.5 ms apart.
  {
    ORings &e = g_a;
    Fresh(e);
    Set(e, "polyphony", "3");
    Note(e, 60);
    Note(e, 64);
    Note(e, 67);
    Note(e, 71);
    Render(e, 0.2f);
    CHECK(e.strums() == 4 && e.swallowed() == 0 && e.pending() == 0,
          "strum: a 4-note chord at once -> %u strums, %u swallowed", e.strums(), e.swallowed());
  }
  // Note-off does nothing to the sound (no gate in Rings): the same run with
  // and without one, from the same RNG seed, is sample-identical.
  {
    Audio a, b;
    for (int pass = 0; pass < 2; ++pass) {
      ORings &e = g_a;
      Fresh(e);
      stmlib::Random::Seed(42);
      Set(e, "model", "SYMPA");
      Set(e, "output", "SUM");
      Note(e, 52);
      Audio &x = pass ? b : a;
      Render(e, 0.2f, &x);
      if (pass == 0) NoteOff(e, 52);
      Render(e, 0.8f, &x);
    }
    CHECK(a.l == b.l && Rms(a.l, 22050) > 0.005, "note-off: changes nothing (sample-identical, still ringing at %.4f)",
          Rms(a.l, 22050));
  }
}

static void TestPolyphony() {
  // FM sines, four notes. With 4 voices all four keep ringing; with 1 only
  // the last one does.
  const int notes[] = {60, 64, 67, 71};
  double kept[5] = {0};
  for (int poly = 1; poly <= 4; poly *= 2) {
    ORings &e = g_a;
    FmSine(e);
    char t[16];
    snprintf(t, sizeof(t), "%d", poly - 1);
    Set(e, "polyphony", t);
    Render(e, 0.05f);
    for (int n : notes) {
      Note(e, n);
      Render(e, 0.05f);
    }
    Audio a = Render(e, 0.5f);
    const double first = Tone(a.l, MidiHz(60), 2205, 13230);
    const double last = Tone(a.l, MidiHz(71), 2205, 13230);
    kept[poly] = first / last;
  }
  CHECK(kept[4] > 0.3 && kept[1] < 0.02, "polyphony: first note's level vs last: 4 voices %.3f, 1 voice %.4f",
        kept[4], kept[1]);
  // 2 voices: alternate between ODD and EVEN (part.cc dispatch).
  {
    ORings &e = g_a;
    FmSine(e);
    Set(e, "output", "STEREO");
    Set(e, "polyphony", "1");
    Render(e, 0.05f);
    Note(e, 60);
    Render(e, 0.05f);
    Note(e, 67);
    Audio a = Render(e, 0.5f);
    // part.cc: a strum moves to the next voice first, so the first note lands
    // on voice 1 (EVEN, right) and the second on voice 0 (ODD, left).
    const double c_l = Tone(a.l, MidiHz(60), 4410, 13230), c_r = Tone(a.r, MidiHz(60), 4410, 13230);
    const double g_l = Tone(a.l, MidiHz(67), 4410, 13230), g_r = Tone(a.r, MidiHz(67), 4410, 13230);
    CHECK(c_r > 20 * c_l && g_l > 20 * g_r, "odd/even: 2 voices alternate EVEN (R) / ODD (L): C %.4f/%.4f, G %.4f/%.4f",
          c_l, c_r, g_l, g_r);
  }
  // 3 voices (the hardware's long-press polyphony) runs.
  {
    ORings &e = g_a;
    Fresh(e);
    Set(e, "polyphony", "2");
    for (int n : notes) {
      Note(e, n);
      Render(e, 0.05f);
    }
    Audio a = Render(e, 0.3f);
    CHECK(AllFinite(a) && Rms(a.l) > 0.005, "polyphony: 3 voices sound (rms %.4f)", Rms(a.l));
  }
}

static Audio Struck(const char *model, const char *key, const char *value, int note = 48, float secs = 1.0f) {
  ORings &e = g_a;
  Fresh(e);
  Set(e, "model", model);
  Set(e, "polyphony", "0");
  Set(e, "output", "SUM");
  if (key) Set(e, key, value);
  Render(e, 0.2f);  // the pots' glide settles
  Note(e, note);
  return Render(e, secs);
}

static void TestKnobs() {
  const char *models[] = {"MODAL", "SYMPA", "STRNG"};
  for (const char *m : models) {
    // STRUCTURE changes the sound.
    Audio s0 = Struck(m, "structure", "0.1"), s1 = Struck(m, "structure", "0.8");
    const double cs = Correlation(s0.l, s1.l);
    // BRIGHTNESS raises the spectral centroid.
    Audio b0 = Struck(m, "brightness", "0.15"), b1 = Struck(m, "brightness", "0.9");
    const double c0 = Centroid(b0.l, 2205), c1 = Centroid(b1.l, 2205);
    // DAMPING lengthens the decay.
    Audio d0 = Struck(m, "damping", "0.2", 48, 4.0f), d1 = Struck(m, "damping", "0.8", 48, 4.0f);
    const double t0 = Sustain(d0.l), t1 = Sustain(d1.l);
    // POSITION changes the harmonic content.
    Audio p0 = Struck(m, "position", "0.05"), p1 = Struck(m, "position", "0.5");
    const double cp = Correlation(p0.l, p1.l);
    CHECK(fabs(cs) < 0.9 && c1 > 1.3 * c0 && t1 > 2 * t0 && fabs(cp) < 0.95,
          "knobs %s: STRUCT corr %.2f, BRIGHT centroid %.0f->%.0f Hz, DAMP sustain %.4f->%.4f, POS corr %.2f",
          m, cs, c0, c1, t0, t1, cp);
  }
  // 2-op FM: RATIO, INDEX, DECAY, FEEDBACK all audible.
  {
    Audio r0 = Struck("FM", "ratio", "0.2"), r1 = Struck("FM", "ratio", "0.7");
    Audio i0 = Struck("FM", "fm_index", "0.1"), i1 = Struck("FM", "fm_index", "0.9");
    Audio d0 = Struck("FM", "decay", "0.2", 48, 4.0f), d1 = Struck("FM", "decay", "0.7", 48, 4.0f);
    Audio f0 = Struck("FM", "feedback", "0.5"), f1 = Struck("FM", "feedback", "0.95");
    CHECK(fabs(Correlation(r0.l, r1.l)) < 0.9 && Centroid(i1.l, 2205) > 1.3 * Centroid(i0.l, 2205) &&
          DecayTime(d1.l) > 1.5 * DecayTime(d0.l) && fabs(Correlation(f0.l, f1.l)) < 0.95,
          "knobs FM: RATIO, INDEX, DECAY, FEEDBACK each change the sound");
  }
  // Western Chords / Disastrous Peace: CHORD picks different notes.
  {
    Audio a = Struck("CHORD", "chord", "M"), b = Struck("CHORD", "chord", "m");
    const double cc = Correlation(a.l, b.l);
    Audio c = Struck("DP-EN", "chord", "M", 48, 1.5f), d = Struck("DP-EN", "chord", "m7", 48, 1.5f);
    const double cd = Correlation(c.l, d.l);
    CHECK(fabs(cc) < 0.9 && fabs(cd) < 0.9, "knobs CHORD: chord M vs m corr %.2f (Western Chords), M vs m7 %.2f (DP)",
          cc, cd);
  }
  // Disastrous Peace: REGISTRATION, ENVELOPE, effect amount.
  {
    Audio r0 = Struck("DP-CH", "registration", "0.1", 48, 1.5f), r1 = Struck("DP-CH", "registration", "0.9", 48, 1.5f);
    Audio e0 = Struck("DP-CH", "envelope", "0.2", 48, 4.0f), e1 = Struck("DP-CH", "envelope", "0.7", 48, 4.0f);
    Audio x0 = Struck("DP-RV", "reverb", "0.0", 48, 1.5f), x1 = Struck("DP-RV", "reverb", "1.0", 48, 1.5f);
    CHECK(Centroid(r1.l, 4410) > 1.3 * Centroid(r0.l, 4410) && DecayTime(e1.l) > 1.5 * DecayTime(e0.l) &&
          fabs(Correlation(x0.l, x1.l)) < 0.95,
          "knobs DP: REGISTRATION centroid %.0f->%.0f Hz, ENVELOPE %.2f->%.2f s, REVERB corr %.2f",
          Centroid(r0.l, 4410), Centroid(r1.l, 4410), DecayTime(e0.l), DecayTime(e1.l), Correlation(x0.l, x1.l));
  }
  // The six Disastrous Peace effects are six different sounds: the relative
  // difference of every pair, effect amount at maximum.
  {
    const char *fx[] = {"DP-FM", "DP-CH", "DP-RV", "DP-F2", "DP-EN", "DP-R2"};
    Audio a[6];
    for (int i = 0; i < 6; ++i) a[i] = Struck(fx[i], "position", "1.0", 48, 1.5f);
    double least = 1e9;
    for (int i = 0; i < 6; ++i)
      for (int j = i + 1; j < 6; ++j) {
        double d = 0, ei = 0, ej = 0;
        for (size_t k = 0; k < a[i].l.size(); ++k) {
          d += (a[i].l[k] - a[j].l[k]) * (a[i].l[k] - a[j].l[k]);
          ei += a[i].l[k] * a[i].l[k];
          ej += a[j].l[k] * a[j].l[k];
        }
        least = fmin(least, sqrt(d / sqrt(ei * ej)));
      }
    CHECK(least > 0.25, "DP: the six effects differ pairwise (smallest relative difference %.2f)", least);
  }
  // Karplusverb: DECAY+VERB lengthens the tail.
  {
    Audio d0 = Struck("KVERB", "decay_verb", "0.2", 48, 4.0f), d1 = Struck("KVERB", "decay_verb", "0.8", 48, 4.0f);
    CHECK(DecayTime(d1.l) > 1.5 * DecayTime(d0.l), "knobs KVERB: DECAY+VERB %.2f -> %.2f s", DecayTime(d0.l),
          DecayTime(d1.l));
  }
}

static void TestOutputs() {
  Audio st = Struck("MODAL", "output", "STEREO");
  Audio sum = Struck("MODAL", "output", "SUM");
  Audio odd = Struck("MODAL", "output", "ODD");
  Audio even = Struck("MODAL", "output", "EVEN");
  CHECK(fabs(Correlation(st.l, st.r)) < 0.98 && Rms(st.r) > 0.01,
        "odd/even: STEREO carries two different signals (corr %.2f)", Correlation(st.l, st.r));
  bool mono = true;
  for (size_t i = 0; i < sum.l.size(); ++i)
    mono = mono && sum.l[i] == sum.r[i] && odd.l[i] == odd.r[i] && even.l[i] == even.r[i];
  CHECK(mono, "output: SUM, ODD and EVEN are mono");
  CHECK(Rms(odd.l) > 0.01 && Rms(even.l) > 0.01 && fabs(Correlation(odd.l, even.l)) < 0.98,
        "output: ODD and EVEN are the two Rings outputs");
  // Level and gain.
  Audio quiet = Struck("MODAL", "level", "0.1"), loud = Struck("MODAL", "level", "0.7");
  CHECK(Rms(loud.l) > 5 * Rms(quiet.l), "output: LEVEL 0.1 -> 0.7 is %.1fx", Rms(loud.l) / Rms(quiet.l));
  Audio g0 = Struck("MODAL", "gain", "-12");
  CHECK(fabs(20 * log10(Rms(sum.l) / Rms(g0.l)) - 12) < 0.5, "output: GAIN -12 dB -> %.2f dB",
        20 * log10(Rms(g0.l) / Rms(sum.l)));
}

static void TestExciterAndVelocity() {
  for (const char *m : {"MODAL", "STRNG", "FM", "DP-EN"}) {
    ORings &e = g_a;
    Fresh(e);
    Set(e, "model", m);
    Set(e, "velocity", "100");
    Set(e, "output", "SUM");
    Note(e, 48, 127);
    Audio hi = Render(e, 0.6f);
    Fresh(e);
    Set(e, "model", m);
    Set(e, "velocity", "100");
    Set(e, "output", "SUM");
    Note(e, 48, 32);
    Audio lo = Render(e, 0.6f);
    Fresh(e);
    Set(e, "model", m);
    Set(e, "exciter", "0");
    Set(e, "output", "SUM");
    Note(e, 48, 127);
    Audio none = Render(e, 0.6f);
    CHECK(Rms(hi.l) > 2 * Rms(lo.l) && Rms(none.l) < 1e-4,
          "velocity %s: vel 127 / 32 at response 100 = %.1fx; EXCITER 0 rms %.6f", m, Rms(hi.l) / Rms(lo.l),
          Rms(none.l));
  }
}

static void TestStateAndKeys() {
  ORings &e = g_a;
  Fresh(e);
  Set(e, "model", "DP-RV");
  Set(e, "polyphony", "3");
  Set(e, "chord", "m9");
  Set(e, "registration", "0.3");
  Set(e, "envelope", "0.4");
  Set(e, "reverb", "0.6");
  Set(e, "frequency", "-7");
  Set(e, "fine", "13");
  Set(e, "gain", "-3");
  Set(e, "output", "SUM");
  char a[1024], b[1024];
  CHECK(e.WriteState(a, sizeof(a)) > 0, "state: written (%s)", a);
  Fresh(g_b);
  CHECK(g_b.ReadState(a), "state: read back");
  g_b.WriteState(b, sizeof(b));
  CHECK(!strcmp(a, b), "state: round trip identical");
  char v[64];
  g_b.GetByKey("chord", v, sizeof(v));
  CHECK(!strcmp(v, "5"), "keys: chord after restore = %s (m9 = 5)", v);
  g_b.GetByKey("model", v, sizeof(v));
  CHECK(!strcmp(v, "8"), "keys: model reads as index %s", v);
  g_b.GetByKey("vowel", v, sizeof(v));
  CHECK(!strcmp(v, "0.6000"), "keys: an alias reads its stored value (vowel = %s)", v);
  CHECK(!e.SetByKey("structure", "nan") && !e.SetByKey("nope", "1") && !e.SetByKey("model", "WAT"),
        "keys: NaN, unknown key and unknown option refused");
  e.SetByKey("structure", "7");
  e.GetByKey("structure", v, sizeof(v));
  CHECK(!strcmp(v, "1.0000"), "keys: out of range clamps (%s)", v);
  // An old/partial state returns missing keys to their defaults.
  Fresh(g_b);
  g_b.ReadState("{\"v\":1,\"model\":4}");
  g_b.GetByKey("level", v, sizeof(v));
  CHECK(g_b.model() == 4 && !strcmp(v, "0.7000"), "state: partial state -> model %d, level %s", g_b.model(), v);
}

static void TestResetAndSwitch() {
  ORings &e = g_a;
  Fresh(e);
  Set(e, "model", "KVERB");
  Set(e, "output", "SUM");
  Set(e, "decay_verb", "0.95");
  Note(e, 48);
  Audio a = Render(e, 0.5f);
  Set(e, "reset", "RESET");
  Audio b = Render(e, 0.2f);
  CHECK(Rms(a.l, 11025) > 0.01 && Rms(b.l, 4410) < 1e-5, "reset: a ringing Karplusverb is silent after RESET (%.6f)",
        Rms(b.l, 4410));

  // Switching MODEL while ringing: no step larger than the signal's own.
  Fresh(e);
  Set(e, "model", "MODAL");
  Set(e, "output", "SUM");
  Set(e, "damping", "0.9");
  Render(e, 0.2f);
  Note(e, 40);
  Audio c = Render(e, 0.3f);
  double before = 0;
  for (size_t i = 1; i < c.l.size(); ++i) before = fmax(before, fabs(c.l[i] - c.l[i - 1]));
  const size_t at = c.l.size();
  Set(e, "model", "SYMPA");
  Render(e, 0.05f, &c);
  double during = 0;
  for (size_t i = at; i < c.l.size(); ++i) during = fmax(during, fabs(c.l[i] - c.l[i - 1]));
  CHECK(during <= before, "switch: MODAL->SYMPA while ringing, largest step %.4f (signal's own %.4f)", during, before);

  // All Sound Off.
  Fresh(e);
  Note(e, 60);
  Render(e, 0.2f);
  const uint8_t aso[3] = {0xB0, 120, 0};
  e.Midi(aso, 3);
  Audio d = Render(e, 0.1f);
  CHECK(Rms(d.l, 2205) < 1e-5, "midi: CC 120 silences");
}

static void TestSoak() {
  // 10 minutes of random notes and knob moves across every model and
  // polyphony: finite, bounded, no recoveries.
  ORings &e = g_a;
  Fresh(e);
  unsigned rng = 12345;
  auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; };
  double peak = 0;
  bool finite = true;
  float l[128], r[128];
  const int blocks = static_cast<int>(600 * kRate / 128);
  const char *knobs[] = {"structure", "brightness", "damping", "position"};
  for (int b = 0; b < blocks; ++b) {
    if (b % 2000 == 0) {
      char t[16];
      snprintf(t, sizeof(t), "%d", static_cast<int>(rnd() * kNumModels));
      e.SetByKey("model", t);
      snprintf(t, sizeof(t), "%d", static_cast<int>(rnd() * 4));
      e.SetByKey("polyphony", t);
    }
    if (rnd() < 0.08f) Note(e, 24 + static_cast<int>(rnd() * 73), 1 + static_cast<int>(rnd() * 126));
    if (rnd() < 0.05f) {
      char t[16];
      snprintf(t, sizeof(t), "%.3f", rnd());
      e.SetByKey(knobs[static_cast<int>(rnd() * 4)], t);
    }
    if (rnd() < 0.01f) Bend(e, static_cast<int>(rnd() * 16383) - 8192);
    e.Process(l, r, 128);
    for (int i = 0; i < 128; ++i) {
      finite = finite && isfinite(l[i]) && isfinite(r[i]);
      peak = fmax(peak, fmax(fabs(l[i]), fabs(r[i])));
    }
  }
  CHECK(finite && peak < 1.5 && e.recoveries() == 0 && e.swallowed() == 0,
        "soak: 10 min random play, all models: finite, peak %.3f, %u recoveries, %u strums (%u swallowed)", peak,
        e.recoveries(), e.strums(), e.swallowed());
}

int main() {
  TestReference();
  TestEveryModelSounds();
  TestPitch();
  TestStrums();
  TestPolyphony();
  TestKnobs();
  TestOutputs();
  TestExciterAndVelocity();
  TestStateAndKeys();
  TestResetAndSwitch();
  TestSoak();
  if (g_failures) {
    printf("%d FAILED\n", g_failures);
    return 1;
  }
  printf("ALL ENGINE TESTS PASSED\n");
  return 0;
}
