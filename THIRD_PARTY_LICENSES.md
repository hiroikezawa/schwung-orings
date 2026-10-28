# Third-party code in oRings and oRings FX

This repository builds two modules from one copy of the Rings DSP: oRings
(sound generator, `dsp.so`) and oRings FX (audio FX, `orings-fx.so`).
Everything either ships is MIT-licensed. Nothing copyleft is compiled in or
linked (each `.so` needs only `libc` and `libm`; `scripts/check_abi.py` fails
the build otherwise).

## Rings DSP and stmlib (Emilie Gillet) — MIT

| | |
|---|---|
| Upstream | https://github.com/pichenettes/eurorack (`rings/`) at `f4cfa7697095a28d292bff758beb51deb49b5a86` (2026-10-12) |
| | https://github.com/pichenettes/stmlib at `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec` (the submodule commit eurorack pins) |
| Author | Emilie Gillet (emilie.o.gillet@gmail.com) |
| Copyright | Copyright 2012, 2014, 2015 Emilie Gillet (per file) |
| Licence | MIT (the eurorack README: "Code (STM32F projects): MIT license"; every vendored file carries the MIT notice, and stmlib's `LICENSE` is kept as `src/rings/mi/LICENSE`) |
| Where | `src/rings/mi/` |

Vendored: all of `rings/dsp/` (Part, StringSynthPart, resonator, string, FM
voice, plucker, strummer, onset detector, note filter, limiter, follower,
string-synth oscillator/voice/envelope, and `fx/` reverb, chorus, ensemble and
FX engine), `rings/resources.{h,cc}`, the resource generators
`rings/resources/*.py`, and the stmlib files the DSP includes (`stmlib.h`,
`dsp/{cosine_oscillator,delay_line,dsp,filter,parameter_interpolator,units}.h`,
`dsp/units.cc`, `utils/random.{h,cc}`). Only DSP: no drivers, UI, settings
storage, CV scaling, calibration or bootloader code.

Unmodified except for two patches. `patches/0001-exciter-gain.patch`, marked in the source
with `oRings patch` comments: the internal exciter's strike strength (the modal
pulse, the plucker's noise burst, the FM voice's internal envelope, the string
synth's envelope level) can be scaled per strum, so that MIDI velocity and
EXCITER LEVEL reach it. At 1.0 — the hardware's fixed strike — every sample is
bit-identical to upstream (`scripts/compare_upstream.sh`).

`patches/0002-interpolate-table-end.patch` (stmlib `Interpolate`): an index
exactly at a table's last entry read one entry past the end of the table with
weight 0 -- Rings does this at DAMPING 1.0 (`lut_4_decades`, index 256 of 257
entries), found by AddressSanitizer. The patch returns that last entry without
the extra read: the same value for every finite table, so the output is
unchanged.

Two pieces of `rings/cv_scaler.cc` logic are re-implemented (they belong to
the hardware's CV handling, which is not vendored): the chord-selection
hysteresis (`src/common/rings_models.h`) and the FM input smoothing
(`src/wrapper/orings.cpp`, `src/fx/orings_fx.cpp`). oRings FX also repeats
`rings.cc`'s eight-line input noise gate verbatim (`src/fx/orings_fx.cpp`),
since `rings.cc` (the firmware's main loop) is not vendored either. oRings FX
never changes the exciter gain, so it runs upstream's code exactly.

`scripts/fetch_rings.sh --verify` re-downloads both commits and checks that the
vendored tree is exactly upstream plus that patch.

### Trademark

Mutable Instruments is a registered trademark, and the eurorack repository's
guidelines ask that derivative works not use the name "Mutable Instruments"
and recommend against keeping the original module name. oRings is an
unofficial port, not affiliated with or endorsed by Mutable Instruments; the
name appears here only to credit the source of the code. "oRings" and
"oRings FX" are working names.

## Schwung plugin API headers — MIT

| | |
|---|---|
| Upstream | https://github.com/charlesvestal/schwung (`src/host/plugin_api_v1.h`, `src/host/audio_fx_api_v2.h`) |
| Licence | MIT (`third_party/schwung/LICENSE`) |
| Where | `third_party/schwung/host/plugin_api_v1.h` and `audio_fx_api_v2.h`, unmodified |
