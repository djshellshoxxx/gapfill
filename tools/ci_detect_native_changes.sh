#!/usr/bin/env bash
# Decide whether a change needs the native (compiled) CI jobs.
#   tools/ci_detect_native_changes.sh <base-sha> <head-sha>
# Prints "native=true" or "native=false". Only documentation-like paths
# (docs/, *.md, LICENSE, .gitignore) are non-native; anything else (sources,
# tests, CMake, packaging, workflows, tools) triggers the native jobs. An
# unknown or all-zero base is treated as "true". GAPFILL_REPO selects the
# repository directory (default: current directory).
set -euo pipefail

BASE="${1:-}"
HEAD="${2:-HEAD}"
REPO="${GAPFILL_REPO:-.}"

if [ -z "$BASE" ] || [ "$BASE" = "0000000000000000000000000000000000000000" ] \
   || ! git -C "$REPO" cat-file -e "$BASE^{commit}" 2>/dev/null; then
    echo "native=true"
    exit 0
fi

changed="$(git -C "$REPO" diff --name-only "$BASE" "$HEAD")"
if [ -z "$changed" ]; then
    echo "native=false"
elif printf '%s\n' "$changed" | grep -Evq '^(docs/|.*\.md$|LICENSE$|\.gitignore$)'; then
    echo "native=true"
else
    echo "native=false"
fi
