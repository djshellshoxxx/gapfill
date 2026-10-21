#!/usr/bin/env bash
# Synthetic Windows Release configuration regression test; no JUCE, Windows or NSIS needed.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

WIN="$TMP/win-build"
OUT="$TMP/dist"
mkdir -p "$WIN/GapFill_artefacts/Release/VST3/GapFill.vst3/Contents/x86_64-win" \
         "$WIN/GapFill_artefacts/Release/CLAP" \
         "$WIN/GapFill_artefacts/JuceLibraryCode" \
         "$WIN/artefacts/Release" "$TMP/bin"

printf 'vst3 fixture' > "$WIN/GapFill_artefacts/Release/VST3/GapFill.vst3/Contents/x86_64-win/GapFill.vst3"
printf 'clap fixture' > "$WIN/GapFill_artefacts/Release/CLAP/GapFill.clap"
printf 'exe fixture' > "$WIN/artefacts/Release/GapFill.exe"
printf 'ico fixture' > "$WIN/GapFill_artefacts/JuceLibraryCode/icon.ico"

# Mock only the NSIS compilation; the actual copy, ZIP and checksum paths run unchanged.
cat > "$TMP/bin/makensis" <<'STUB'
#!/usr/bin/env bash
set -euo pipefail
for arg in "$@"; do
    case "$arg" in -DOUTFILE=*) out="${arg#-DOUTFILE=}";; esac
done
: "${out:?missing installer output}"
printf 'fake installer' > "$out"
STUB
chmod +x "$TMP/bin/makensis"

PATH="$TMP/bin:$PATH" bash "$ROOT/packaging/package_release.sh" "$WIN" '' "$OUT" > /dev/null
for a in Setup.exe Portable.zip VST3.zip CLAP.zip; do
    test -s "$OUT/GapFill-0.0.2-beta-Windows-x64-$a"
done
unzip -Z1 "$OUT/GapFill-0.0.2-beta-Windows-x64-Portable.zip" | grep -qx 'GapFill.exe'
unzip -Z1 "$OUT/GapFill-0.0.2-beta-Windows-x64-Portable.zip" | grep -q 'GapFill.vst3/Contents/x86_64-win/GapFill.vst3'
(cd "$OUT" && sha256sum -c GapFill-0.0.2-beta-SHA256SUMS.txt > /dev/null)

# Version override used by release-beta.yml, plus Linux packaging.
LIN="$TMP/lin-build"
mkdir -p "$LIN/artefacts" "$LIN/GapFill_artefacts/Release/VST3/GapFill.vst3/Contents/x86_64-linux" \
         "$LIN/GapFill_artefacts/Release/CLAP"
printf 'elf fixture' > "$LIN/artefacts/GapFill"
printf 'so fixture' > "$LIN/GapFill_artefacts/Release/VST3/GapFill.vst3/Contents/x86_64-linux/GapFill.so"
printf 'clap fixture' > "$LIN/GapFill_artefacts/Release/CLAP/GapFill.clap"
OUT2="$TMP/dist2"
GAPFILL_ASSET_TAG=9.9.9-beta7 GAPFILL_DISPLAY_VERSION='9.9.9 beta7' \
    PATH="$TMP/bin:$PATH" bash "$ROOT/packaging/package_release.sh" "$WIN" "$LIN" "$OUT2" > /dev/null
for a in Windows-x64-Setup.exe Windows-x64-Portable.zip Linux-x64-Portable.zip Linux-x64-VST3.zip Linux-x64-CLAP.zip; do
    test -s "$OUT2/GapFill-9.9.9-beta7-$a"
done
(cd "$OUT2" && sha256sum -c GapFill-9.9.9-beta7-SHA256SUMS.txt > /dev/null)

# macOS archive (lipo/codesign checks are skipped only when explicitly requested off-macOS).
MAC="$TMP/mac-stage"
mkdir -p "$MAC/VST3/GapFill.vst3/Contents/MacOS" "$MAC/AU/GapFill.component/Contents/MacOS" \
         "$MAC/CLAP" "$MAC/Standalone/GapFill.app/Contents/MacOS"
for b in VST3/GapFill.vst3 AU/GapFill.component Standalone/GapFill.app; do
    printf 'macho' > "$MAC/$b/Contents/MacOS/GapFill"
done
printf 'clap' > "$MAC/CLAP/GapFill.clap"
OUT3="$TMP/dist3"
GAPFILL_SKIP_NATIVE_CHECKS=1 GAPFILL_ASSET_TAG=9.9.9-beta7 GAPFILL_DISPLAY_VERSION='9.9.9 beta7' \
    bash "$ROOT/packaging/package_macos.sh" "$MAC" arm64 "$OUT3" > /dev/null
Z="$OUT3/GapFill-9.9.9-beta7-macOS-arm64.zip"
test -s "$Z"
unzip -Z1 "$Z" | grep -q 'AU/GapFill.component/Contents/MacOS/GapFill'
unzip -Z1 "$Z" | grep -q 'Standalone/GapFill.app/Contents/MacOS/GapFill'
unzip -Z1 "$Z" | grep -q 'INSTALL.txt'
if [ "$(uname -s)" != Darwin ]; then
    if bash "$ROOT/packaging/package_macos.sh" "$MAC" arm64 "$TMP/dist4" > /dev/null 2>&1; then
        echo 'FAIL: macOS packaging must not silently skip native checks off-macOS'; exit 1
    fi
fi

echo 'PASS: release packaging (Windows, Linux, macOS synthetic trees)'
