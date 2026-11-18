#!/usr/bin/env bash
# Regression tests for target selection in tools/dev/build-target.sh, run against a mock cmake
# and a mock test binary (no compiler needed). Adapted from tools/test_dev_build.sh on
# codex/fast-modular-build-slices-20261020 (afaa3c4), retargeted at build-target.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT="$ROOT/tools/dev/build-target.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/bin" "$TMP/build/artefacts/Release"
: > "$TMP/build/CMakeCache.txt"
cat > "$TMP/bin/cmake" <<'MOCK'
#!/usr/bin/env bash
if [[ " $* " == *" --target help "* ]]; then
    printf '%s\n' 'GapFillTests: phony' 'GapFill_VST3: phony' 'GapFill_CLAP: phony' 'GapFill_Standalone: phony' 'GapFill_AU: phony'
    exit 0
fi
printf '%s\n' "$*" >> "$BUILD_LOG"
MOCK
cat > "$TMP/build/artefacts/Release/GapFillTests" <<'MOCK'
#!/usr/bin/env bash
printf 'filter=%s\n' "$*" >> "$TEST_LOG"
MOCK
chmod +x "$TMP/bin/cmake" "$TMP/build/artefacts/Release/GapFillTests"
export PATH="$TMP/bin:$PATH" BUILD_LOG="$TMP/build.log" TEST_LOG="$TMP/test.log"

fail() { echo "FAIL: $*" >&2; exit 1; }

expect_targets() {   # mode, then every target that must be built
    local mode="$1"; shift
    : > "$BUILD_LOG"
    JOBS=3 bash "$SCRIPT" "$TMP/build" "$mode" >/dev/null
    grep -Fq -- "--parallel 3" "$BUILD_LOG" || fail "'$mode' did not pass --parallel 3: $(cat "$BUILD_LOG")"
    for target in "$@"; do
        grep -Fq -- "$target" "$BUILD_LOG" || fail "'$mode' did not build '$target': $(cat "$BUILD_LOG")"
    done
}

expect_targets tests GapFillTests
expect_targets plugin GapFill_VST3 GapFill_Standalone
expect_targets vst3 GapFill_VST3
expect_targets clap GapFill_CLAP
expect_targets standalone GapFill_Standalone
expect_targets au GapFill_AU
expect_targets all GapFillTests GapFill_VST3 GapFill_Standalone GapFill_CLAP GapFill_AU

: > "$BUILD_LOG"
JOBS=1 bash "$SCRIPT" "$TMP/build" vst3 >/dev/null
if grep -Fq GapFillTests "$BUILD_LOG"; then fail "'vst3' also built GapFillTests"; fi

# tests mode runs the built binary with the default filter, or the one given
: > "$TEST_LOG"
JOBS=1 bash "$SCRIPT" "$TMP/build" tests >/dev/null
grep -Fqx "filter=-Combinatorial" "$TEST_LOG" || fail "default test filter: $(cat "$TEST_LOG")"
: > "$TEST_LOG"
JOBS=1 bash "$SCRIPT" "$TMP/build" tests Protection >/dev/null
grep -Fqx "filter=Protection" "$TEST_LOG" || fail "explicit test filter: $(cat "$TEST_LOG")"

for bad in unknown; do
    if JOBS=1 bash "$SCRIPT" "$TMP/build" "$bad" >/dev/null 2>&1; then fail "mode '$bad' succeeded"; fi
done
for jobs in 0 -1 abc 1.5; do
    if JOBS="$jobs" bash "$SCRIPT" "$TMP/build" tests >/dev/null 2>&1; then fail "JOBS='$jobs' succeeded"; fi
done
if JOBS=1 bash "$SCRIPT" "$TMP/missing" tests >/dev/null 2>&1; then fail "unconfigured build directory succeeded"; fi

# an explicitly requested CLAP / AU build fails when the target is not configured
sed -i -e "s/ 'GapFill_CLAP: phony'//" -e "s/ 'GapFill_AU: phony'//" "$TMP/bin/cmake"
if JOBS=1 bash "$SCRIPT" "$TMP/build" clap >/dev/null 2>&1; then fail "missing CLAP target succeeded"; fi
if JOBS=1 bash "$SCRIPT" "$TMP/build" au >/dev/null 2>&1; then fail "missing AU target succeeded"; fi
expect_targets all GapFillTests GapFill_VST3 GapFill_Standalone
if grep -Fq -e GapFill_CLAP -e GapFill_AU "$BUILD_LOG"; then fail "'all' built an unconfigured target"; fi

echo "PASS: developer build target selection"
