#!/usr/bin/env bash
# Regression tests for target selection in tools/dev/build.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/bin" "$TMP/build"
: > "$TMP/build/CMakeCache.txt"
cat > "$TMP/bin/cmake" <<'MOCK'
#!/usr/bin/env bash
if [[ " $* " == *" --target help "* ]]; then
    cat <<'TARGETS'
GapFillTests: phony
GapFill_VST3: phony
GapFill_CLAP: phony
GapFill_Standalone: phony
GapFill_AU: phony
TARGETS
    exit 0
fi
printf '%s\n' "$*" >> "$BUILD_LOG"
MOCK
chmod +x "$TMP/bin/cmake"
export PATH="$TMP/bin:$PATH"
export BUILD_LOG="$TMP/build.log"

expect_build_target() {
    local component="$1" target="$2"
    : > "$BUILD_LOG"
    JOBS=3 "$ROOT/tools/dev/build.sh" "$TMP/build" "$component" >/dev/null
    grep -Fq -- "--target $target" "$BUILD_LOG" || {
        echo "Expected '$component' to build '$target'; got: $(cat "$BUILD_LOG")" >&2
        exit 1
    }
}

expect_build_target tests GapFillTests
expect_build_target vst3 GapFill_VST3
expect_build_target clap GapFill_CLAP
expect_build_target standalone GapFill_Standalone
expect_build_target au GapFill_AU

: > "$BUILD_LOG"
JOBS=2 "$ROOT/tools/dev/build.sh" "$TMP/build" all >/dev/null
for target in GapFillTests GapFill_VST3 GapFill_CLAP GapFill_Standalone GapFill_AU; do
    grep -Fq "$target" "$BUILD_LOG" || {
        echo "Expected 'all' to include '$target'; got: $(cat "$BUILD_LOG")" >&2
        exit 1
    }
done

if "$ROOT/tools/dev/build.sh" "$TMP/build" unknown >/dev/null 2>&1; then
    echo "Unknown component unexpectedly succeeded" >&2
    exit 1
fi
if JOBS=0 "$ROOT/tools/dev/build.sh" "$TMP/build" tests >/dev/null 2>&1; then
    echo "Invalid JOBS unexpectedly succeeded" >&2
    exit 1
fi
if "$ROOT/tools/dev/build.sh" "$TMP/missing" tests >/dev/null 2>&1; then
    echo "Unconfigured build directory unexpectedly succeeded" >&2
    exit 1
fi

mkdir -p "$TMP/no-clap"
: > "$TMP/no-clap/CMakeCache.txt"
sed -i '/GapFill_CLAP/d' "$TMP/bin/cmake"
if "$ROOT/tools/dev/build.sh" "$TMP/no-clap" clap >/dev/null 2>&1; then
    echo "Missing explicit CLAP target unexpectedly succeeded" >&2
    exit 1
fi

bash -n "$ROOT/tools/dev/build.sh" "$ROOT/tools/dev/check.sh"
echo "PASS: developer build target selection"
