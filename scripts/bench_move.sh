#!/usr/bin/env bash
# Run build/bench-arm64 on the Move (no sftp there: streamed over ssh into
# /data/UserData, never /tmp), then delete it.
#   scripts/bench_move.sh ableton@192.168.1.42 -i ~/.ssh/move_key
# Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
HOST="${1:-ableton@move.local}"
shift || true
BIN=/data/UserData/orings-bench
ssh "$@" "$HOST" "cat > $BIN && chmod +x $BIN" < "$MODULE/build/bench-arm64"
ssh "$@" "$HOST" "taskset 0x7 $BIN; rm -f $BIN"
