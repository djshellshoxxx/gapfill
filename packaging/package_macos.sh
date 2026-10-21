#!/usr/bin/env bash
# Packages one verified macOS architecture into a release archive.
#   packaging/package_macos.sh <stage-dir> <arch: arm64|x86_64> [<out-dir>]
# <stage-dir> holds VST3/GapFill.vst3, AU/GapFill.component, CLAP/GapFill.clap
# and Standalone/GapFill.app (the layout CI uploads inside gapfill-macos-<arch>).
# Output: <out-dir>/GapFill-<tag>-macOS-<arch>.zip (no checksum file; the
# publisher writes one combined SHA256SUMS). On macOS every executable is
# re-checked with lipo and codesign; elsewhere (fixtures) set
# GAPFILL_SKIP_NATIVE_CHECKS=1. Env overrides as in package_release.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:?stage dir}"
ARCH="${2:?arch (arm64|x86_64)}"
case "$ARCH" in arm64|x86_64) ;; *) echo "unsupported arch: $ARCH" >&2; exit 2 ;; esac
OUT="$(mkdir -p "${3:-$ROOT/dist}" && cd "${3:-$ROOT/dist}" && pwd)"

VERSION_NUM="$(sed -n 's/^project(GapFill VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
STAGE_NAME="$(sed -n 's/^set(GAPFILL_RELEASE_STAGE "\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")"
TAG="${GAPFILL_ASSET_TAG:-${VERSION_NUM}${STAGE_NAME:+-$STAGE_NAME}}"
DISPLAY="${GAPFILL_DISPLAY_VERSION:-${VERSION_NUM}${STAGE_NAME:+ $STAGE_NAME}}"

VST3="$SRC/VST3/GapFill.vst3"
AU="$SRC/AU/GapFill.component"
CLAP="$SRC/CLAP/GapFill.clap"
APP="$SRC/Standalone/GapFill.app"
for item in "$VST3" "$AU" "$CLAP" "$APP"; do
    test -e "$item" || { echo "missing $ARCH bundle: $item" >&2; exit 1; }
done

if [ "${GAPFILL_SKIP_NATIVE_CHECKS:-0}" != 1 ]; then
    [ "$(uname -s)" = Darwin ] || { echo "native checks need macOS (or set GAPFILL_SKIP_NATIVE_CHECKS=1)" >&2; exit 1; }
    for bundle in "$VST3" "$AU" "$APP"; do
        binary="$bundle/Contents/MacOS/GapFill"
        test -f "$binary"
        chmod +x "$binary"
        lipo "$binary" -verify_arch "$ARCH"
        codesign --verify --deep --strict --verbose=2 "$bundle"
    done
    if [ -d "$CLAP" ]; then
        clapbin="$CLAP/Contents/MacOS/GapFill"
        codesign --verify --deep --strict --verbose=2 "$CLAP"
    else
        clapbin="$CLAP"
    fi
    test -f "$clapbin"
    chmod +x "$clapbin"
    lipo "$clapbin" -verify_arch "$ARCH"
fi

STAGING="$(mktemp -d)"
trap 'rm -rf "$STAGING"' EXIT
PKG="$STAGING/GapFill-macos-$ARCH"
mkdir -p "$PKG"
cp -R "$SRC/VST3" "$SRC/AU" "$SRC/CLAP" "$SRC/Standalone" "$PKG/"
cp "$ROOT/LICENSE" "$PKG/LICENSE.txt"
cat > "$PKG/INSTALL.txt" <<DOC
GapFill $DISPLAY - macOS 12 or newer ($ARCH) - Circuit Drift Labs
Copy VST3/GapFill.vst3 to ~/Library/Audio/Plug-Ins/VST3/
Copy AU/GapFill.component to ~/Library/Audio/Plug-Ins/Components/
Copy CLAP/GapFill.clap to ~/Library/Audio/Plug-Ins/CLAP/
Copy Standalone/GapFill.app to /Applications/
Packages are not Apple-notarized. This is a pre-release beta.
DOC

ARCHIVE="$OUT/GapFill-$TAG-macOS-$ARCH.zip"
rm -f "$ARCHIVE"
if command -v ditto >/dev/null 2>&1; then
    ditto -c -k --sequesterRsrc --keepParent "$PKG" "$ARCHIVE"
else
    (cd "$STAGING" && zip -qry9 "$ARCHIVE" "GapFill-macos-$ARCH")
fi
echo "$ARCHIVE"
