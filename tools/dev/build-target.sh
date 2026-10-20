#!/usr/bin/env bash
# Incremental developer build: compile only requested targets, preserving the CMake cache.
# Usage: tools/dev/build-target.sh [build-dir] [tests|plugin|vst3|clap|standalone|all] [filter]
# JOBS=2 by default, CONFIG=Release for multi-config generators.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-build-dev}"
MODE="${2:-tests}"
FILTER="${3:--Combinatorial}"
JOBS="${JOBS:-2}"
CONFIG="${CONFIG:-Release}"

case "$JOBS" in ''|*[!0-9]*) echo 'JOBS must be a positive integer' >&2; exit 2;; esac
if (( JOBS < 1 )); then echo 'JOBS must be a positive integer' >&2; exit 2; fi
case "$MODE" in
  tests) TARGETS=(GapFillTests);;
  plugin) TARGETS=(GapFill_VST3 GapFill_Standalone);;
  vst3) TARGETS=(GapFill_VST3);;
  clap) TARGETS=(GapFill_CLAP);;
  standalone) TARGETS=(GapFill_Standalone);;
  all) TARGETS=(GapFillTests GapFill_VST3 GapFill_Standalone);;
  *) echo "Unknown target group: $MODE" >&2; exit 2;;
esac
cd "$ROOT"
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  echo "No configured CMake build at $BUILD_DIR. Run tools/dev/bootstrap.sh first." >&2
  exit 2
fi
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"
if [[ "$MODE" == clap || "$MODE" == all ]]; then
  if ! cmake --build "$BUILD_DIR" --target help | grep -q 'GapFill_CLAP'; then
    if [[ "$MODE" == clap ]]; then
      echo 'CLAP target unavailable. Check clap-juce-extensions and reconfigure.' >&2
      exit 2
    fi
  else
    if [[ "$MODE" == all ]]; then TARGETS+=(GapFill_CLAP); fi
  fi
fi
printf 'Building: %s (jobs=%s, config=%s)\n' "${TARGETS[*]}" "$JOBS" "$CONFIG"
cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel "$JOBS" --target "${TARGETS[@]}"
if [[ "$MODE" == tests || "$MODE" == all ]]; then
  if [[ -x "$BUILD_DIR/artefacts/$CONFIG/GapFillTests.exe" ]]; then
    TEST_BIN="$BUILD_DIR/artefacts/$CONFIG/GapFillTests.exe"
  elif [[ -x "$BUILD_DIR/artefacts/$CONFIG/GapFillTests" ]]; then
    TEST_BIN="$BUILD_DIR/artefacts/$CONFIG/GapFillTests"
  elif [[ -x "$BUILD_DIR/artefacts/GapFillTests" ]]; then
    TEST_BIN="$BUILD_DIR/artefacts/GapFillTests"
  else
    echo 'Built test binary was not found in the expected configuration.' >&2
    exit 3
  fi
  "$TEST_BIN" "$FILTER"
fi
