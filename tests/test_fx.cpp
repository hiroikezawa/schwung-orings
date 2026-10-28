// oRings FX engine tests (run under ASan/UBSan by scripts/test.sh).
// Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "analysis.h"
#include "fx/orings_fx.h"
#include "stmlib/utils/random.h"

using namespace oringsfx;

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

// The engines are big (two reverb memories each): static, never on the stack.
static RingsFx g_a, g_b;

struct Audio {
  std::vector<float> l, r;
};

static bool AllFinite(const Audio &a) {
  for (size_t i = 0; i < a.l.size(); ++i)
    if (!isfinite(a.l[i]) || !isfinite(a.r[i])) return false;
  return true;
}

static void Set(RingsFx &e, const char *key, const char *value) {
  if (!e.SetByKey(key, value)) {
    printf("FAIL set %s=%s refused\n", key, value);
    ++g_failures;
  }
}

static void Fresh(RingsFx &e, const char *model = "MODAL") {
  e.Init();
  Set(e, "model", model);
  Set(e, "mix", "1");
  e.Snap();
}

static void NoteOn(RingsFx &e) {
  const uint8_t m[3] = {0x90, 60, 100};
  e.Midi(m, 3);
}

// ---------------------------------------------------------------------------
// Test signals, deterministic. A drum machine at 120 BPM: kick on the beat,
// snare on 2 and 4, closed hat on every 8th.
// ---------------------------------------------------------------------------
struct Drums {
  double rate;
  uint32_t rng = 1;
  float Noise() {
    rng = rng * 1664525u + 1013904223u;
    return static_cast<int32_t>(rng) * (1.0f / 2147483648.0f);
  }
  float hp_ = 0, hp_x_ = 0;
  // Sample n of the loop; `level` scales everything; mask: 1 kick 2 snare 4 hat.
  float At(size_t n, float level = 0.5f, int mask = 7) {
    const double t = n / rate;
    const double beat = 0.5;
    const double tb = fmod(t, beat), t8 = fmod(t, beat / 2);
    const int beat_index = static_cast<int>(t / beat) % 4;
    float s = 0;
    if (mask & 1) {  // kick: 150 -> 50 Hz sweep, 200 ms
      const double f = 50 + 100 * exp(-tb * 30);
      s += 0.9f * static_cast<float>(sin(2 * M_PI * (50 * tb + 100 / 30.0 * (1 - exp(-tb * 30)))) * exp(-tb * 12));
      (void)f;
    }
    const float noise = Noise();
    if ((mask & 2) && (beat_index & 1)) {  // snare: 190 Hz + noise
      s += static_cast<float>((0.4 * sin(2 * M_PI * 190 * tb) + 0.5 * noise) * exp(-tb * 25));
    }
    if (mask & 4) {  // closed hat: high-passed noise, 30 ms
      const float x = noise;
      hp_ = 0.6f * (hp_ + x - hp_x_);
      hp_x_ = x;
      s += static_cast<float>(0.35 * hp_ * exp(-t8 * 120));
    }
    return s * level;
  }
};

// Runs the engine on mono input `x` (both channels), 128 frames at a time.
static Audio Run(RingsFx &e, const std::vector<float> &x) {
  Audio a;
  a.l.resize(x.size());
  a.r.resize(x.size());
  for (size_t done = 0; done < x.size(); done += 128) {
    const int k = static_cast<int>(x.size() - done < 128 ? x.size() - done : 128);
    e.ProcessFloat(&x[done], &x[done], &a.l[done], &a.r[done], k);
  }
  return a;
}

static std::vector<float> DrumLoop(float seconds, float level = 0.5f, int mask = 7) {
  Drums d;
  d.rate = kRate;
  std::vector<float> x(static_cast<size_t>(seconds * kRate));
  for (size_t i = 0; i < x.size(); ++i) x[i] = d.At(i, level, mask);
  return x;
}

// One hit (an impulse-like click: 2 ms noise burst) at t = 0, then silence.
static std::vector<float> Hit(float seconds, float level = 0.5f) {
  std::vector<float> x(static_cast<size_t>(seconds * kRate), 0.0f);
  uint32_t rng = 7;
  for (int i = 0; i < 88; ++i) {
    rng = rng * 1664525u + 1013904223u;
    x[i] = level * static_cast<int32_t>(rng) * (1.0f / 2147483648.0f) * (1.0f - i / 88.0f);
  }
  return x;
}

static std::vector<float> Silence(float seconds) {
  return std::vector<float>(static_cast<size_t>(seconds * kRate), 0.0f);
}

// ---------------------------------------------------------------------------
// 1. The wrapper adds nothing: RingsFx::RenderBlock against rings.cc's own
//    FillBuffer with a cable in the IN jack (noise gate, Strummer on the
//    input, Part / StringSynthPart), driven by hand, bit for bit.
// ---------------------------------------------------------------------------
struct Hardware {
  rings::Part part;
  rings::StringSynthPart synth;
  rings::Strummer strummer;
  uint16_t reverb_a[32768], reverb_b[32768];
  float in_level;
};
static Hardware g_hw;

