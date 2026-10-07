#!/usr/bin/env bash
# Packages GapFill release assets from finished builds.
#   packaging/package_release.sh <windows-build-dir> [<linux-build-dir>] [<out-dir>]
# Windows assets: installer (NSIS), portable zip, VST3 zip, CLAP zip.
# Linux assets (when a Linux build dir is given): portable, VST3 and CLAP zips.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WIN="${1:?windows build dir}"
LIN="${2:-}"
OUT="${3:-$ROOT/dist}"

VERSION_NUM="$(sed -n 's/^project(GapFill VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
STAGE="$(sed -n 's/^set(GAPFILL_RELEASE_STAGE "\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")"
TAG="${VERSION_NUM}${STAGE:+-$STAGE}"
DISPLAY="${VERSION_NUM}${STAGE:+ $STAGE}"

mkdir -p "$OUT"
STAGING="$(mktemp -d)"
trap 'rm -rf "$STAGING"' EXIT

docs () {
    cp "$ROOT/LICENSE" "$1/LICENSE.txt"
    {
        echo "GapFill $DISPLAY - Circuit Drift Labs"
        echo
        echo "VST3:       copy GapFill.vst3 into your VST3 folder (Windows: C:\\Program Files\\Common Files\\VST3, Linux: ~/.vst3)."
        echo "CLAP:       copy GapFill.clap into your CLAP folder (Windows: C:\\Program Files\\Common Files\\CLAP, Linux: ~/.clap)."
        echo "Standalone: run GapFill directly; no installation is required."
        echo
        echo "Full documentation: https://github.com/djshellshoxxx/gapfill"
    } > "$1/README.txt"
}

zipdir () { (cd "$1" && rm -f "$2" && zip -qr9 "$2" .); }

# ---------------- Windows ----------------
W="$STAGING/win"; mkdir -p "$W"
cp -r "$WIN/GapFill_artefacts/Release/VST3/GapFill.vst3" "$W/"
rm -f "$W/GapFill.vst3/desktop.ini" "$W/GapFill.vst3/Plugin.ico"
cp "$WIN/GapFill_artefacts/Release/CLAP/GapFill.clap" "$W/"
cp "$WIN/artefacts/GapFill.exe" "$W/"
cp "$WIN/GapFill_artefacts/JuceLibraryCode/icon.ico" "$W/GapFill.ico"
docs "$W"

makensis -V2 -DVERSION="$DISPLAY" -DSRCDIR="$W" \
         -DOUTFILE="$OUT/GapFill-$TAG-Windows-x64-Setup.exe" "$ROOT/packaging/GapFill.nsi"

for kind in Portable VST3 CLAP; do
    d="$STAGING/w-$kind"; mkdir -p "$d"; docs "$d"
    case $kind in
        Portable) cp -r "$W/GapFill.exe" "$W/GapFill.vst3" "$W/GapFill.clap" "$d/" ;;
        VST3)     cp -r "$W/GapFill.vst3" "$d/" ;;
        CLAP)     cp "$W/GapFill.clap" "$d/" ;;
    esac
    zipdir "$d" "$OUT/GapFill-$TAG-Windows-x64-$kind.zip"
done

# ---------------- Linux ----------------
if [ -n "$LIN" ]; then
    for kind in Portable VST3 CLAP; do
        d="$STAGING/l-$kind"; mkdir -p "$d"; docs "$d"
        case $kind in
            Portable) cp "$LIN/artefacts/GapFill" "$d/" ;;
            VST3)     cp -r "$LIN/GapFill_artefacts/Release/VST3/GapFill.vst3" "$d/" ;;
            CLAP)     cp "$LIN/GapFill_artefacts/Release/CLAP/GapFill.clap" "$d/" ;;
        esac
        zipdir "$d" "$OUT/GapFill-$TAG-Linux-x64-$kind.zip"
    done
fi

(cd "$OUT" && sha256sum GapFill-"$TAG"-* > "GapFill-$TAG-SHA256SUMS.txt")
ls -la "$OUT"
