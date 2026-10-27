#!/usr/bin/env bash
# Where src/rings/mi came from, as a command.
#
#   scripts/fetch_rings.sh --verify   download the pinned upstream commits,
#                                     apply patches/, and diff against the
#                                     vendored tree (exit 1 on any difference)
#   scripts/fetch_rings.sh --update   the same, but overwrite the vendored tree
#
# Copied: all of rings/dsp/ (Part, StringSynthPart, every resonator, string,
# FM voice, exciter, strummer, note filter, limiter and the fx/ reverb, chorus
# and ensemble), rings/resources.{h,cc}, the resource generators
# (rings/resources/*.py) and the stmlib files the DSP includes. Nothing of the
# hardware (drivers/, ui.cc, settings.cc, cv_scaler.cc, rings.cc) is copied.
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
EURORACK_COMMIT=f4cfa7697095a28d292bff758beb51deb49b5a86
STMLIB_COMMIT=e3bd7c9cc00e4364166f9905c0509b6ffd0535ec   # eurorack's stmlib submodule
STMLIB_FILES=(stmlib.h LICENSE
  dsp/cosine_oscillator.h dsp/delay_line.h dsp/dsp.h dsp/filter.h
  dsp/parameter_interpolator.h dsp/units.h dsp/units.cc
  utils/random.h utils/random.cc)

mode="${1:---verify}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

git -C "$work" init -q eurorack
git -C "$work/eurorack" fetch -q --depth 1 https://github.com/pichenettes/eurorack "$EURORACK_COMMIT"
git -C "$work/eurorack" checkout -q FETCH_HEAD
git -C "$work" init -q stmlib
git -C "$work/stmlib" fetch -q --depth 1 https://github.com/pichenettes/stmlib "$STMLIB_COMMIT"
git -C "$work/stmlib" checkout -q FETCH_HEAD

out="$work/mi"
mkdir -p "$out/rings/resources" "$out/stmlib"
cp -r "$work/eurorack/rings/dsp" "$out/rings/dsp"
cp "$work/eurorack/rings/resources.h" "$work/eurorack/rings/resources.cc" "$out/rings/"
cp "$work/eurorack/rings/resources/"*.py "$out/rings/resources/"
for f in "${STMLIB_FILES[@]}"; do
  mkdir -p "$out/stmlib/$(dirname "$f")"
  cp "$work/stmlib/$f" "$out/stmlib/$f"
done
mv "$out/stmlib/LICENSE" "$out/LICENSE"
for p in "$MODULE"/patches/*.patch; do
  [[ -e "$p" ]] || continue
  patch -s -d "$out" -p1 < "$p"
done

case "$mode" in
  --verify)
    if diff -r "$out" "$MODULE/src/rings/mi"; then
      echo "PASS: src/rings/mi is upstream $EURORACK_COMMIT / $STMLIB_COMMIT + patches/"
    else
      echo "FAIL: src/rings/mi differs from upstream + patches/" >&2
      exit 1
    fi ;;
  --update)
    rm -rf "$MODULE/src/rings/mi"
    mkdir -p "$MODULE/src/rings"
    cp -r "$out" "$MODULE/src/rings/mi"
    echo "updated src/rings/mi" ;;
  *) echo "usage: $0 [--verify|--update]" >&2; exit 2 ;;
esac