static void TestReference() {
  // strum: 0 AUTO (STRUM unpatched: the onset detector), 1 MIDI (STRUM
  // patched); exciter: what Part is told after the strummer.
  struct Case { int strum; bool exciter; bool part_only; const char *what; };
  const Case cases[] = {
    {0, false, true, "IN patched, STRUM by onset (hardware)"},
    {1, false, false, "IN + STRUM patched (hardware)"},
    {0, true, true, "STRUM by onset + internal exciter"},
  };
  const int kPoly[] = {1, 2, 4};
  for (const Case &c : cases) {
    int identical = 0, runs = 0;
    uint32_t total_strums = 0;
    for (int model = 0; model < kNumModels; ++model) {
      const bool dp = orings::kModels[model].string_synth;
      if (dp && c.part_only) continue;
      for (int pi = 0; pi < 3; ++pi) {
        RingsFx &e = g_a;
        e.Init();
        char t[16];
        snprintf(t, sizeof(t), "%d", model);
        Set(e, "model", t);
        snprintf(t, sizeof(t), "%d", pi);
        Set(e, "polyphony", t);
        snprintf(t, sizeof(t), "%d", c.strum);
        Set(e, "strum", t);
        Set(e, "exciter", c.exciter ? "ON" : "OFF");
        e.Snap();

        Hardware &h = g_hw;
        memset(static_cast<void *>(&h), 0, sizeof(h));
        h.strummer.Init(0.01f, rings::kSampleRate / rings::kMaxBlockSize);
        h.part.Init(h.reverb_a);
        h.synth.Init(h.reverb_b);
        // The two applications the engine made: Init()'s defaults, then these.
        h.part.set_polyphony(1);
        h.synth.set_polyphony(1);
        h.part.set_model(rings::RESONATOR_MODEL_MODAL);
        h.part.set_polyphony(kPoly[pi]);
        h.synth.set_polyphony(kPoly[pi]);
        const orings::ModelDef &md = orings::kModels[model];
        if (md.string_synth) h.synth.set_fx(static_cast<rings::FxType>(md.index));
        else h.part.set_model(static_cast<rings::ResonatorModel>(md.index));

        rings::Patch patch;
        patch.structure = kParams[P_STRUCTURE].def;
        patch.brightness = kParams[P_BRIGHTNESS].def;
        patch.damping = kParams[P_DAMPING].def;
        patch.position = kParams[P_POSITION].def;
        Drums d;
        d.rate = rings::kSampleRate;
        bool same = true;
        size_t n = 0;
        for (int block = 0; block < 8000 && same; ++block) {  // 4 s
          float in[kBlock];
          for (size_t i = 0; i < kBlock; ++i) in[i] = d.At(n++);
          const bool midi_strum = c.strum == 1 && block % 400 == 20;
          if (midi_strum) NoteOn(e);

          // rings.cc FillBuffer.
          float gated[kBlock];
          if (md.string_synth) {
            memcpy(gated, in, sizeof(in));
          } else {
            for (size_t i = 0; i < kBlock; ++i) {
              float in_sample = in[i];
              float error, gain;
              error = in_sample * in_sample - h.in_level;
              h.in_level += error * (error > 0.0f ? 0.1f : 0.0001f);
              gain = h.in_level <= 0.00003f ? (1.0f / 0.00003f) * h.in_level : 1.0f;
              gated[i] = gain * in_sample;
            }
          }
          rings::PerformanceState ps;
          ps.internal_exciter = false;
          ps.internal_strum = c.strum == 0;
          ps.internal_note = true;
          ps.strum = midi_strum;
          ps.note = 0.0f;
          ps.tonic = 36.0f;  // FREQUENCY C2
          ps.fm = 0.0f;
          ps.chord = 2;      // STRUCTURE 0.25
          h.strummer.Process(md.string_synth ? NULL : gated, kBlock, &ps);
          total_strums += ps.strum;
          ps.internal_exciter = c.exciter;
          float ho[kBlock], ha[kBlock], eo[kBlock], ea[kBlock];
          stmlib::Random::Seed(0x9E3779B9u + block);
          if (md.string_synth) h.synth.Process(ps, patch, gated, ho, ha, kBlock);
          else h.part.Process(ps, patch, gated, ho, ha, kBlock);
          stmlib::Random::Seed(0x9E3779B9u + block);
          e.RenderBlock(in, eo, ea);
          same = !memcmp(ho, eo, sizeof(ho)) && !memcmp(ha, ea, sizeof(ha));
          if (!same)
            printf("     model %d poly %d differs at block %d (%.9g/%.9g vs %.9g/%.9g)\n", model, kPoly[pi],
                   block, ho[0], ha[0], eo[0], ea[0]);
        }
        ++runs;
        identical += same;
      }
    }
    CHECK(identical == runs && total_strums > 0,
          "reference [%s]: %d/%d model x polyphony runs bit-identical to hand-driven Rings DSP (%u strums)", c.what,
          identical, runs, total_strums);
  }
}

