#!/usr/bin/env bash
# Regenerates Tests/golden/<platform>/*.f32 from the current build.
#
# Golden files may only change in a PR whose title contains [golden] and whose
# description states which feature intentionally changed the sound
# (tools/check_hot_files.py enforces this). Missing state fixtures are written
# once; existing ones are never overwritten.
#
# Usage: tools/dev/write_golden.sh [build-dir]    (default: build-ci)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="${1:-$root/build-ci}"
exe="$build/artefacts/GapFillTests"

if [ ! -x "$exe" ]; then
    echo "Build GapFillTests first: cmake --build $build --target GapFillTests" >&2
    exit 1
fi

if command -v xvfb-run >/dev/null 2>&1 && [ -z "${DISPLAY:-}" ]; then
    xvfb-run -a "$exe" --write-golden
else
    "$exe" --write-golden
fi
