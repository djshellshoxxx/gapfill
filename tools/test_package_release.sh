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
    test -s "$OUT/GapFill-0.0.1-beta-Windows-x64-$a"
done
unzip -Z1 "$OUT/GapFill-0.0.1-beta-Windows-x64-Portable.zip" | grep -qx 'GapFill.exe'
unzip -Z1 "$OUT/GapFill-0.0.1-beta-Windows-x64-Portable.zip" | grep -q 'GapFill.vst3/Contents/x86_64-win/GapFill.vst3'
(cd "$OUT" && sha256sum -c GapFill-0.0.1-beta-SHA256SUMS.txt > /dev/null)

echo 'PASS: Windows release packaging (synthetic multi-config tree)'