// ---------------------------------------------------------------------------
// 2. Latency, dry path, MIX.
// ---------------------------------------------------------------------------
static void TestLatencyAndMix() {
  // Part's own bypass copies IN to ODD and EVEN: what comes out is the round
  // trip alone. An impulse at several positions: always the same delay.
  {
    int worst = 0;
    double worst_frac = 0;
    for (int pos = 300; pos < 300 + 160; pos += 17) {
      RingsFx &e = g_a;
      Fresh(e);
      e.set_bypass_for_test(true);
      std::vector<float> x(4096, 0.0f);
      x[pos] = 0.5f;
      Audio a = Run(e, x);
      size_t peak = 0;
      for (size_t i = 0; i < a.l.size(); ++i)
        if (fabsf(a.l[i]) > fabsf(a.l[peak])) peak = i;
      double s = 0, w = 0;
      for (size_t i = peak - 40; i < peak + 40; ++i) {
        s += i * a.l[i] * a.l[i];
        w += a.l[i] * a.l[i];
      }
      const double centre = s / w - pos;
      worst = abs(static_cast<int>(peak) - pos - kDryDelay) > worst ? abs(static_cast<int>(peak) - pos - kDryDelay)
                                                                     : worst;
      worst_frac = fmax(worst_frac, fabs(centre - kDryDelay));
    }
    CHECK(worst == 0 && worst_frac < 0.1,
          "latency: wet round trip = %d samples (%.2f ms) at every phase, within %.3f of the dry delay", kDryDelay,
          kDryDelay / 44.1, worst_frac);
  }
  // An hour of audio through the FIFO: never an underrun.
  {
    RingsFx &e = g_a;
    Fresh(e);
    float l[128], r[128];
    const long frames = 3600L * 44100;
    long done = 0;
    int k = 1;
    while (done < frames) {
      k = k % 128 + 1;  // every block size 1..128
      for (int i = 0; i < k; ++i) l[i] = r[i] = 0.0f;
      e.ProcessFloat(l, r, l, r, k);
      done += k;
    }
    CHECK(e.underruns() == 0, "latency: 1 hour at every block size 1..128: %u underruns", e.underruns());
  }
  // MIX 0: the input, sample for sample (int16 in, int16 out), only delayed.
  {
    int exact = 0;
    for (int model = 0; model < kNumModels; ++model) {
      RingsFx &e = g_a;
      e.Init();
      char t[16];
      snprintf(t, sizeof(t), "%d", model);
      Set(e, "model", t);
      Set(e, "mix", "0");
      Set(e, "input_gain", "24");
      Set(e, "exciter", "ON");
      e.Snap();
      std::vector<int16_t> in(44100 * 2 * 2), io;
      uint32_t rng = 99;
      for (size_t i = 0; i < in.size(); ++i) {
        rng = rng * 1664525u + 1013904223u;
        in[i] = static_cast<int16_t>(rng >> 16);  // full-scale noise, every value
      }
      in[0] = -32768;
      in[1] = 32767;
      io = in;
      const size_t frames = io.size() / 2;
      for (size_t f = 0; f < frames; f += 128)
        e.Process(&io[2 * f], static_cast<int>(frames - f < 128 ? frames - f : 128));
      bool same = true;
      for (size_t f = 0; f < io.size() / 2 && same; ++f) {
        const int16_t l = f >= static_cast<size_t>(kDryDelay) ? in[2 * (f - kDryDelay)] : 0;
        const int16_t r = f >= static_cast<size_t>(kDryDelay) ? in[2 * (f - kDryDelay) + 1] : 0;
        same = io[2 * f] == l && io[2 * f + 1] == r;
      }
      exact += same;
    }
    CHECK(exact == kNumModels, "mix 0: output = input, bit for bit, delayed %d samples (%d/%d models)", kDryDelay,
          exact, kNumModels);
  }
  // MIX 100: nothing of the dry signal. Two inputs with the same mono sum but
  // different stereo (L = x + d, R = x - d) give identical outputs.
  {
    RingsFx &a = g_a, &b = g_b;
    Fresh(a, "SYMPA");
    Fresh(b, "SYMPA");
    std::vector<float> x = DrumLoop(2.0f), d(x.size());
    // On the int16 grid, so x + d, x - d and their sum are exact in float.
    for (size_t i = 0; i < d.size(); ++i) {
      x[i] = roundf(x[i] * 16384.0f) / 32768.0f;
      d[i] = roundf(0.3f * sinf(i * 0.05f) * 16384.0f) / 32768.0f;
    }
    std::vector<float> l1(x), r1(x), l2(x.size()), r2(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
      l2[i] = x[i] + d[i];
      r2[i] = x[i] - d[i];
    }
    float oa[2][128], ob[2][128];
    bool same = true;
    double wet = 0;
    for (size_t f = 0; f + 128 <= x.size(); f += 128) {
      a.ProcessFloat(&l1[f], &r1[f], oa[0], oa[1], 128);
      b.ProcessFloat(&l2[f], &r2[f], ob[0], ob[1], 128);
      same = same && !memcmp(oa, ob, sizeof(oa));
      for (int i = 0; i < 128; ++i) wet = fmax(wet, fabsf(oa[0][i]));
    }
    CHECK(same && wet > 0.01, "mix 100: wet only (no trace of the dry side signal), wet peak %.3f", wet);
  }
  // MIX 50 %: equal power -- a wet signal identical to the dry (Part bypass,
  // so the wet IS the delayed input) comes out at cos + sin = +3 dB, not +6.
  {
    RingsFx &e = g_a;
    Fresh(e);
    e.set_bypass_for_test(true);
    Set(e, "mix", "0.5");
    e.Snap();
    std::vector<float> x(44100);
    for (size_t i = 0; i < x.size(); ++i) x[i] = 0.25f * sinf(2 * M_PI * 440 * i / kRate);
    Audio a = Run(e, x);
    const double g = Rms(a.l, 22050) / Rms(x, 22050);
    CHECK(fabs(20 * log10(g) - 3.01) < 0.2, "mix 50 %%: coherent dry + wet = %+.2f dB (equal power, +3.01 expected)",
          20 * log10(g));
  }
  // MIX changes glide: a full sweep 0 -> 1 in one call moves no sample more
  // than the signal itself does.
  {
    RingsFx &e = g_a;
    Fresh(e);
    e.set_bypass_for_test(true);
    Set(e, "mix", "0");
    e.Snap();
    std::vector<float> x(44100, 0.5f);  // DC: any step in the gains shows
    Audio a = Run(e, std::vector<float>(x.begin(), x.begin() + 22050));
    Set(e, "mix", "1");
    Audio b = Run(e, std::vector<float>(x.begin(), x.begin() + 22050));
    double jump = 0;
    for (size_t i = 1; i < b.l.size(); ++i) jump = fmax(jump, fabs(b.l[i] - b.l[i - 1]));
    CHECK(jump < 0.002, "mix: 0 -> 100 %% in one step glides (largest sample step %.5f on DC 0.5)", jump);
  }
}

