# oRings FX

**English** | [日本語](README.ja.md)

(Module ID: `orings-fx`. "oRings FX" is a provisional development name.)

An **Audio FX** for Schwung / Ableton Move. It is an **unofficial port** of Mutable
Instruments' Eurorack module **Rings** (a resonator): the DSP source code of the
official firmware, published by Emilie Gillet under the MIT licence, is bundled and run
as-is. The resonators have not been rebuilt, nor imitated with comb filters, delays or
reverbs.

It reproduces, with a Move track's audio, what Rings does with a cable plugged into its
**IN jack**. The incoming sound (drums, synths, noise and so on) "strikes" the
resonator, and metal plates, bells and strings ring and decay naturally.

- Rings' 3 standard models (Modal Resonator / Sympathetic Strings /
  Modulated/Inharmonic String) and the official hidden modes (**2-op FM**, **Western
  Chords**, **Karplusverb**, and **Disastrous Peace** = a string machine + 6 effects)
  can be chosen with the MODEL knob.
- For **automatic strumming** from the input, Rings' own onset detection
  (`OnsetDetector`) is used as it is. It acts on every drum hit, even without MIDI.
- The sound-producing parts (`rings::Part`, `rings::StringSynthPart`,
  `rings::Strummer`) are run **with the same calls in the same order** as the hardware
  firmware's `rings.cc`, and the input also goes through the same noise gate as in
  `rings.cc`.
- The DSP runs at the hardware's **48 kHz in 24-sample blocks**, resampled to and from
  Move's 44.1 kHz.
- Tests confirm that the Rings part of this module's output is **identical sample for
  sample** to the official DSP driven directly with the same wiring as the hardware
  (input into IN, STRUM from automatic detection or an external trigger), across every
  model × 1/2/4 voices.

