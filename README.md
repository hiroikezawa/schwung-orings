# oRings

**English** | [日本語](README.ja.md)

(Module ID: `orings`. "oRings" is a provisional development name.)

A Sound Generator for Schwung / Ableton Move. It is an **unofficial port** of
Mutable Instruments' Eurorack module **Rings** (a resonator): the DSP source code
of the official firmware, published by Emilie Gillet under the MIT licence, is
bundled and run as-is. The resonators have not been rebuilt or simplified.

- Rings' 3 standard models (Modal Resonator / Sympathetic Strings /
  Modulated/Inharmonic String) and the 4 official hidden modes
  (**2-op FM**, **Western Chords**, **Karplusverb**, and
  **Disastrous Peace** = an organ / string machine with 6 effects) can be chosen
  directly with the MODEL knob. No hidden gestures are needed.
- The sound-producing parts (`rings::Part`, `rings::StringSynthPart`,
  `rings::Strummer`) are run **with the same calls in the same order** as the
  hardware firmware's `rings.cc`. The port replaces **only the hardware**: pots,
  CV/trigger inputs and output jacks.
- The DSP runs at the hardware's **48 kHz in 24-sample blocks** and is resampled
  to Move's 44.1 kHz.
- Tests confirm that this module's output is **identical sample for sample** to
  the official DSP driven directly with the same wiring as the hardware, across
  all 12 modes × 1–4 voices (see "Verification").
- Performance features: it **strikes automatically on every 1/16** in time with
  Move's playback, 16 pads switch the struck steps ON/OFF, touching and turning a
  knob **records its movement** (REC), the **RANDOM** pad generates melodies, the
  **PITCH** knob chooses notes on a scale, and there are 8 **preset pads** (see
  "Getting started"). It still plays from MIDI notes as before.

