#!/usr/bin/env bash
# Build both modules of this repository from one compile of the Rings DSP:
#   oRings     (sound generator, dsp.so)
#   oRings FX  (audio FX, orings-fx.so; the chain host loads <id>.so)
#   scripts/build.sh native   desktop builds for the tests (build/native,
#                             build/native-fx)
#   scripts/build.sh arm64    Move builds + install tarballs (dist/)
# Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
# Schwung's plugin API header ships in third_party/schwung (MIT, see its
# LICENSE), unmodified.
API="$MODULE/third_party/schwung"

case "${1:-arm64}" in
  native) CXX="${CXX:-g++}"; CC="${CC:-gcc}"; OUT="$MODULE/build/native"; OUT_FX="$MODULE/build/native-fx" ;;
  arm64) CXX="${CROSS_PREFIX:-aarch64-linux-gnu-}g++"; CC="${CROSS_PREFIX:-aarch64-linux-gnu-}gcc"
         OUT="$MODULE/dist/orings"; OUT_FX="$MODULE/dist/orings-fx" ;;
  *) echo "Usage: $0 [native|arm64]" >&2; exit 2 ;;
esac

GEN="$MODULE/build/generated"
OBJ="$MODULE/build/obj-${1:-arm64}"
mkdir -p "$OUT" "$OUT_FX" "$GEN" "$OBJ"
python3 "$MODULE/scripts/gen_tables.py" "$GEN/orings_tables.h"
python3 "$MODULE/scripts/metadata.py" "$GEN"
python3 "$MODULE/scripts/fx_metadata.py" "$GEN"

# -DTEST: stmlib's switch for a non-MCU build (no ".ramtext" placement).
# -include stdio.h: as for the other Mutable ports, so a TEST branch's
#   printf compiles without editing the file.
# -fvisibility=hidden + -Bsymbolic: only move_plugin_init_v2 is exported and
#   every internal call binds locally, so another Mutable-based module loaded
#   in the same process (oPlaits, oClouds...) can never interpose its
#   stmlib:: symbols on ours.
# -ffp-contract=off: no fused multiply-adds the x86 build would not also make,
#   so the desktop tests and the Move compute the same arithmetic as written.
# No exceptions/RTTI and linked with the C driver: no libstdc++ at runtime.
COMMON=(-O2 -fPIC -fno-strict-aliasing -fno-math-errno -ffp-contract=off -DTEST
        -fvisibility=hidden -fvisibility-inlines-hidden
        -fno-exceptions -fno-rtti -fno-threadsafe-statics
        -I"$MODULE/src" -isystem "$MODULE/src/rings/mi" -I"$GEN" -I"$API")
OURS=(-std=gnu++17 -Wall -Wextra -Werror)
VENDOR=(-std=gnu++11 -w -include stdio.h)   # third-party, compiled as shipped

compile() {  # compile <flags-array-name> <source>; prints the object
  local -n flags=$1
  local src=$2 obj
  obj="$OBJ/$(echo "${src#$MODULE/src/}" | tr '/' '_').o"
  "$CXX" "${COMMON[@]}" "${flags[@]}" -c "$src" -o "$obj"
  echo "$obj"
}
dsp=()   # the Rings DSP, shared by both modules
while IFS= read -r f; do
  dsp+=("$(compile VENDOR "$f")")
done < <(find "$MODULE/src/rings/mi" -name '*.cc' | sort)
synth=("$(compile OURS "$MODULE/src/dsp_plugin.cpp")" "$(compile OURS "$MODULE/src/wrapper/orings.cpp")")
fx=("$(compile OURS "$MODULE/src/fx_plugin.cpp")" "$(compile OURS "$MODULE/src/fx/orings_fx.cpp")")
LINK=(-shared -Wl,-z,defs -Wl,-Bsymbolic -Wl,--gc-sections -lm)
"$CC" -o "$OUT/dsp.so" "${dsp[@]}" "${synth[@]}" "${LINK[@]}"
"$CC" -o "$OUT_FX/orings-fx.so" "${dsp[@]}" "${fx[@]}" "${LINK[@]}"

cp "$MODULE/module.json" "$OUT/"
cp "$MODULE/fx/module.json" "$OUT_FX/"
if [[ "${1:-arm64}" == arm64 ]]; then
  python3 "$MODULE/scripts/check_abi.py" "$OUT/dsp.so" move_plugin_init_v2
  python3 "$MODULE/scripts/check_abi.py" "$OUT_FX/orings-fx.so" move_audio_fx_init_v2
  for f in help.json README.md LICENSE THIRD_PARTY_LICENSES.md; do
    cp "$MODULE/$f" "$OUT/"
  done
  for f in LICENSE THIRD_PARTY_LICENSES.md; do cp "$MODULE/$f" "$OUT_FX/"; done
  for f in help.json README.md; do cp "$MODULE/fx/$f" "$OUT_FX/"; done
  tar -czf "$MODULE/dist/orings-module.tar.gz" -C "$MODULE/dist" orings
  tar -czf "$MODULE/dist/orings-fx-module.tar.gz" -C "$MODULE/dist" orings-fx
fi
file "$OUT/dsp.so" "$OUT_FX/orings-fx.so"
