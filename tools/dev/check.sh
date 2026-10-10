#!/usr/bin/env bash
# GapFill local gate: mirrors CI, fastest checks first, stops at the first failure.
# Usage: tools/dev/check.sh [build-dir] [test-filter]
#   build-dir    default build-dev (run tools/dev/bootstrap.sh first)
#   test-filter  optional GapFillTests filter (group or name substring); default = everything except Combinatorial
# Env: JOBS (default 2, safe on 4-8 GB machines), PLUGINVAL=/path/to/pluginval (optional extra gate)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-build-dev}"
FILTER="${2:-}"
JOBS="${JOBS:-2}"

cd "$ROOT"

step() { printf '\n==> %s\n' "$1"; }

step "1/4 Repository preflight"
python3 tools/validate_repo.py
python3 -m unittest tools/test_validate_repo.py

if [ ! -d "${BUILD_DIR}" ]; then
    echo "Build dir '${BUILD_DIR}' not found. Run tools/dev/bootstrap.sh first." >&2
    exit 2
fi

step "2/4 Build (tests + plugin targets), -j${JOBS}"
TARGETS=(GapFillTests GapFill_VST3 GapFill_Standalone)
if grep -q "GapFill_CLAP" "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null || cmake --build "${BUILD_DIR}" --target help 2>/dev/null | grep -q "GapFill_CLAP"; then
    TARGETS+=(GapFill_CLAP)
fi
cmake --build "${BUILD_DIR}" --target "${TARGETS[@]}" -- -j"${JOBS}"

step "3/4 Unit tests"
TEST_BIN="$(find "${BUILD_DIR}" -type f -name GapFillTests -perm -u+x | head -n 1)"
if [ -z "${TEST_BIN}" ]; then
    echo "GapFillTests binary not found under ${BUILD_DIR}" >&2
    exit 3
fi
if [ -n "${FILTER}" ]; then
    "${TEST_BIN}" "${FILTER}"
else
    # Everything except the ~13 minute Combinatorial group. If the runner has no
    # exclusion syntax yet (see 03-SPEC-ESSENTIALS E-4), run the groups explicitly.
    if "${TEST_BIN}" "-Combinatorial" >/dev/null 2>&1; then
        "${TEST_BIN}" "-Combinatorial"
    else
        echo "(runner has no '-' exclusion yet: running full suite, this can take a long time)"
        "${TEST_BIN}"
    fi
fi

step "4/4 Plugin validation (optional)"
if [ -n "${PLUGINVAL:-}" ]; then
    VST3="$(find "${BUILD_DIR}" -type d -name 'GapFill.vst3' | head -n 1)"
    "${PLUGINVAL}" --strictness-level 5 --validate "${VST3}"
else
    echo "PLUGINVAL not set - skipped (CI runs strictness 10)."
fi

printf '\nAll local gates passed. Remember: real-DAW / hardware-MIDI checks are NOT covered here.\n'
