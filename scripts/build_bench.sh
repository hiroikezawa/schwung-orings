#!/usr/bin/env bash
# Static AArch64 builds of tests/bench.cpp and tests/bench_fx.cpp for the Move
# (build/bench-arm64, build/bench-fx-arm64), the same flags as the modules.
# Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
CXX="${CROSS_PREFIX:-aarch64-linux-gnu-}g++"
CC="${CROSS_PREFIX:-aarch64-linux-gnu-}gcc"
GEN="$MODULE/build/generated"
OBJ="$MODULE/build/obj-bench"
mkdir -p "$OBJ"
phrase=()   # Phrase Gen's engine is C (the RANDOM pad)
for f in "$MODULE"/src/phrase/*.c; do
  "$CC" -std=c11 -O2 -I"$MODULE/src/phrase" -I"$GEN" -c "$f" -o "$OBJ/$(basename "$f").o"
  phrase+=("$OBJ/$(basename "$f").o")
done
"$CXX" -O2 -static -fno-strict-aliasing -fno-math-errno -ffp-contract=off -DTEST -fno-exceptions -fno-rtti \
  -include stdio.h -w -I"$MODULE/src" -isystem "$MODULE/src/rings/mi" -I"$GEN" \
  "$MODULE/tests/bench.cpp" "$MODULE"/src/wrapper/*.cpp "${phrase[@]}" $(find "$MODULE/src/rings/mi" -name '*.cc' | sort) \
  -o "$MODULE/build/bench-arm64" -lm
"$CXX" -O2 -static -fno-strict-aliasing -fno-math-errno -ffp-contract=off -DTEST -fno-exceptions -fno-rtti \
  -include stdio.h -w -I"$MODULE/src" -isystem "$MODULE/src/rings/mi" -I"$MODULE/build/generated" \
  "$MODULE/tests/bench_fx.cpp" "$MODULE/src/fx/orings_fx.cpp" $(find "$MODULE/src/rings/mi" -name '*.cc' | sort) \
  -o "$MODULE/build/bench-fx-arm64" -lm
echo "built build/bench-arm64, build/bench-fx-arm64 (scripts/bench_move.sh runs them on the Move)"
