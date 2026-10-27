#!/usr/bin/env bash
# Build the oRings module.
#   scripts/build.sh native   desktop dsp.so for the tests (build/native)
#   scripts/build.sh arm64    Move dsp.so + install tarball (dist/)
# Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
set -euo pipefail
ID=orings
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
# Schwung's plugin API header ships in third_party/schwung (MIT, see its
# LICENSE), unmodified.
API="$MODULE/third_party/schwung"

case "${1:-arm64}" in
  native) CXX="${CXX:-g++}"; CC="${CC:-gcc}"; OUT="$MODULE/build/native" ;;
  arm64) CXX="${CROSS_PREFIX:-aarch64-linux-gnu-}g++"; CC="${CROSS_PREFIX:-aarch64-linux-gnu-}gcc"; OUT="$MODULE/dist/$ID" ;;
  *) echo "Usage: $0 [native|arm64]" >&2; exit 2 ;;
esac

GEN="$MODULE/build/generated"
OBJ="$MODULE/build/obj-${1:-arm64}"
mkdir -p "$OUT" "$GEN" "$OBJ"
python3 "$MODULE/scripts/gen_tables.py" "$GEN/orings_tables.h"
python3 "$MODULE/scripts/metadata.py" "$GEN"

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

objs=()
compile() {  # compile <flags-array-name> <source>
  local -n flags=$1
  local src=$2 obj
  obj="$OBJ/$(echo "${src#$MODULE/src/}" | tr '/' '_').o"
  "$CXX" "${COMMON[@]}" "${flags[@]}" -c "$src" -o "$obj"
  objs+=("$obj")
}
while IFS= read -r f; do
  compile VENDOR "$f"
done < <(find "$MODULE/src/rings/mi" -name '*.cc' | sort)
for f in "$MODULE"/src/dsp_plugin.cpp "$MODULE"/src/wrapper/*.cpp; do
  compile OURS "$f"
done
"$CC" -shared -o "$OUT/dsp.so" "${objs[@]}" -Wl,-z,defs -Wl,-Bsymbolic -Wl,--gc-sections -lm

cp "$MODULE/module.json" "$OUT/"
if [[ "${1:-arm64}" == arm64 ]]; then
  python3 "$MODULE/scripts/check_abi.py" "$OUT/dsp.so"
  for f in help.json README.md LICENSE THIRD_PARTY_LICENSES.md; do
    cp "$MODULE/$f" "$OUT/"
  done
  tar -czf "$MODULE/dist/$ID-module.tar.gz" -C "$MODULE/dist" "$ID"
fi
file "$OUT/dsp.so"
