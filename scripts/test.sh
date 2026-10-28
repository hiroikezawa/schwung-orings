#!/usr/bin/env bash
# Vendored-source check, native build + every test, for both modules
# (oRings and oRings FX): engine (ASan/UBSan), upstream comparison, real
# dlopen smoke, the knob grid through Schwung's own page engine, then the
# Move builds and their ABI checks.
# Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
if [[ "${ORINGS_SKIP_FETCH:-0}" != 1 ]]; then
  echo "== vendored DSP"
  bash "$MODULE/scripts/fetch_rings.sh" --verify
  echo "== upstream comparison"
  bash "$MODULE/scripts/compare_upstream.sh"
fi
echo "== native build"
bash "$MODULE/scripts/build.sh" native
OUT="$MODULE/build/native"
GEN="$MODULE/build/generated"
OBJ="$MODULE/build/obj-test"
mkdir -p "$OBJ"

echo "== engine (ASan/UBSan)"
SAN=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
COMMON=(-O1 -g -fno-strict-aliasing -fno-math-errno -ffp-contract=off -DTEST -fno-exceptions -fno-rtti
        -I"$MODULE/src" -isystem "$MODULE/src/rings/mi" -I"$GEN")
objs=()
while IFS= read -r f; do
  o="$OBJ/$(echo "${f#$MODULE/src/}" | tr '/' '_').o"
  g++ -std=gnu++11 -w -include stdio.h "${COMMON[@]}" "${SAN[@]}" -c "$f" -o "$o"
  objs+=("$o")
done < <(find "$MODULE/src/rings/mi" -name '*.cc' | sort)
ours() {  # ours <source>...: compiles our own sources, prints the objects
  for f in "$@"; do
    o="$OBJ/$(basename "$f").o"
    g++ -std=gnu++17 -Wall -Wextra -Werror "${COMMON[@]}" -I"$MODULE/tests" "${SAN[@]}" -c "$f" -o "$o"
    echo "$o"
  done
}
mapfile -t synth < <(ours "$MODULE"/src/wrapper/*.cpp "$MODULE"/tests/test_engine.cpp)
g++ "${SAN[@]}" "${objs[@]}" "${synth[@]}" -o "$OUT/test-engine" -lm
"$OUT/test-engine"

echo "== FX engine (ASan/UBSan)"
mapfile -t fx < <(ours "$MODULE"/src/fx/orings_fx.cpp "$MODULE"/tests/test_fx.cpp)
g++ "${SAN[@]}" "${objs[@]}" "${fx[@]}" -o "$MODULE/build/native-fx/test-fx" -lm
"$MODULE/build/native-fx/test-fx"

echo "== plugin smoke (dlopen)"
python3 "$MODULE/tests/smoke.py" "$OUT/dsp.so" "$OUT/contract.json"
echo "== FX plugin smoke (dlopen)"
python3 "$MODULE/tests/smoke_fx.py" "$MODULE/build/native-fx/orings-fx.so" "$MODULE/build/native-fx/contract.json"

echo "== knob grid (Schwung page engine)"
ROOT="${SCHWUNG_ROOT:-$MODULE/../schwung}"
SHARED="${OR_SHARED:-}"
[[ -z "$SHARED" && -d "$MODULE/build/device-shared/shared" ]] && SHARED="$MODULE/build/device-shared/shared"
if [[ ! -f "$ROOT/tools/param-pages/harness.mjs" ]]; then
  echo "SKIP: no Schwung checkout at $ROOT (set SCHWUNG_ROOT)" >&2
else
  for t in test_grid.mjs test_grid_fx.mjs; do
    if command -v node >/dev/null 2>&1; then
      OR_SHARED="$SHARED" SCHWUNG_ROOT="$ROOT" node "$MODULE/tests/$t"
    else
      NODE=node.exe; command -v node.exe >/dev/null 2>&1 || NODE="/mnt/c/Program Files/nodejs/node.exe"
      OR_SHARED="${SHARED:+$(wslpath -w "$SHARED")}" SCHWUNG_ROOT="$(wslpath -w "$ROOT")" \
        WSLENV="OR_SHARED:SCHWUNG_ROOT" "$NODE" "$(wslpath -w "$MODULE/tests/$t")"
    fi
  done
fi

echo "== Move builds (arm64) + ABI"
if command -v "${CROSS_PREFIX:-aarch64-linux-gnu-}g++" >/dev/null 2>&1; then
  bash "$MODULE/scripts/build.sh" arm64
  bash "$MODULE/scripts/build_bench.sh"
else
  echo "SKIP: no aarch64 cross compiler; Move build and ABI not checked" >&2
fi
echo "ALL TESTS PASSED"