| | |
|---|---|
| Kept | The resonators of every model (modal with 64 modes, 8 strings, the FM voice, the string synth's 12 voices), the input noise gate, onset detection and STRUM processing, the internal exciter (per-model pulse / noise burst / FM envelope), voice allocation, the two outputs ODD / EVEN, the limiter, reverb/chorus/ensemble/formant |
| Left out | The V/OCT input (pitch is set by the FREQ knob alone), the CV inputs and attenuverters of each knob, calibration, LEDs and buttons |
| Added | MIX (dry/wet), input gain, detection sensitivity, strum switching (auto / MIDI / none), internal exciter ON/OFF, output routing and stereo width, wet level |

---

## Getting started

1. In a Move track's Schwung slot (or Master FX), choose **oRings FX** as an Audio FX.
2. Play drums or a synth. The input strikes the resonator, adding metallic or string
   resonance at FREQ's pitch.
3. Set the balance with the original sound with knob 1 (MIX). 0 % is the original
   sound as it is, 100 % only Rings' sound.
4. Choose the model with knob 7 (MODEL) and the resonance pitch (C0–C7) with knob 6
   (FREQ).
5. Knob 4 (DAMP) changes the length of the tail, knob 3 (BRIGHT) the brightness.
6. Turn the jog wheel to move to page 2 (Advanced).

**FREQ does not "detect the input's pitch".** It sets the resonator's own fundamental
(the same as the hardware's FREQUENCY knob). Whatever pitch the input has, the
resonance sounds at FREQ's pitch.

---

## Signal flow

```
input (stereo) ─┬──────────── delayed 72 samples (aligned with wet) ───── Dry ─┐
                │                                                              │
                └ (L+R)/2 → IN GN → clip at ±1.0 → 44.1→48 kHz                MIX
                  → noise gate (rings.cc) → Strummer (onset detection)  (equal power)
                  → Part / StringSynthPart → ODD / EVEN                        │
                  → ROUTE, WIDTH → LEVEL → 48→44.1 kHz ─────────────────── Wet ─┘
```

- Rings' IN jack is mono, so a stereo input goes in as the average of L and R (a mono
  source keeps its level). The original sound (dry) takes a separate path in stereo,
  with no Rings processing at all.
- When the input is too loud it tops out at full scale (±1.0), like the hardware's
  audio codec. It stops before the resonator, so there is no runaway oscillation.
- In the official Disastrous Peace, the IN sound goes into the **effect** (formant /
  chorus / reverb / ensemble), not the resonator (and does not pass the noise gate).
  This module does the same, so in DP-* you get the input with the effect applied plus
  the string machine's chords.

---

## Knobs

### Page 1 (Main)

| Knob | Display | What it does |
|---|---|---|
| 1 | MIX | Dry / wet 0–100 % (equal power; 0 % = original, 100 % = wet only) |
| 2 | STRUCT etc. | Harmonics, the strings' pitch relationships, degree of dissonance (the name changes with the model; table below) |
| 3 | BRIGHT etc. | Brightness (highs of the excitation and resonance) |
| 4 | DAMP etc. | Decay time |
| 5 | POS etc. | Where it is struck (how the harmonics come out) |
| 6 | FREQ | The resonator's fundamental C0–C7 (semitone steps, shown as a note name). Default C2 |
| 7 | MODEL | 12 modes (table below) |
| 8 | LEVEL | Wet level -24 to +12 dB (no effect on the dry) |

### MODEL and the names of knobs 2–5

| MODEL | What it is | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| MODAL | Modal resonator (plate, bar, membrane, bell). Hi-hats ring like bells | STRUCT | BRIGHT | DAMP | POS |
| SYMPA | Sympathetic strings (a main string + strings ringing in sympathy) | STRUCT | BRIGHT | DAMP | POS |
| STRNG | Modulated/inharmonic string (STRUCT sets the string's stiffness and nonlinearity) | STRUCT | BRIGHT | DAMP | POS |
| KVERB | Karplusverb (string + reverb) | STRUCT | BRIGHT | DCY+RV | POS |
| FM | 2-operator FM. The input's envelope applies the FM | RATIO | INDEX | DECAY | FDBK |
| CHORD | Western Chords (sympathetic strings fixed to a chord) | CHORD | BRIGHT | DAMP | POS |
| DP-FM | Disastrous Peace + formant | CHORD | REGIST | ENV | VOWEL |
| DP-CH | Disastrous Peace + chorus | CHORD | REGIST | ENV | CHORUS |
| DP-RV | Disastrous Peace + reverb | CHORD | REGIST | ENV | REVERB |
| DP-F2 | Disastrous Peace + formant 2 | CHORD | REGIST | ENV | VOWEL |
| DP-EN | Disastrous Peace + ensemble | CHORD | REGIST | ENV | ENSMBL |
| DP-R2 | Disastrous Peace + reverb 2 | CHORD | REGIST | ENV | REVERB |

- Whatever the names, the values moved inside are the same four (STRUCTURE /
  BRIGHTNESS / DAMPING / POSITION). The meaning the official DSP assigns to them per
  mode is shown on screen as it is.
- **FM**: RATIO = the modulator's frequency ratio, INDEX = FM depth, DECAY = envelope
  length, FDBK = feedback. With input, the official FM voice is modulated by the input's
  envelope follower.
- **CHORD**: knob 2 chooses one of 11 chords (OCT, 5, sus4, m, m7, m9, m11, 69, M9, M7, M).
- **Disastrous Peace**: REGIST = the organ's combination of partials, ENV = envelope
  length (a drone fully right), knob 5 = amount of effect.
- **KVERB**: DCY+RV moves the string's decay and the reverb's amount and length together.

### Page 2 (Advanced)

| Knob | Display | What it does |
|---|---|---|
| 1 | IN GN | Input level into Rings -24 to +24 dB |
| 2 | SENS | Onset detection sensitivity 0–100 % (only when STRUM is AUTO) |
| 3 | STRUM | AUTO (detected from the input) / MIDI (note-ons) / OFF |
| 4 | EXCITE | Internal exciter OFF / ON (default OFF) |
| 5 | POLY | Number of voices 1 / 2 / 4 (default 1, the same as the hardware's default) |
| 6 | ROUTE | STEREO (ODD = left, EVEN = right) / ODD / EVEN |
| 7 | FINE | Fine tuning ±100 cents |
| 8 | WIDTH | Stereo width 0–100 % (only when ROUTE is STEREO) |

A knob that has no effect in the current setting (SENS when STRUM is not AUTO, EXCITE
in Disastrous Peace, WIDTH when ROUTE is not STEREO) turns into **a dotted "--"**
(read-only) in its place. The positions of the other knobs do not move.

---

## STRUM

A Rings "strum" is the signal to **switch to the next voice and sound the internal
exciter**. The input sound itself always goes into the resonator, strum or no strum.

- **AUTO**: as on the hardware with the STRUM jack empty, Rings' own onset detection
  finds the input's attacks and strums.
  - After a detection, no further strum is accepted for 40 ms (how the official
    Strummer works). Hits 20 ms apart do not make the strums run away.
  - It does not react to steady noise or sustained sounds (because it picks up only
    attacks that are statistically "out of the ordinary").
  - **For about the first second after start-up the detector has not settled**, so the
    first few hits are missed (a property of the official algorithm).
  - With a low decaying sound (a kick) it may react once more about 45 ms after a hit
    (also the official algorithm's behaviour). With 1 voice and the exciter OFF the
    sound does not change.
  - **SENS** raises or lowers only the signal going into the detector by ±30 dB (50 % =
    the same as the hardware). The sound going into the resonator does not change.
    Raise it to make it react to quiet input.
- **MIDI**: the same as feeding a trigger into the STRUM jack. Each MIDI note-on
  reaching that slot becomes one strum (the note number is not used; the pitch is set by
  FREQ).
- **OFF**: no strums. The input keeps sounding a single voice.

**About Disastrous Peace.** In the official firmware, Disastrous Peace's onset
detection is not given the input (the string machine sounds only from a trigger or
V/OCT). In this module, only when STRUM is **AUTO**, the input's attacks sound the
string machine's chords (drums play chords). With MIDI or OFF it behaves as the
official firmware does.

## Internal exciter (EXCITE)

The default is **OFF**. When OFF, as on the hardware with a cable in IN, only the input
sound strikes the resonator. No extra pulses or noise are layered on the drums.

When ON, at every strum Rings' own exciter (the modal pulse, the string's noise burst,
FM's envelope) also sounds and strikes the resonator together with the input. **It
sounds only at strums** (every detected hit in AUTO, every note-on in MIDI, never in
OFF). It never sounds by itself with silent input. This is a combination the hardware
does not offer (using the internal exciter with IN plugged in), but the processing is
the official `Part`'s exciter path itself.

## ODD / EVEN, ROUTE, WIDTH

Rings has two outputs, ODD and EVEN.

- **1 voice**: two pickups on one resonator (different ways of taking out the harmonics).
- **2 and 4 voices**: voices are assigned to ODD and EVEN in turn.

The roles of ROUTE and WIDTH are split so they do not overlap.

- **ROUTE = STEREO**: ODD left, EVEN right. At WIDTH 100 % as is; at 0 % the average of
  ODD + EVEN (mono) comes out on both sides. **"SUM" is WIDTH 0 %**, so it is not a
  separate choice.
- **ROUTE = ODD / EVEN**: only one output on both sides (one side, as when using just
  one jack on the hardware). It is mono, so WIDTH shows "--".

---

## MIX and dry

- MIX is **equal power** (dry = cos, wet = sin). At 50 % there is no extreme dip or rise
  in level.
- **At MIX 0 % the input comes out bit for bit** (checked for every model in tests).
- The wet path (44.1→48 kHz → Rings' 24-sample blocks → 44.1 kHz) is **72 samples
  (1.6 ms)** late. The dry is delayed by the same amount so their phases do not drift
  apart when MIX blends them (it is 72 samples late even at MIX 0 %).
- Moving MIX changes it smoothly over about 10 ms, so there are no clicks.
- The wet has clip protection that rounds off smoothly only what exceeds 0.9 (-0.9 dBFS).
  With MIX between 0 % and 100 %, the dry + wet sum gets the same protection (nothing is
  done to sound at or below 0.9).

---

## Relation to the hardware (what replaces what)

| Hardware | This module |
|---|---|
| IN jack | Move's audio (the average of L + R), IN GN, clipping at full scale |
| V/OCT (empty) | Left empty. The pitch is set by FREQ (C0–C7) alone |
| FREQUENCY knob | FREQ (semitone steps; changes glide over about 50 ms, like the hardware's pot) |
| FM input | FINE (with the same smoothing as the hardware's FM input) |
| STRUM jack | STRUM: AUTO = empty / MIDI = trigger input / OFF = nothing arrives |
| Internal exciter | On the hardware it is not used once IN is plugged; EXCITE can turn it ON |
| ODD / EVEN jacks | ROUTE and WIDTH |
| Model buttons, hidden gestures | MODEL and POLY |

STRUCTURE / BRIGHTNESS / DAMPING / POSITION move smoothly with the same coefficients as
the hardware's pots. Changing MODEL or POLY fades out over 4 ms, switches, and comes back
over 4 ms, so there are no clicks.

---

## Saved settings

Every parameter is saved as `state`, and can be saved/restored with Schwung's slot saving
and My Presets.

---

## Realtime safety and load

- No memory allocation, locking, file I/O or logging during processing. Instances are
  taken from a static pool (of 8).
- It never switches by itself to a heavier model, nor swaps models.
- If NaN / infinity appears, the resonator is re-initialised and restarts from silence
  (it never happened in tests).

- Memory per instance: about 243 KB (including 128 KB of reverb memory). The pool holds
  8 statically (about 1.9 MB), and only created instances are used.
- **CPU load (measured on a real Move, 2026-10-28)**: with a drum-loop input, all 12 modes
  × 1/2/4 voices × exciter OFF/ON were measured for 10 s each. The mean per block (128
  samples, 2902 µs) is 133–308 µs; the heaviest is KVERB with 4 voices and the exciter ON
  at 308 µs (**10.6 %**). The lightest are STRNG / FM with 1 voice (about 5 %). Build the
  bench with `scripts/build_bench.sh` and measure with
  `BENCH=fx scripts/bench_move.sh ableton@<Move's IP>`.

---

## Verification (`scripts/test.sh`)

- **The wrapper adds nothing**: the output of the Rings part is **bit-identical**, with a
  drum-loop input, to the official DSP driven by hand with the same wiring as `rings.cc`
  (noise gate → Strummer → Part / StringSynthPart): IN + onset detection (6 models × 1/2/4
  voices), IN + STRUM trigger (12 modes × 1/2/4 voices), onset detection + internal
  exciter (6 models × 1/2/4 voices).
- Latency: the wet path is 72 samples at every phase (within 0.05 sample of the dry), and
  an hour's worth of processing (every block length 1–128) has 0 buffer shortages.
- MIX 0 % = bit-identical to the input (every model, full-scale noise), no dry mixed into
  MIX 100 %, 50 % being equal power (+3.01 dB), and MIX staying smooth when moved all at once.
- Every model sounds with drums, and the resonance continues after the input stops. With
  the exciter OFF the output is exactly 0 even when strummed with silent input; with it ON
  the strings sound; and with silent input every model outputs exactly 0.
- Onset detection: with kick-, snare- and hi-hat-like hits and a drum loop, every hit after
  the first second is detected (0 missed, at most 2 per hit), no reaction to steady noise or
  sustained sounds, no runaway with hits 20 ms apart, SENS picking up quiet hits (-40 dBFS),
  one strum per note in MIDI.
- FREQ (within 5 cents over C1–C5), FINE, DAMP (decay time), BRIGHT (spectral centroid),
  STRUCT, POS, chords, ODD/EVEN, WIDTH, ROUTE, LEVEL, IN GN.
- A full-scale kick + IN GN +24 dB, DAMP at maximum, LEVEL +12 dB and C0 staying finite
  and within the clip protection for every model × voice count; no energy building up with
  continuous kicks; aliasing below 19 kHz at -60 dB or lower when passing 19 kHz; model
  switching while drums play; saving and restoring state; 10 minutes of random operation
  (every model). With ASan / UBSan.
- A smoke test dlopening it as a plugin (every model, MIX 0 matching, saving state, 8
  instances at once, instances independent of each other).
- With Schwung's own page engine (`shared/` copied from the device): the knob order for
  all 12 models (knob 1 always MIX), "--" appearing in the same place, and knob operation.

---

## Building and installing

From **one and the same Rings DSP** (`src/rings/mi/`), this repository builds two modules:
the Sound Generator version (`orings`) and this Audio FX version (`orings-fx`).

```bash
scripts/build.sh arm64
```

```bash
scripts/test.sh
```

```bash
python3 scripts/install.py http://move.local:7700 --module orings-fx
```

`build.sh arm64` produces `dist/orings-fx-module.tar.gz`. Restart Move after installing.

| File | Contents |
|---|---|
| `src/fx/orings_fx.{h,cpp}` | The FX itself (the wiring around Rings, MIX, the output stage) |
| `src/fx/upsampler.h` | 44.1→48 kHz (polyphase FIR, 32 taps × 160 phases) |
| `src/rings/resampler.h` | 48→44.1 kHz (64 taps) |
| `src/common/rings_models.h` | Mapping between MODEL and Rings' models, chord hysteresis |
| `src/fx_plugin.cpp` | Connection to Schwung's Audio FX API v2 |
| `scripts/fx_metadata.py` | Parameter definitions (`fx/module.json` is generated from it too) |
| `tests/test_fx.cpp`, `tests/smoke_fx.py`, `tests/test_grid_fx.mjs` | Tests |

---

## Licence

This module's code is under the MIT licence (`LICENSE`, copyright holder Hiro Ikezawa).
The bundled Rings DSP and stmlib are Emilie Gillet's MIT-licensed code, and each file
keeps its copyright notice (`THIRD_PARTY_LICENSES.md`).

Mutable Instruments is a registered trademark. This module is an unofficial port with no
connection to Mutable Instruments, and the name is used only to show where the code came from.
