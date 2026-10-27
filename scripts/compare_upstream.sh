#!/usr/bin/env bash
# Build tests/upstream_compare.cpp against untouched upstream Rings and
# against src/rings/mi (upstream + patches/), run both, and require
# byte-identical output: patches/ changes nothing at its neutral setting
# (exciter gain 1.0, the hardware's fixed strike).
# Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
EURORACK_COMMIT=f4cfa7697095a28d292bff758beb51deb49b5a86
STMLIB_COMMIT=e3bd7c9cc00e4364166f9905c0509b6ffd0535ec
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
git -C "$work" init -q eurorack
git -C "$work/eurorack" fetch -q --depth 1 https://github.com/pichenettes/eurorack "$EURORACK_COMMIT"
git -C "$work/eurorack" checkout -q FETCH_HEAD
git -C "$work" init -q stmlib
git -C "$work/stmlib" fetch -q --depth 1 https://github.com/pichenettes/stmlib "$STMLIB_COMMIT"
git -C "$work/stmlib" checkout -q FETCH_HEAD
mkdir -p "$work/up"
cp -r "$work/eurorack/rings" "$work/up/rings"
cp -r "$work/stmlib" "$work/up/stmlib"

build() {  # build <include root> <output>
  local root=$1 out=$2
  g++ -std=gnu++11 -O2 -w -DTEST -include stdio.h -fno-strict-aliasing -ffp-contract=off \
    -isystem "$root" "$MODULE/tests/upstream_compare.cpp" \
    "$root"/rings/dsp/*.cc "$root/rings/resources.cc" "$root/stmlib/dsp/units.cc" \
    "$root/stmlib/utils/random.cc" -o "$out"
}
build "$work/up" "$work/upstream"
build "$MODULE/src/rings/mi" "$work/vendored"
"$work/upstream" > "$work/a.raw"
"$work/vendored" > "$work/b.raw"
size=$(stat -c %s "$work/a.raw")
python3 -c "import array,sys; a=array.array(\"f\"); a.frombytes(open(sys.argv[1],\"rb\").read()); n=sum(1 for x in a if x); assert n > len(a)//2, n; print(\"signal:\", n, \"of\", len(a), \"samples non-zero\")" "$work/a.raw"
if cmp -s "$work/a.raw" "$work/b.raw"; then
  echo "PASS: vendored + patches/ is byte-identical to upstream ($((size / 8)) stereo samples, 6 models + 6 FX x 4 polyphonies)"
else
  echo "FAIL: vendored output differs from upstream" >&2
  exit 1
fi