| | |
|---|---|
| Kept | The resonators of every model (modal with 64 modes, 8 strings, the FM voice, the string synth's 12 voices), the internal exciter (per-model pulse/noise burst), STRUM processing, voice allocation (round robin, ping-pong with 3 voices), the two outputs ODD / EVEN, the limiter, reverb/chorus/ensemble/formant |
| Left out | The external audio input (IN jack) and automatic strum detection, the CV inputs and attenuverters of each knob, calibration, LEDs and buttons |
| Added | MIDI playing (note = V/OCT + STRUM), strike strength from velocity, pitch bend, fine tuning, the spacing for strumming a chord one note at a time, output routing, volume. Transport-synced automatic strikes (16 step pads), knob movement recording, RANDOM, scale PITCH, preset pads |

---

## Getting started

The basic flow is: put it in a slot → play → shape the sound with the knobs →
build a pattern on the pads → record knob movements with REC.

### 1. Make a sound

1. Open a slot in the Schwung screen (Shift+Vol+Track, or long-press a track
   button) and choose **oRings** as the Synth.
2. Open the slot's Synth (oRings) to get the oRings screen.
3. Press **Play** on Move. oRings **strikes its resonator automatically on every
   1/16 in time with the transport** (no clip or MIDI notes needed). It does not
   strike while stopped. It also plays from MIDI notes on a Move track, as before.

**Rings has no ADSR.** A struck sound decays naturally with the length of the tail,
DAMPING (and the model's own physics). Releasing a note does not stop it. This is
how the hardware behaves too.

### 2. Shape the sound (main page)

| Knob 1 | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|
| VOLUME | STRUCT | BRIGHT | DAMP |
| **Knob 5** | **Knob 6** | **Knob 7** | **Knob 8** |
| POS | POLY (1–4 voices) | MODEL (12) | PITCH |

- **MODEL** chooses the kind of resonator. The names of knobs 2–5 change to match
  the model (e.g. RATIO / INDEX / DECAY / FDBK in FM; see the table below).
- **DAMP** changes the length of the tail, **BRIGHT** the brightness, **STRUCT** the
  harmonic structure and **POS** where it is struck.
- **PITCH** is the pitch of the automatic strikes, chosen from the notes of a scale
  (set on the Scale page).
- **Narrow down to the models you use**: the MODELS pad (left of RANDOM) opens a
  list where you switch on only the models you want (see "Choosing models (MODELS
  list)" below).
- **Turn the jog wheel** to move to the Rings page (transpose, strike and output
  settings) → Scale (ROOT, SCALE, STEPS, RESET, CHORD) → My Presets (presets) →
  Module.

### 3. Build a pattern (pads)

While the screen is open the pads belong to oRings (they do not reach Move).

```
                          MODELS RANDOM  REC   ← right three of the top row
 P1  P2  P3  P4  P5  P6  P7  P8                ← second row from the top (presets)
  9  10  11  12  13  14  15  16                ← second row from the bottom
  1   2   3   4   5   6   7   8                ← bottom row
```

- Press a pad in the **bottom two rows** to switch whether that step is struck, ON
  (bright) / OFF (dim). The white one is the current step. With 8 steps the second
  row from the bottom is dark (16 steps: STEPS on the Scale page).
- **RANDOM** writes a new melody into PITCH (see "RANDOM" below).
- **MODELS** opens the ON/OFF list of the models the MODEL knob can choose.
- The 8 pads of the **second row from the top** are presets (see "5. Saving and
  recalling presets" below).

### 4. Record knob movements (REC)

1. Press **REC** (the top-right pad) to switch it ON (solid red; OFF is pale red).
2. With Move playing, **touch and turn** the knob you want to record. While you
   touch it, its value is recorded into each step of the loop. Let go to stop.
3. A recorded knob replays its movement on every pass of the loop from then on. The
   knob's own value is not overwritten; clearing the recording returns to it.
4. To clear: **hold Shift and touch a knob** to clear that knob's recording.
   **Shift + touch knob 1 (VOLUME)** clears everything.
5. When you are done, press REC again to switch it OFF so that a stray touch records
   nothing.

Recordable: knobs 2–5 (the same four values whatever their names: STRUCTURE,
BRIGHTNESS, DAMPING, POSITION), PITCH, and EXCITE on the Rings page (strike strength,
which becomes a per-step accent). VOLUME, POLY, MODEL and the other settings are not
recorded.

### 5. Saving and recalling presets

**On the pads (eight, quickly):** the 8 pads of the second row from the top are
presets 1–8.

- **Save**: hold Shift and tap a pad. It stores the whole current state.
- **Recall**: tap a pad. It switches at once, even while playing (MODEL switches too).
- Colours: dark = empty, pale yellow = saved, white = the last one recalled (or
  saved). Tapping an empty pad only shows `EMPTY` on screen and changes nothing.
- Stored: every knob value including MODEL, recorded knob movements, the melody made
  by RANDOM, the step ON/OFF pattern, ROOT / SCALE / STEPS (not the REC state or the
  MODELS list).
- The eight presets are independent of the set and are saved to
  `/data/UserData/schwung/orings/presets.json`, so the same eight are available from
  oRings in any set and any slot. The file is separate from other modules' presets.
  Reinstalling keeps it.

**With a name (no limit on how many):** turn the jog wheel to the **My Presets** page
near the end. Save As (save under a name), Preset (load from the list), Save
(overwrite) and Delete are available. This is Schwung's common preset feature, saved
in `/data/UserData/schwung/presets/orings/`.

### 6. When you are done

Close the screen with **Back**. The pads return to Move. The sound, recordings and
step pattern are kept in the set (the slot autosave) and in presets.

---

## Controls (detail)

The screen and knobs look and work like Schwung's standard knob screen (eight knobs
per page), with knob touch (for recording) and the pads added.

| Control | Action |
|---|---|
| Knobs 1–8 | Parameters of the current page (page table below) |
| Touch a knob (REC ON, playing) | **Motion-record** that knob |
| Shift + touch a knob | Clear that knob's recording; on VOLUME (knob 1), clear **all** |
| Jog turn / click | Change page, select an item (same as Schwung's standard grid) |
| Back | Close the screen (pads return to Move) |

| Page | Knobs 1–8 |
|---|---|
| Main | VOLUME, STRUCT, BRIGHT, DAMP, POS, POLY, MODEL, PITCH (2–5 change name with the model) |
| Rings | FREQ, FINE, EXCITE, SPREAD, VELO, BEND, OUTPUT, GAIN |
| Scale | ROOT, SCALE, STEPS, RESET, CHORD (only for models that use chords) |
| My Presets | Load, save, delete presets (Schwung common) |
| Module | Swap the module, help (Schwung common) |

The header shows the current model, the loop position, `ARM` while REC is ON and
`REC` while recording. While PITCH is touched it shows the note name (e.g.
`PITCH Eb3`). A knob with a recording shows Schwung's "modulated" display (the
playing value moves).

Stepped knobs (MODEL, POLY, PITCH, FREQ etc.) do not accelerate; they advance one
step every few detents. Continuous knobs (STRUCT etc.) accelerate when turned fast
(like Schwung's standard knobs).

## Pads

**While this screen is open, oRings owns the pads** (`host_pad_block`). Pad presses
do not reach Move's own instruments or sequencer. Close the screen and the pads
return to Move with their colours (Schwung only lets a module own them while its
screen is shown).

| Pad | Action |
|---|---|
| **REC** (top right) | Motion recording ON/OFF. **ON = solid red, OFF = pale red** |
| **RANDOM** (left of it) | Make a new melody and write it into each step of PITCH |
| MODELS (further left) | Open / close the MODELS list (models in use) |
| Second row from the top (8) | Presets 1–8. Tap to recall, Shift + tap to save |
| Bottom two rows (16) | Strike ON/OFF per step. ON = bright, OFF = dim, current step = white. With 8 steps the upper row (9–16) is dark |

- It **strikes automatically on every 1/16, synced to Move's transport** (starts
  with Play, no strikes while stopped; steps line up with the bar). At the start of
  each step a strike goes into Rings' STRUM (strength equivalent to velocity 100 ×
  EXCITE), at PITCH's note. OFF steps are not struck, and the previous sound keeps
  decaying.
- The loop length is **STEPS** on the Scale page (8 / 16, default 8).
- Automatic strikes are not affected by SPREAD (SPREAD is for chords played over MIDI).
- MIDI notes from the Move track (clips, or the pads while the screen is closed) also
  play oRings. Automatic strikes and MIDI can be used together.

### Motion recording

- With REC ON and Move playing, a knob's movement is recorded into the loop **while
  you touch it** (8 points per 1/16, 12-bit). Let go to stop.
- Continuous knobs are interpolated smoothly between points; PITCH holds each point's
  value.
- Recorded values reach the resonator with the same smoothness as the hardware's pots
  (a low-pass of about 50 ms).
- Playback does not overwrite the knob value (the recording just takes priority).
  Clearing with Shift + touch returns to the knob value.
- While stopped nothing is recorded or played back, and the knob values sound.
- Touching a knob whose name changes with the model (RATIO, CHORD, VOWEL etc.) records
  the value inside it (STRUCTURE etc.). Recordings stay as they are when the model
  changes.

### PITCH (knob 8)

- **PITCH is a scale note from the start.** Knob 8 chooses only the notes of the Scale
  page's ROOT + SCALE, over C1–C6 (five octaves). It is not quantised afterwards; the
  knob itself moves through the scale. Default: C3 in C Phrygian.
- Changing ROOT / SCALE moves PITCH and the recorded PITCH to the nearest note of the
  new scale.
- SCALE: Phrygian (default), Minor, Dorian, Major, Mixolyd, Lydian, Locrian, HarmMin,
  PhrygDom, MinPent, In, Chromatic.
- PITCH is the pitch of the automatic strikes. When played from MIDI notes, the note's
  own pitch sounds. Both are transposed as a whole by FREQ and FINE on the Rings page.

### RANDOM (melody generation)

RANDOM **makes a slightly different melody each time from the MIDI files (source
phrases) in `patterns/` and writes it into PITCH**. Six source phrases are included.

**What happens when you press it**

1. One source phrase is chosen at random (the previous one is less likely).
2. Its note sequence is varied with a "50 % amount of change": roughly half of the
   notes change — moving to a neighbouring scale note, jumping an octave, repeating
   the previous note and so on — and the rest stay as in the source. The result
   differs every time and never equals the previous melody.
3. No rests are made. Rests in the source are filled with notes, so every step has a
   pitch.
4. The melody is written into each step of the loop as the PITCH recording.
   - 8 steps with STEPS at 8; 16 steps at 16 (two 8-step variations in a row).
   - The previous PITCH recording is overwritten for the length of STEPS.
   - Whether a step is struck is decided by the bottom two rows of pads. RANDOM does
     not change the step ON/OFF pattern.

**Range and scale**

- Every note written is a note of the scale set on the Scale page (ROOT, SCALE).
- The range is **from a fifth below to an octave and a half above the note of knob 8
  (PITCH)** (-7 to +17 semitones, inside C1–C6). Notes outside it are folded back by
  octaves. Set the overall height with knob 8 before pressing.

**Undo / redo**

- Press again to rewrite with a different melody.
- **Shift + touch knob 8** clears the PITCH recording, back to knob 8's note only.
- With REC ON, touch and turn knob 8 to rewrite only some steps by hand.

#### Replacing the source phrases

The source MIDI files are embedded into `dsp.so` at build time (the Move does not
read files for this). After replacing them you must **rebuild and reinstall**.

1. Put the MIDI files you want (Standard MIDI File, `.mid`) in `patterns/`.
2. List the files to use in `patterns/patterns.txt`, one per line (1–16). The line
   order is the phrase number. Anything after `#` is a comment.

   ```
   my_riff.mid          # 8 steps from the start of the file
   long_phrase.mid 16   # 8 steps from step 17 (the second column is the start)
   ```

   Without `patterns.txt`, every `.mid` in `patterns/` is used in name order.
3. Build, install and restart Move.

Which part of a MIDI file is used:

- Only **eight 1/16 notes from the start position** of each file.
- Only **pitch and order** are used. Velocity and note length are not.
- Note positions snap to the nearest 1/16. When a step holds several notes, only the
  strongest (the first, on a tie) is used. All tracks and channels are read together.
- Source phrases are first fitted to **C Phrygian** when loaded, varied, and finally
  fitted to the current ROOT and SCALE. Phrases written in C Phrygian keep their shape
  best.
- A file with no note within the 8 steps from its start position is not used. File
  names up to 31 bytes, up to 256 notes per file.

## Choosing models (MODELS list)

You can decide which of the 12 models the MODEL knob can choose.

1. Press the **MODELS pad** (left of RANDOM) → the list opens (the `<` on the right
   marks the model sounding now).
2. Turn the jog wheel to move the cursor (it stops at the ends); click the jog to
   switch **ON / OFF** (■ = ON, □ = OFF).
3. **ALL ON** at the top switches all on; **ALL OFF** switches off all but the model
   sounding now.
4. Close the list with **Back** or the MODELS pad.

- Changes are saved at once (`/data/UserData/schwung/orings/settings.json`). Shared by
  oRings in every slot, kept across reinstalls, not part of presets.
- The MODEL knob steps through only the models that are ON and stops at both ends.
- At least one stays ON ("ONE MUST STAY ON").
- A preset that uses a model switched OFF still plays with that model (the list only
  limits what the knob can choose).

---

## Knobs

### Page 1 (main)

| Knob | Display | What it does |
|---|---|---|
| 1 | VOLUME | Level |
| 2 | STRUCT etc. | Harmonic/pitch structure (the name changes with the model; table below) |
| 3 | BRIGHT etc. | Brightness (highs of the excitation and resonance) |
| 4 | DAMP etc. | Decay time |
| 5 | POS etc. | Where it is struck (how the harmonics come out) |
| 6 | POLY | Number of voices 1 / 2 / 3 / 4 |
| 7 | MODEL | 12 modes (table below) |
| 8 | PITCH | Pitch of the automatic strikes (scale notes, C1–C6) |

### MODEL and the names of knobs 2–5

| MODEL | What it is | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| MODAL | Modal resonator (resonance like a struck plate or bell) | STRUCT | BRIGHT | DAMP | POS |
| SYMPA | Sympathetic strings (a main string + strings ringing in sympathy) | STRUCT | BRIGHT | DAMP | POS |
| STRNG | Modulated/inharmonic string (STRUCT sets the string's stiffness) | STRUCT | BRIGHT | DAMP | POS |
| KVERB | Karplusverb (string + reverb) | STRUCT | BRIGHT | DCY+RV | POS |
| FM | 2-operator FM | RATIO | INDEX | DECAY | FDBK |
| CHORD | Western Chords (sympathetic strings fixed to a chord) | STRUCT | BRIGHT | DAMP | POS |
| DP-FM | Disastrous Peace + formant | CUTOFF | REGIST | ENV | VOWEL |
| DP-CH | Disastrous Peace + chorus | CUTOFF | REGIST | ENV | CHORUS |
| DP-RV | Disastrous Peace + reverb | CUTOFF | REGIST | ENV | REVERB |
| DP-F2 | Disastrous Peace + formant 2 | CUTOFF | REGIST | ENV | VOWEL |
| DP-EN | Disastrous Peace + ensemble | CUTOFF | REGIST | ENV | ENSMBL |
| DP-R2 | Disastrous Peace + reverb 2 | CUTOFF | REGIST | ENV | REVERB |

- Whatever the names, the values moved inside are the same four (STRUCTURE /
  BRIGHTNESS / DAMPING / POSITION). The meaning the official DSP assigns to them per
  mode is shown on screen as it is.
- **BRIGHT's floor**: in MODAL, SYMPA, STRNG, KVERB and CHORD, BRIGHT's knob range 0–1
  is mapped to the official DSP's BRIGHTNESS 0.2–1, because below 0.2 the strike is cut
  by 15–20 dB and almost no sound comes out (0.2.1). FM (INDEX 0 = a pure sine) and the
  Disastrous Peace family stay at 0–1.
- **FM**: RATIO = the modulator's frequency ratio (the official quantisation table),
  INDEX = FM depth, DECAY = length of the internal envelope (a drone near the
  maximum), FDBK = feedback (0 at the centre, two kinds of feedback either side).
- **CHORD** (Western Chords and Disastrous Peace): CHORD on the Scale page chooses one
  of 11 chords (OCT, 5, sus4, m, m7, m9, m11, 69, M9, M7, M). It is shown only for
  models that use chords. Knobs 2–5 are left free for changing the sounding notes in
  real time.
- **Knob 2**: Western Chords' STRUCT changes the strings' inharmonicity continuously.
  Disastrous Peace's CUTOFF is a 2-pole low-pass not in the official DSP, after the
  organ's output (150 Hz to 20 kHz, no resonance). The official DSP's STRUCTURE is used
  only for chord selection in this mode, leaving nothing that can be moved
  continuously, so this was added. Fully right it is bypassed completely and gives the
  official sound itself.
- **Disastrous Peace** is the organ / string machine that appears with a hidden gesture
  on the hardware. REGIST = the organ's registration (combination of partials), ENV =
  longer decay the further you turn, a slower attack too in the right half, a drone
  fully right, knob 5 = amount of effect (the vowel in formant). Note-on, chords,
  envelope and effects are all the official `StringSynthPart`.
- **KVERB**: DCY+RV moves the string's decay and the reverb's amount and length
  together. POS moves the strike position and also the stereo width.

### Page 2 (Rings)

| Knob | Display | What it does |
|---|---|---|
| 1 | FREQ | Transpose -24 to +24 semitones. Shifts everything up or down, leaving the pitches of MIDI notes and PITCH as they are |
| 2 | FINE | Fine tuning ±100 cents |
| 3 | EXCITE | Strength of the strike (internal exciter) 0–200 %. 100 % is the same as the hardware. Recordable |
| 4 | SPREAD | When a chord is played over MIDI, the spacing for sounding it one note at a time, 10–200 ms |
| 5 | VELO | How much velocity acts, 0–100 % (0 % = always full strength) |
| 6 | BEND | Pitch-bend range 0–24 semitones |
| 7 | OUTPUT | STEREO (ODD = left, EVEN = right) / SUM (ODD + EVEN in mono) / ODD / EVEN |
| 8 | GAIN | -24 to +12 dB |

### Page 3 (Scale)

| Knob | Display | What it does |
|---|---|---|
| 1 | ROOT | Root of the PITCH scale (C to B) |
| 2 | SCALE | Scale of PITCH (12 kinds, default Phrygian) |
| 3 | STEPS | Loop length 8 / 16 steps |
| 4 | RESET | Click to silence every voice (turning the knob does nothing) |

**About SPREAD.** The hardware Rings ignores the second and later strums (triggers)
arriving within 10 ms (how `Strummer` works). Playing a chord at once would therefore
sound only one note, so this module queues the notes and strums them one at a time, at
least 10.5 ms apart. Raising SPREAD gives a time offset like a guitar strum. The
hardware's "automatic strum detection" is a feature for the external audio input, and
this module, being an instrument, has no input, so this knob is there instead.

**About EXCITE and VELO.** The hardware's internal exciter strikes at a constant
strength. This module multiplies the strike strength (the modal pulse, the string's
noise burst, FM's internal envelope, the height of Disastrous Peace's envelope) by
"EXCITE × velocity". At strength 1.0 (EXCITE 100 %, velocity 127) the output is
identical sample for sample to the official DSP.

### ODD / EVEN

Rings has two outputs, ODD and EVEN.

- **1 voice**: two pickups on one resonator (different ways of taking out the harmonics).
- **2–4 voices**: voices are assigned to ODD and EVEN in turn. The first note goes to
  EVEN (right), the next to ODD (left), and so on (as on the hardware).

With OUTPUT at STEREO, ODD comes out left and EVEN right. It is not the same sound
copied to both sides.

---

## MIDI

| Message | Action |
|---|---|
| Note on | Sets the pitch (V/OCT) and strums. Strums every time, even for the same note |
| Note off | Does nothing (Rings has no gate) |
| Velocity | Strike strength (VELO and EXCITE) |
| Pitch bend | Overall pitch (BEND's range, with the same smoothing as the hardware's FM input) |
| CC 120 (All Sound Off) | The same as RESET |
| CC 123 (All Notes Off) | Discards queued notes |
| CC 121 | Returns pitch bend to 0 |
| MIDI clock (start/clock/stop) | Used as the tempo of the automatic strikes only where Schwung passes no playback position |

The MIDI channel, scale and transposition can use Schwung / Move's own mechanisms (the
slot's receive channel, the pads' scale) as they are.

Voice allocation is as official: each strum moves to the next voice (with 3 voices, the
same 1,0,2,1,0,2… pattern as the hardware).

---

## Sample rate and pitch

Rings' DSP computes its pitch, envelopes, filters, delays and reverb times all
assuming 48 kHz (`kSampleRate` in `rings/dsp/dsp.h`). So this module runs the DSP at
48 kHz in 24-sample units, as on the hardware, and converts to 44.1 kHz with a 64-tap
polyphase FIR (flat to 20 kHz, latency about 0.7 ms + at most one 0.5 ms block). Pitch
error, measured with FM's sine, is within 0.05 cent over C1–C7.

The string model (STRNG) is off by up to 15 cents over C1–C7, and Karplusverb by about
10 cents. This is a property of the official string model itself (the delay of its
damping filter), not an error from the port.

---

## Saved settings

Every parameter, the recorded knob movements and the step ON/OFF pattern are saved as
`state`, and can be saved/restored with Schwung's slot saving, My Presets and the preset
pads (RESET and the REC state are not saved). States saved by 0.1.0 also load (as no
recordings and every step ON).

So that the automatic strikes are not delayed even when the screen is not shown or the
sound has stopped, silent blocks output a tiny DC of -75 dBFS (6 LSB). Schwung thins out
the processing of silent slots, and without this the strikes right after playback
starts would be late. At VOLUME 0 it is completely silent.

---

## Verification (`scripts/test.sh`)

- **Matching upstream**: the bundled DSP (with patches) and the unmodified official
  source are run on the same input (all 6 models + 6 FX × 1–4 voices, 4.6 million
  samples moving knobs, chords, FM and strums), and the output matches byte for byte.
- **The wrapper adds nothing**: this module's output is bit-identical, across all 12
  modes × 4 voice counts, to the official DSP driven by hand in the same order as
  `rings.cc`.
- Pitch (C1–C7), bend, FINE and FREQ, repeated notes (12 → 12 strums), 4-note chords at
  once (4 strums, 0 dropped), note-offs not changing the sound, 1/2/3/4 voices, ODD/EVEN
  assignment, every knob changing the sound (every model), Disastrous Peace's 6 effects
  each sounding different, output modes, velocity, saving and restoring state, RESET, no
  clicks on model switches, 10 minutes of random playing (every model, no NaN or abnormal
  amplitude). With ASan / UBSan.
- A smoke test that dlopens it as a plugin and plays every model.
- Performance features: no strikes while stopped, 8 strikes per second at 120 BPM (0
  dropped), PITCH's pitch (0 cents at G2), every strike lining up with the start of a
  1/16, no strikes on OFF pads, MIDI and automatic strikes together, recording and
  playback of knobs (nothing recorded with REC OFF, knob values not overwritten, knob
  values while stopped), accents from recording EXCITE, every RANDOM note being on the
  scale, PITCH moving with scale changes, 16 steps, saving and restoring recordings and
  steps (0.1.0 states, broken data), 3000 random operations without anomalies.
- With Schwung's own page engine (`shared/` copied from the device): the order and names
  of the knobs for all 12 models (when turning the MODEL knob and when a preset switches
  it), pads, touch recording, PITCH's note names, the MODELS list and the preset pads.

---

## Building and installing

```bash
scripts/test.sh                                # all tests + the Move build
scripts/build.sh arm64                         # dist/orings-module.tar.gz
python3 scripts/install.py http://move.local:7700
```

Restart Move after installing.

From the same Rings DSP (`src/rings/mi/`), this repository also builds the Audio FX
version **oRings FX** (`orings-fx`, an effect that excites the resonator with its input
audio). `scripts/build.sh arm64` also produces `dist/orings-fx-module.tar.gz`, which is
installed with the same `install.py` plus `--module orings-fx`. It is described in
`fx/README.md`.

The bundled DSP has 2 patches against the official source (`patches/`): `0001` is the
strike strength above, and `0002` fixes stmlib's `Interpolate` reading one past the end
when looking up exactly the table's last element (it happens with DAMP at its maximum
1.0; the value read is multiplied by a weight of 0, so the sound does not change).

CPU load (real Move): build the bench with `scripts/build_bench.sh` and measure with
`scripts/bench_move.sh ableton@<Move's IP>`.

---

## Licence

MIT (`LICENSE`). The bundled Rings DSP and stmlib are Emilie Gillet's MIT-licensed code
(`THIRD_PARTY_LICENSES.md`). The RANDOM melody generation (`src/phrase/`) and the source
phrase MIDI files (`patterns/`) are the author's own, under MIT.

This module is an unofficial port and has no connection with Mutable Instruments.
Mutable Instruments is a registered trademark.
