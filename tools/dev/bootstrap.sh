#!/usr/bin/env bash
# GapFill developer bootstrap: fetch pinned dependencies and configure a Release build.
# Safe to re-run. Usage: tools/dev/bootstrap.sh [build-dir]   (default: build-dev)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-build-dev}"
JUCE_TAG="8.0.15"
CLAP_EXT_COMMIT="55525c9858d4b25687be7759a5e0f70eccef218e"   # keep in sync with .github/workflows/ci.yml

cd "$ROOT"

if [ ! -d JUCE ]; then
    echo "==> Cloning JUCE ${JUCE_TAG}"
    git clone --depth 1 --branch "${JUCE_TAG}" https://github.com/juce-framework/JUCE JUCE
else
    echo "==> JUCE already present"
fi

if [ ! -f clap-juce-extensions/CMakeLists.txt ]; then
    echo "==> Fetching clap-juce-extensions @ ${CLAP_EXT_COMMIT}"
    rm -rf clap-juce-extensions
    git clone --filter=blob:none --no-checkout https://github.com/free-audio/clap-juce-extensions.git clap-juce-extensions
    git -C clap-juce-extensions checkout "${CLAP_EXT_COMMIT}"
    git -C clap-juce-extensions submodule update --init --recursive --depth 1
else
    echo "==> clap-juce-extensions already present"
fi

GEN=()
if command -v ninja >/dev/null 2>&1; then GEN=(-G Ninja); fi

echo "==> Configuring ${BUILD_DIR}"
cmake -S . -B "${BUILD_DIR}" "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release

cat <<MSG

Done. Next:
  tools/dev/check.sh ${BUILD_DIR}      # preflight + build + tests (same gates as CI, fast subset)
MSG