// ---------------------------------------------------------------------------
// 3. The input excites the resonators; every model; the exciter switch.
// ---------------------------------------------------------------------------
static void TestExcitation() {
  int ok = 0;
  for (int m = 0; m < kNumModels; ++m) {
    RingsFx &e = g_a;
    char t[16];
    snprintf(t, sizeof(t), "%d", m);
    Fresh(e, t);
    Set(e, "damping", "0.8");
    e.Snap();
    std::vector<float> x = DrumLoop(2.0f);
    std::vector<float> tail = Silence(1.0f);
    x.insert(x.end(), tail.begin(), tail.end());
    Audio a = Run(e, x);
    const double during = Rms(a.l, 22050, 88200);
    const double after = Rms(a.l, 88200 + 4410, 88200 + 8820);
    const bool dp = orings::kModels[m].string_synth;
    // A Part model rings on after the input stops; Disastrous Peace (input
    // through its effect, plus the strummed chord) is at least audible.
    const bool good = AllFinite(a) && Peak(a.l) < 1.0 && during > 0.01 && (dp || after > 1e-4);
    if (!good) printf("     model %s: during %.4f, 100 ms after %.6f, peak %.3f\n", kModelNames[m], during, after, Peak(a.l));
    ok += good;
  }
  CHECK(ok == kNumModels, "excitation: %d/%d models sound from the drum loop (Part models ring on after it)", ok,
        kNumModels);

  // Exciter OFF, STRUM MIDI, no input: strums excite nothing.
  {
    RingsFx &e = g_a;
    Fresh(e, "STRNG");
    Set(e, "strum", "MIDI");
    e.Snap();
    double peak = 0;
    for (int i = 0; i < 8; ++i) {
      NoteOn(e);
      Audio a = Run(e, Silence(0.25f));
      peak = fmax(peak, Peak(a.l));
    }
    CHECK(peak == 0.0 && e.strums() == 8, "exciter OFF: 8 strums into silence -> output exactly 0 (peak %g)", peak);
    Set(e, "exciter", "ON");
    NoteOn(e);
    Audio a = Run(e, Silence(0.5f));
    CHECK(Rms(a.l) > 0.005, "exciter ON: a strum into silence plucks the string (rms %.4f)", Rms(a.l));
    Set(e, "strum", "OFF");
    Run(e, Silence(3.0f));  // the plucked string dies away
    NoteOn(e);  // ignored: STRUM OFF
    Audio b = Run(e, Silence(0.5f));
    CHECK(Peak(b.l) < 1e-4, "exciter ON + STRUM OFF: nothing triggers it (peak %.6f)", Peak(b.l));
  }
  // Exciter ON and AUTO: one strum per detected hit, never on its own.
  {
    RingsFx &e = g_a;
    Fresh(e, "MODAL");
    Set(e, "exciter", "ON");
    e.Snap();
    Audio a = Run(e, Silence(4.0f));
    CHECK(Peak(a.l) == 0.0 && e.strums() == 0, "exciter ON + AUTO, silence: no strum, output exactly 0");
  }
  // Silence in, every model: nothing out (exactly, or a reverb's denormal
  // residue far below audibility).
  {
    double worst = 0;
    for (int m = 0; m < kNumModels; ++m) {
      RingsFx &e = g_a;
      char t[16];
      snprintf(t, sizeof(t), "%d", m);
      Fresh(e, t);
      Audio a = Run(e, Silence(3.0f));
      worst = fmax(worst, Peak(a.l));
      worst = fmax(worst, Peak(a.r));
    }
    CHECK(worst < 1e-6, "silence: every model outputs silence (peak %.3g)", worst);
  }
}

// ---------------------------------------------------------------------------
// 4. STRUM: Rings' onset detector on the input.
// ---------------------------------------------------------------------------
static std::vector<float> Hits(float seconds, float every, float level, float hz) {
  std::vector<float> x(static_cast<size_t>(seconds * kRate), 0.0f);
  const size_t step = static_cast<size_t>(every * kRate);
  for (size_t at = step / 2; at < x.size(); at += step)
    for (size_t i = 0; i < 4410 && at + i < x.size(); ++i)
      x[at + i] = level * sinf(2 * M_PI * hz * i / kRate) * expf(-i / 400.0f);
  return x;
}

// Runs `x` and returns the time (s) of every strum, to the 128-frame block.
static std::vector<double> StrumTimes(RingsFx &e, const std::vector<float> &x) {
  std::vector<double> t;
  float o[2][128];
  for (size_t f = 0; f + 128 <= x.size(); f += 128) {
    const uint32_t before = e.strums();
    e.ProcessFloat(&x[f], &x[f], o[0], o[1], 128);
    if (e.strums() != before) t.push_back(f / kRate);
  }
  return t;
}

