#!/usr/bin/env bash
# Same-runner proof that the branch renders the golden scenarios like master.
#   tools/ci_golden_vs_master.sh <branch-test-exe> <master-sha> [work-dir]
# Builds GapFillTests at <master-sha> with this branch's golden render rig
# (TestRig + TestMain) overlaid, renders scenarios A..F with both executables on
# THIS machine and requires max abs difference <= 1e-6. Needs the repository
# root as cwd, JUCE and clap-juce-extensions present, full git history.
set -euo pipefail
EXE="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
SHA="$2"
WORK="${3:-$PWD/golden-compare}"
ROOT="$PWD"
rm -rf "$WORK"; mkdir -p "$WORK"
git worktree prune; git worktree add --detach "$WORK/master-src" "$SHA" >/dev/null
cd "$WORK/master-src"
ln -s "$ROOT/JUCE" JUCE
ln -s "$ROOT/clap-juce-extensions" clap-juce-extensions
cp "$ROOT/Tests/support/TestRig.h" "$ROOT/Tests/support/TestRig.cpp" Tests/support/ 2>/dev/null || {
  mkdir -p Tests/support; cp "$ROOT/Tests/support/TestRig.h" "$ROOT/Tests/support/TestRig.cpp" Tests/support/; }
cp "$ROOT/Tests/TestMain.cpp" Tests/TestMain.cpp
grep -q 'Tests/support/TestRig.cpp' CMakeLists.txt || sed -i 's#Tests/TestMain.cpp#Tests/TestMain.cpp\n    Tests/support/TestRig.cpp#' CMakeLists.txt
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  ${CMAKE_LAUNCHER_ARGS:-} >/dev/null
cmake --build build --target GapFillTests -- -j"$(nproc)" >"$WORK/master-build.log" 2>&1 || { tail -30 "$WORK/master-build.log"; exit 1; }
cd "$ROOT"
xvfb-run -a "$WORK/master-src/build/artefacts/GapFillTests" --dump-golden "$WORK/master-out"
xvfb-run -a "$EXE" --dump-golden "$WORK/head-out"
echo "fills master:"; cat "$WORK/master-out/fills.txt"
echo "fills head:";   cat "$WORK/head-out/fills.txt"
cmp "$WORK/master-out/fills.txt" "$WORK/head-out/fills.txt"
python3 tools/compare_golden_dirs.py "$WORK/master-out" "$WORK/head-out" --max-abs 1e-6
# Also show distance of this runner's render from the checked-in baseline.
echo "head vs checked-in baseline (informational):"
python3 tools/compare_golden_dirs.py "$WORK/head-out" Tests/golden/linux --max-abs 1e9 || true
