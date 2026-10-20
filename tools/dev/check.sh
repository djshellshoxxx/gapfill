#!/usr/bin/env bash
# GapFill local gate: preflight plus a selected build/test slice.
# Usage: tools/dev/check.sh [build-dir] [test-filter] [component]
#   build-dir    default build-dev (run tools/dev/bootstrap.sh first)
#   test-filter  optional GapFillTests filter; default quick run excludes Combinatorial
#   component    tests, vst3, clap, standalone, au, all (default all)
# Env: JOBS (default 2, safe on 4-8 GB machines), PLUGINVAL=/path/to/pluginval (optional extra gate)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-build-dev}"
FILTER="${2:-}"
COMPONENT="${3:-all}"
JOBS="${JOBS:-2}"

cd "$ROOT"

step() { printf '\n==> %s\n' "$1"; }

step "1/4 Repository preflight"
python3 tools/validate_repo.py
python3 -m unittest tools/test_validate_repo.py

if [ "$COMPONENT" = preflight ]; then
    exit 0
fi

if [ ! -d "${BUILD_DIR}" ]; then
    echo "Build dir '${BUILD_DIR}' not found. Run tools/dev/bootstrap.sh first." >&2
    exit 2
fi

step "2/4 Build component '${COMPONENT}', parallel jobs=${JOBS}"
JOBS="$JOBS" tools/dev/build.sh "$BUILD_DIR" "$COMPONENT"

if [ "$COMPONENT" = all ] || [ "$COMPONENT" = tests ]; then
    step "3/4 Unit tests"
    TEST_BIN="$(find "${BUILD_DIR}" -type f -name GapFillTests -perm -u+x | head -n 1)"
    if [ -z "${TEST_BIN}" ]; then
        echo "GapFillTests binary not found under ${BUILD_DIR}" >&2
        exit 3
    fi
    if [ -n "${FILTER}" ]; then
        "${TEST_BIN}" "${FILTER}"
    else
        # The -Combinatorial exclusion provides a quick meaningful local gate.
        # Do not probe by executing the suite first: that runs tests twice and can
        # silently mask an invalid filter on older runners.
        "${TEST_BIN}" "-Combinatorial"
    fi
else
    step "3/4 Unit tests skipped (component '${COMPONENT}' does not include GapFillTests)"
fi

if [ "$COMPONENT" = all ] || [ "$COMPONENT" = vst3 ]; then
    step "4/4 Plugin validation (optional)"
elif [ "$COMPONENT" = preflight ]; then
    exit 0
else
    step "4/4 Plugin validation skipped for component '${COMPONENT}'"
fi
if { [ "$COMPONENT" = all ] || [ "$COMPONENT" = vst3 ]; } && [ -n "${PLUGINVAL:-}" ]; then
    VST3="$(find "${BUILD_DIR}" -type d -name 'GapFill.vst3' | head -n 1)"
    if [ -z "$VST3" ]; then
        echo "GapFill VST3 bundle not found under ${BUILD_DIR}" >&2
        exit 4
    fi
    "${PLUGINVAL}" --strictness-level 10 --validate "${VST3}"
elif { [ "$COMPONENT" = all ] || [ "$COMPONENT" = vst3 ]; } && [ -z "${PLUGINVAL:-}" ]; then
    echo "PLUGINVAL not set - skipped (CI runs strictness 10)."
fi

printf '\nAll local gates passed. Remember: real-DAW / hardware-MIDI checks are NOT covered here.\n'