// Every hit at `first + k * every` after the detector's ~1 s warm-up gets
// one strum, at most two (a second ~45 ms later is the original detector's
// own behaviour on a decaying low tone), and nothing strums between hits.
static bool EveryHitStrums(const std::vector<double> &t, double first, double every, double end, int *missed,
                           int *doubled, int *stray) {
  *missed = *doubled = *stray = 0;
  for (double at = first; at + every <= end + 1e-9; at += every) {
    int n = 0;
    for (double s : t) n += s >= at - 0.004 && s < at + every - 0.004;
    if (at < 1.0) continue;  // warm-up
    if (n == 0) ++*missed;
    if (n > 2) ++*stray;
    if (n == 2) ++*doubled;
  }
  return !*missed && !*stray;
}

static void TestStrum() {
  // Hits every 250 ms: kick-, snare- and hat-like.
  const float kinds[][2] = {{60, 0.7f}, {200, 0.5f}, {9000, 0.3f}};
  const char *names[] = {"kick-like 60 Hz", "snare-like 200 Hz", "hat-like 9 kHz"};
  for (int k = 0; k < 3; ++k) {
    RingsFx &e = g_a;
    Fresh(e, "MODAL");
    std::vector<double> t = StrumTimes(e, Hits(6.0f, 0.25f, kinds[k][1], kinds[k][0]));
    int missed, doubled, stray;
    const bool ok = EveryHitStrums(t, 0.125, 0.25, 6.0, &missed, &doubled, &stray);
    CHECK(ok, "strum AUTO: %s every 250 ms -> %zu strums; after 1 s warm-up %d missed, %d doubled, %d more",
          names[k], t.size(), missed, doubled, stray);
  }
  // A drum loop (8th-note hats, kick on the beat, snare on 2 and 4).
  {
    RingsFx &e = g_a;
    Fresh(e, "MODAL");
    std::vector<double> t = StrumTimes(e, DrumLoop(8.0f));
    int missed, doubled, stray;
    const bool ok = EveryHitStrums(t, 0.0, 0.25, 8.0, &missed, &doubled, &stray);
    CHECK(ok, "strum AUTO: 8 s drum loop -> %zu strums; every 8th after 1 s: %d missed, %d doubled, %d more",
          t.size(), missed, doubled, stray);
  }
  // Continuous noise and a steady tone: no stream of strums.
  {
    RingsFx &e = g_a;
    Fresh(e, "MODAL");
    std::vector<float> x(static_cast<size_t>(4 * kRate));
    uint32_t rng = 3;
    for (size_t i = 0; i < x.size(); ++i) {
      rng = rng * 1664525u + 1013904223u;
      x[i] = 0.2f * static_cast<int32_t>(rng) * (1.0f / 2147483648.0f);
    }
    Run(e, x);
    const uint32_t noise = e.strums();
    Fresh(e, "MODAL");
    for (size_t i = 0; i < x.size(); ++i) x[i] = 0.3f * sinf(2 * M_PI * 110 * i / kRate);
    Run(e, x);
    CHECK(noise <= 3 && e.strums() <= 2, "strum AUTO: 4 s of steady noise -> %u strums, steady tone -> %u", noise,
          e.strums());
  }
  // A roll of hits 20 ms apart (inside the 40 ms inhibit): never a storm.
  {
    RingsFx &e = g_a;
    Fresh(e, "MODAL");
    Run(e, Hits(2.0f, 0.02f, 0.5f, 300));
    CHECK(e.strums() <= 50, "strum AUTO: 100 hits in 2 s (20 ms apart) -> %u strums (inhibit 40 ms)", e.strums());
  }
  // SENSITIVITY: quiet hits (-40 dBFS) are missed at 0 % and caught at 100 %.
  {
    RingsFx &e = g_a;
    Fresh(e, "MODAL");
    Set(e, "sensitivity", "0");
    Run(e, Hits(4.0f, 0.25f, 0.01f, 200));
    const uint32_t low = e.strums();
    Fresh(e, "MODAL");
    Set(e, "sensitivity", "1");
    Run(e, Hits(4.0f, 0.25f, 0.01f, 200));
    const uint32_t high = e.strums();
    CHECK(low < 4 && high >= 12, "strum SENS: 16 hits at -40 dBFS -> %u strums at 0 %%, %u at 100 %%", low, high);
  }
  // OFF: no strum, but the input still reaches the resonator.
  {
    RingsFx &e = g_a;
    Fresh(e, "MODAL");
    Set(e, "strum", "OFF");
    Audio a = Run(e, DrumLoop(2.0f));
    CHECK(e.strums() == 0 && Rms(a.l, 22050) > 0.01, "strum OFF: 0 strums, the input still resonates (rms %.3f)",
          Rms(a.l, 22050));
  }
  // MIDI: every note-on is one strum, the note number ignored.
  {
    RingsFx &e = g_a;
    Fresh(e, "MODAL");
    Set(e, "strum", "MIDI");
    for (int i = 0; i < 10; ++i) {
      NoteOn(e);
      Run(e, Silence(0.1f));
    }
    Run(e, Hits(2.0f, 0.25f, 0.5f, 200));  // audio hits: not strums in MIDI mode
    CHECK(e.strums() == 10, "strum MIDI: 10 note-ons -> %u strums (audio onsets ignored)", e.strums());
  }
  // Disastrous Peace in AUTO: the drums strum the chord.
  {
    RingsFx &e = g_a;
    Fresh(e, "DP-RV");
    std::vector<double> t = StrumTimes(e, Hits(4.0f, 0.25f, 0.5f, 200));
    int missed, doubled, stray;
    const bool ok = EveryHitStrums(t, 0.125, 0.25, 4.0, &missed, &doubled, &stray);
    CHECK(ok, "strum AUTO in Disastrous Peace: the hits strum the chord (%zu strums, %d missed after 1 s)",
          t.size(), missed);
  }
}

