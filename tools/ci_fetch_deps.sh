#!/usr/bin/env bash
# Fetch the pinned JUCE and clap-juce-extensions checkouts used by every CI job.
# Idempotent: when the directories were restored from actions/cache and already
# sit on the pinned revisions, nothing is downloaded.
#   tools/ci_fetch_deps.sh            (run from the repository root)
# The workflows hash this file into their cache keys, so changing a pin below
# automatically invalidates the cached checkouts.
set -euo pipefail

JUCE_TAG="${JUCE_TAG:-8.0.15}"
CLAP_EXT_SHA="${CLAP_EXT_SHA:-55525c9858d4b25687be7759a5e0f70eccef218e}"

if [ -f JUCE/CMakeLists.txt ] && [ "$(git -C JUCE describe --tags --exact-match 2>/dev/null || true)" = "$JUCE_TAG" ]; then
    echo "JUCE $JUCE_TAG already present (cache hit)"
else
    rm -rf JUCE
    git clone --depth 1 --branch "$JUCE_TAG" https://github.com/juce-framework/JUCE JUCE
fi

if [ -d clap-juce-extensions/.git ] && [ "$(git -C clap-juce-extensions rev-parse HEAD 2>/dev/null || true)" = "$CLAP_EXT_SHA" ]; then
    echo "clap-juce-extensions $CLAP_EXT_SHA already present (cache hit)"
else
    rm -rf clap-juce-extensions
    git clone --filter=blob:none --no-checkout https://github.com/free-audio/clap-juce-extensions.git clap-juce-extensions
    git -C clap-juce-extensions checkout "$CLAP_EXT_SHA"
fi
git -C clap-juce-extensions submodule update --init --recursive --depth 1
