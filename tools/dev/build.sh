#!/usr/bin/env bash
# Build one GapFill deliverable at a time, or all configured deliverables.
# Usage: tools/dev/build.sh [build-dir] [tests|vst3|clap|standalone|au|all]
# Env: JOBS (default 2)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-build-dev}"
COMPONENT="${2:-all}"
JOBS="${JOBS:-2}"

cd "$ROOT"

if ! [[ "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "JOBS must be a positive integer (received: $JOBS)" >&2
    exit 2
fi

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "Build directory '$BUILD_DIR' is not configured. Run tools/dev/bootstrap.sh $BUILD_DIR first." >&2
    exit 2
fi

case "$COMPONENT" in
    tests) TARGETS=(GapFillTests) ;;
    vst3) TARGETS=(GapFill_VST3) ;;
    clap) TARGETS=(GapFill_CLAP) ;;
    standalone) TARGETS=(GapFill_Standalone) ;;
    au) TARGETS=(GapFill_AU) ;;
    all) TARGETS=(GapFillTests GapFill_VST3 GapFill_Standalone) ;;
    *)
        echo "Unknown component '$COMPONENT'. Choose: tests, vst3, clap, standalone, au, all." >&2
        exit 2
        ;;
esac

AVAILABLE_TARGETS="$(cmake --build "$BUILD_DIR" --target help 2>/dev/null || true)"
for target in "${TARGETS[@]}"; do
    if ! grep -Eq "(^|[[:space:]])${target}([[:space:]:]|$)" <<< "$AVAILABLE_TARGETS"; then
        case "$target" in
            GapFill_CLAP)
                echo "CLAP target is not configured. Check clap-juce-extensions and re-run tools/dev/bootstrap.sh $BUILD_DIR." >&2
                ;;
            GapFill_AU)
                echo "AU target is available only in an Apple build." >&2
                ;;
            *)
                echo "Configured build does not expose required target '$target'. Reconfigure and inspect CMake output." >&2
                ;;
        esac
        exit 3
    fi
done

if [ "$COMPONENT" = all ] && grep -Eq '(^|[[:space:]])GapFill_CLAP([[:space:]:]|$)' <<< "$AVAILABLE_TARGETS"; then
    TARGETS+=(GapFill_CLAP)
fi
if [ "$COMPONENT" = all ] && grep -Eq '(^|[[:space:]])GapFill_AU([[:space:]:]|$)' <<< "$AVAILABLE_TARGETS"; then
    TARGETS+=(GapFill_AU)
fi

printf 'Building component: %s\nTargets: %s\nParallel jobs: %s\n' \
    "$COMPONENT" "${TARGETS[*]}" "$JOBS"
cmake --build "$BUILD_DIR" --parallel "$JOBS" --target "${TARGETS[@]}"