// ---------------------------------------------------------------------------
// 5. The knobs do what Rings' do.
// ---------------------------------------------------------------------------
static Audio Ring(const char *model, const char *key, const char *value, float seconds = 3.0f,
                  const char *freq = "C3") {
  RingsFx &e = g_a;
  Fresh(e, model);
  Set(e, "frequency", freq);
  Set(e, "structure", "0.25");
  Set(e, "brightness", "0.5");
  Set(e, "damping", "0.7");
  Set(e, "position", "0.25");
  Set(e, "width", "0");  // ODD + EVEN
  if (key) Set(e, key, value);
  e.Snap();
  return Run(e, Hit(seconds));
}

static void TestKnobs() {
  // FREQUENCY: the string rings at the named note.
  {
    double worst = 0;
    for (int octave = 1; octave <= 5; ++octave) {
      char name[16];
      snprintf(name, sizeof(name), "C%d", octave);
      Audio a = Ring("STRNG", NULL, NULL, 1.0f, name);
      const double hz = MidiHz(12 * (octave + 1));
      const double f = Pitch(a.l, 8820, 8820, hz * 0.8, hz * 1.25);
      const double c = f > 0 ? Cents(f, hz) : 9999;
      if (fabs(c) > 20) printf("     C%d: %.2f Hz (%.1f cents)\n", octave, f, c);
      worst = fmax(worst, fabs(c));
    }
    CHECK(worst < 20, "FREQUENCY: string excited by the input rings at C1..C5 within %.1f cents", worst);
    RingsFx &e = g_a;
    Fresh(e);
    char buf[16];
    Set(e, "frequency", "A3");
    e.GetByKey("frequency", buf, sizeof(buf));
    CHECK(!strcmp(buf, "45"), "FREQUENCY: named notes; A3 is option %s (MIDI 57)", buf);
  }
  // FINE: +50 cents.
  {
    Audio a = Ring("STRNG", NULL, NULL, 1.0f, "A3"), b = Ring("STRNG", "fine", "50", 1.0f, "A3");
    const double f0 = Pitch(a.l, 8820, 8820, 176, 275), f1 = Pitch(b.l, 8820, 8820, 176, 275);
    CHECK(fabs(Cents(f1, f0) - 50.0) < 3, "FINE: +50 ct -> %.1f cents above A3 (%.2f Hz -> %.2f Hz)",
          Cents(f1, f0), f0, f1);
  }
  // DAMPING: longer ring.
  {
    Audio a = Ring("MODAL", "damping", "0.3", 6.0f), b = Ring("MODAL", "damping", "0.9", 6.0f);
    const double ta = DecayTime(a.l), tb = DecayTime(b.l);
    CHECK(tb > 2 * ta, "DAMPING: 0.3 -> %.2f s, 0.9 -> %.2f s to -40 dB", ta, tb);
  }
  // BRIGHTNESS: brighter.
  {
    Audio a = Ring("MODAL", "brightness", "0.1"), b = Ring("MODAL", "brightness", "0.9");
    const double ca = Centroid(a.l, 4410), cb = Centroid(b.l, 4410);
    CHECK(cb > 1.3 * ca, "BRIGHTNESS: 0.1 -> %.0f Hz, 0.9 -> %.0f Hz spectral centroid", ca, cb);
  }
  // STRUCTURE and POSITION change the sound.
  {
    Audio a = Ring("MODAL", "structure", "0.1"), b = Ring("MODAL", "structure", "0.8");
    const double c = Correlation(std::vector<float>(a.l.begin(), a.l.begin() + 44100),
                                 std::vector<float>(b.l.begin(), b.l.begin() + 44100));
    CHECK(fabs(c) < 0.5, "STRUCTURE: 0.1 vs 0.8 -> correlation %.3f (different partials)", c);
    Audio p = Ring("STRNG", "position", "0.05"), q = Ring("STRNG", "position", "0.5");
    const double cp = Centroid(p.l, 4410), cq = Centroid(q.l, 4410);
    const double d = Correlation(std::vector<float>(p.l.begin(), p.l.begin() + 44100),
                                 std::vector<float>(q.l.begin(), q.l.begin() + 44100));
    CHECK(fabs(cp - cq) > 50 || d < 0.9, "POSITION: 0.05 vs 0.5 -> centroid %.0f / %.0f Hz, correlation %.3f", cp, cq,
          d);
  }
  // Chords: STRUCTURE through Western Chords' hysteresis.
  {
    RingsFx &e = g_a;
    Fresh(e, "CHORD");
    Set(e, "chord", "m7");
    char buf[16];
    e.GetByKey("chord", buf, sizeof(buf));
    CHECK(e.chord() == 4 && !strcmp(buf, "4"), "chord: m7 written by name -> chord %d", e.chord());
  }
  // ODD / EVEN, WIDTH, ROUTING.
  {
    RingsFx &e = g_a;
    Fresh(e, "MODAL");
    Set(e, "polyphony", "2");
    e.Snap();
    Audio a = Run(e, DrumLoop(2.0f));
    const double corr = Correlation(a.l, a.r);
    Fresh(e, "MODAL");
    Set(e, "polyphony", "2");
    Set(e, "width", "0");
    e.Snap();
    Audio b = Run(e, DrumLoop(2.0f));
    bool mono = true;
    for (size_t i = 0; i < b.l.size(); ++i) mono = mono && b.l[i] == b.r[i];
    Fresh(e, "MODAL");
    Set(e, "polyphony", "2");
    Set(e, "routing", "ODD");
    e.Snap();
    Audio c = Run(e, DrumLoop(2.0f));
    bool odd_mono = true;
    for (size_t i = 0; i < c.l.size(); ++i) odd_mono = odd_mono && c.l[i] == c.r[i];
    CHECK(corr < 0.9 && mono && odd_mono && Rms(c.l) > 0.001,
          "ODD/EVEN: STEREO width 100 %% L/R correlation %.3f; width 0 and ROUTING ODD exactly mono", corr);
  }
  // LEVEL: wet only, in dB.
  {
    Audio a = Ring("MODAL", NULL, NULL), b = Ring("MODAL", "level", "-12");
    const double g = 20 * log10(Rms(b.l) / Rms(a.l));
    CHECK(fabs(g + 12) < 0.3, "LEVEL: -12 dB -> wet %.2f dB", g);
  }
  // INPUT GAIN: drives the resonator harder (the excitation scales).
  {
    Audio a = Ring("MODAL", "input_gain", "-12"), b = Ring("MODAL", NULL, NULL);
    const double g = 20 * log10(Rms(b.l) / Rms(a.l));
    CHECK(g > 6, "INPUT GAIN: -12 -> 0 dB raises the wet by %.1f dB", g);
  }
}

