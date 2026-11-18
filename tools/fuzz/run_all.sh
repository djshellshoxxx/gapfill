#!/usr/bin/env bash
# Run every GapFillFuzz target for a bounded time each; stop at the first finding.
#   tools/fuzz/run_all.sh [seconds-per-target] [build-dir]
# Crash/timeout/OOM reproducers land in <build-dir>/fuzz-findings/<target>/.
set -euo pipefail
seconds="${1:-300}"
build="${2:-build-fuzz}"
binary="$build/artefacts/GapFillFuzz"
[ -x "$binary" ] || { echo "missing $binary (configure with -DGAPFILL_FUZZ=ON, clang)"; exit 2; }
export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"
for target in state preset recipe profile catalog events ratings; do
    corpus="$build/fuzz-corpus/$target"
    findings="$build/fuzz-findings/$target"
    mkdir -p "$corpus" "$findings"
    seeds=(Tests/fuzz/corpus/"$target")
    [ "$target" = state ] && seeds+=(Tests/fixtures)
    echo "== $target (${seconds}s)"
    GF_FUZZ_TARGET="$target" "$binary" "$corpus" "${seeds[@]}" \
        -max_total_time="$seconds" -max_len=1048576 -timeout=10 -rss_limit_mb=4096 \
        -artifact_prefix="$findings/" -print_final_stats=1 2>&1 | tail -n 12
done