// ---------------------------------------------------------------------------
// 6. Hard material: low end, high end, overload, model switching.
// ---------------------------------------------------------------------------
static void TestRobustness() {
  // Kick-heavy loop at full scale, INPUT GAIN +24 dB, every model and
  // polyphony, long DAMPING: finite, bounded, no recovery.
  {
    double peak = 0;
    bool finite = true;
    uint32_t recoveries = 0;
    for (int m = 0; m < kNumModels; ++m) {
      for (int pi = 0; pi < 3; ++pi) {
        RingsFx &e = g_a;
        char t[16];
        snprintf(t, sizeof(t), "%d", m);
        Fresh(e, t);
        snprintf(t, sizeof(t), "%d", pi);
        Set(e, "polyphony", t);
        Set(e, "input_gain", "24");
        Set(e, "damping", "1");
        Set(e, "level", "12");
        Set(e, "frequency", "C0");
        e.Snap();
        Audio a = Run(e, DrumLoop(4.0f, 1.0f, 1));
        finite = finite && AllFinite(a);
        peak = fmax(peak, fmax(Peak(a.l), Peak(a.r)));
        recoveries += e.recoveries();
      }
    }
    CHECK(finite && peak <= 1.0 && recoveries == 0,
          "overload: full-scale kicks +24 dB, DAMP 1, LEVEL +12, C0, all models x polyphony -> finite, peak %.3f "
          "(soft-clipped), %u recoveries",
          peak, recoveries);
  }
  // Consecutive kicks never build up: the resonance after 20 s is no louder
  // than after 4 s.
  {
    RingsFx &e = g_a;
    Fresh(e, "SYMPA");
    Set(e, "damping", "0.95");
    e.Snap();
    Audio a = Run(e, DrumLoop(20.0f, 0.8f, 1));
    const double early = Rms(a.l, 3 * 44100, 4 * 44100), late = Rms(a.l, 19 * 44100, 20 * 44100);
    CHECK(late < early * 1.5, "kick build-up: SYMPA DAMP 0.95, rms at 3-4 s %.3f, at 19-20 s %.3f", early, late);
  }
  // High input: a 19 kHz tone through the round trip (Part bypass) comes out
  // as 19 kHz; nothing folds back below it.
  {
    RingsFx &e = g_a;
    Fresh(e);
    e.set_bypass_for_test(true);
    std::vector<float> x(44100);
    for (size_t i = 0; i < x.size(); ++i) x[i] = 0.5f * sinf(2 * M_PI * 19000 * i / kRate);
    Audio a = Run(e, x);
    const double tone = Tone(a.l, 19000, 4410, 44100) * 2;
    double worst = 0;
    for (double f = 100; f < 18000; f += 97) worst = fmax(worst, Tone(a.l, f, 4410, 44100) * 2);
    CHECK(20 * log10(worst / tone) < -60, "aliasing: 19 kHz through the resamplers, worst image below it %.1f dB",
          20 * log10(worst / tone));
  }
  // MODEL switching under a drum loop: fades, no step bigger than the music.
  {
    RingsFx &e = g_a;
    Fresh(e, "MODAL");
    std::vector<float> x = DrumLoop(0.5f);
    double jump = 0;
    bool finite = true;
    for (int m = 0; m < kNumModels * 2; ++m) {
      char t[16];
      snprintf(t, sizeof(t), "%d", (m * 5) % kNumModels);
      Set(e, "model", t);
      Audio a = Run(e, x);
      finite = finite && AllFinite(a);
      for (size_t i = 1; i < a.l.size(); ++i) jump = fmax(jump, fabs(a.l[i] - a.l[i - 1]));
    }
    CHECK(finite && jump < 0.5, "model switch: 24 switches under drums, finite, largest sample step %.3f", jump);
  }
}

// ---------------------------------------------------------------------------
// 7. Keys, state.
// ---------------------------------------------------------------------------
static void TestState() {
  RingsFx &a = g_a, &b = g_b;
  a.Init();
  Set(a, "mix", "0.3");
  Set(a, "model", "KVERB");
  Set(a, "frequency", "G#4");
  Set(a, "decay_verb", "0.8");
  Set(a, "level", "-6");
  Set(a, "input_gain", "6");
  Set(a, "sensitivity", "0.7");
  Set(a, "strum", "MIDI");
  Set(a, "exciter", "ON");
  Set(a, "polyphony", "4");
  Set(a, "routing", "EVEN");
  Set(a, "fine", "-33");
  Set(a, "width", "0.4");
  char s[1024];
  CHECK(a.WriteState(s, sizeof(s)) > 0, "state: %s", s);
  b.Init();
  CHECK(b.ReadState(s), "state: read back");
  bool same = true;
  for (int i = 0; i < P_COUNT; ++i) same = same && a.GetParam(i) == b.GetParam(i);
  CHECK(same, "state: every parameter restored");
  CHECK(b.ReadState("{\"v\":1,\"mix\":0.9}") && b.GetParam(P_MIX) == 0.9f && b.GetParam(P_MODEL) == 0,
        "state: a key the state lacks returns to its default");
  char buf[32];
  CHECK(a.GetByKey("width_na", buf, sizeof(buf)) > 0 && !strcmp(buf, "0") && a.SetByKey("width_na", "3"),
        "keys: the '--' placeholders answer and ignore writes");
  CHECK(!a.SetByKey("nonsense", "1") && a.GetByKey("nonsense", buf, sizeof(buf)) < 0, "keys: unknown refused");
  CHECK(a.SetByKey("polyphony", "2") && a.polyphony() == 4, "keys: polyphony index 2 is 4 voices");
  CHECK(a.SetByKey("strum", "OFF") && a.GetParam(P_STRUM) == 2, "keys: enum written by name");
}

// ---------------------------------------------------------------------------
// 8. Soak.
// ---------------------------------------------------------------------------
static void TestSoak() {
  // 10 minutes of a drum loop with random knob moves, model, polyphony,
  // strum, exciter and routing changes: finite, bounded, no recoveries.
  RingsFx &e = g_a;
  Fresh(e);
  Set(e, "mix", "0.5");
  unsigned rng = 4242;
  auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; };
  Drums d;
  d.rate = kRate;
  double peak = 0;
  bool finite = true;
  float l[128], r[128];
  const int blocks = static_cast<int>(600 * kRate / 128);
  const char *knobs[] = {"structure", "brightness", "damping", "position", "mix", "sensitivity", "width"};
  size_t n = 0;
  for (int b = 0; b < blocks; ++b) {
    if (b % 1500 == 0) {
      char t[16];
      snprintf(t, sizeof(t), "%d", static_cast<int>(rnd() * kNumModels));
      e.SetByKey("model", t);
      snprintf(t, sizeof(t), "%d", static_cast<int>(rnd() * 3));
      e.SetByKey("polyphony", t);
      snprintf(t, sizeof(t), "%d", static_cast<int>(rnd() * 3));
      e.SetByKey("strum", t);
      e.SetByKey("exciter", rnd() < 0.5f ? "ON" : "OFF");
      snprintf(t, sizeof(t), "%d", static_cast<int>(rnd() * 3));
      e.SetByKey("routing", t);
      snprintf(t, sizeof(t), "%d", static_cast<int>(rnd() * kNumFreqNotes));
      e.SetByKey("frequency", t);
      snprintf(t, sizeof(t), "%d", static_cast<int>(rnd() * 48) - 24);
      e.SetByKey("input_gain", t);
    }
    if (rnd() < 0.05f) {
      char t[16];
      snprintf(t, sizeof(t), "%.3f", rnd());
      e.SetByKey(knobs[static_cast<int>(rnd() * 7)], t);
    }
    if (rnd() < 0.02f) NoteOn(e);
    for (int i = 0; i < 128; ++i) l[i] = r[i] = d.At(n++, 0.6f);
    e.ProcessFloat(l, r, l, r, 128);
    for (int i = 0; i < 128; ++i) {
      finite = finite && isfinite(l[i]) && isfinite(r[i]);
      peak = fmax(peak, fmax(fabs(l[i]), fabs(r[i])));
    }
  }
  CHECK(finite && peak <= 1.0 && e.recoveries() == 0 && e.underruns() == 0,
        "soak: 10 min drums + random settings, all models: finite, peak %.3f, %u recoveries, %u strums", peak,
        e.recoveries(), e.strums());
}

int main() {
  setvbuf(stdout, NULL, _IOLBF, 0);
  TestReference();
  TestLatencyAndMix();
  TestExcitation();
  TestStrum();
  TestKnobs();
  TestRobustness();
  TestState();
  TestSoak();
  if (g_failures) {
    printf("%d FAILED\n", g_failures);
    return 1;
  }
  printf("ALL FX ENGINE TESTS PASSED\n");
  return 0;
}
