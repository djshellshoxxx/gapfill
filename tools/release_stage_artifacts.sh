#!/usr/bin/env bash
# Rebuild a build-tree-shaped directory from a downloaded CI artifact so that
# packaging/package_release.sh can consume it, and verify the binary formats.
#   tools/release_stage_artifacts.sh <windows|linux> <raw-artifact-dir> <build-dir>
# Artifact layout is located with find (zip upload drops executable bits and
# the common-prefix depth is an upload-artifact detail), so nothing here
# depends on it. Fails on a missing file or a wrong object format/architecture
# (PE32+ x86-64 for Windows, ELF x86-64 for Linux).
set -euo pipefail

OS="${1:?windows|linux}"
RAW="${2:?raw artifact dir}"
DEST="${3:?build dir to create}"
case "$OS" in windows|linux) ;; *) echo "unsupported os: $OS" >&2; exit 2 ;; esac

first_dir ()  { find "$RAW" -type d -name "$1" -print -quit; }
first_file () { find "$RAW" -type f -name "$1" -not -path '*GapFill.vst3/*' -print -quit; }

vst3="$(first_dir GapFill.vst3)"
clap="$(find "$RAW" -type f -name GapFill.clap -print -quit)"
test -n "$vst3" && test -d "$vst3" || { echo "::error::$OS artifact lacks GapFill.vst3" >&2; exit 1; }
test -n "$clap" && test -s "$clap" || { echo "::error::$OS artifact lacks GapFill.clap" >&2; exit 1; }

mkdir -p "$DEST/GapFill_artefacts/Release/VST3" "$DEST/GapFill_artefacts/Release/CLAP"
cp -R "$vst3" "$DEST/GapFill_artefacts/Release/VST3/GapFill.vst3"
cp "$clap" "$DEST/GapFill_artefacts/Release/CLAP/GapFill.clap"

if [ "$OS" = windows ]; then
    exe="$(first_file GapFill.exe)"
    ico="$(first_file icon.ico)"
    test -n "$exe" && test -s "$exe" || { echo "::error::windows artifact lacks GapFill.exe" >&2; exit 1; }
    test -n "$ico" && test -s "$ico" || { echo "::error::windows artifact lacks icon.ico" >&2; exit 1; }
    mkdir -p "$DEST/artefacts/Release" "$DEST/GapFill_artefacts/JuceLibraryCode"
    cp "$exe" "$DEST/artefacts/Release/GapFill.exe"
    cp "$ico" "$DEST/GapFill_artefacts/JuceLibraryCode/icon.ico"
else
    exe="$(first_file GapFill)"
    test -n "$exe" && test -s "$exe" || { echo "::error::linux artifact lacks GapFill standalone" >&2; exit 1; }
    mkdir -p "$DEST/artefacts"
    cp "$exe" "$DEST/artefacts/GapFill"
    chmod +x "$DEST/artefacts/GapFill"
fi

python3 -I - "$OS" "$DEST" <<'PY'
import struct, sys
from pathlib import Path

os_name, dest = sys.argv[1], Path(sys.argv[2])
rel = dest / "GapFill_artefacts" / "Release"
vst3_bins = [p for p in (rel / "VST3" / "GapFill.vst3").rglob("*")
             if p.is_file() and p.suffix == (".vst3" if os_name == "windows" else ".so")]
assert len(vst3_bins) == 1, f"{os_name}: expected exactly one VST3 binary, found {vst3_bins}"
files = [vst3_bins[0], rel / "CLAP" / "GapFill.clap",
         dest / "artefacts" / "Release" / "GapFill.exe" if os_name == "windows" else dest / "artefacts" / "GapFill"]
for f in files:
    b = f.read_bytes()
    if os_name == "windows":
        assert b.startswith(b"MZ"), f"{f}: not a PE file"
        off = struct.unpack_from("<I", b, 0x3C)[0]
        assert b[off:off + 4] == b"PE\0\0", f"{f}: bad PE signature"
        assert struct.unpack_from("<H", b, off + 4)[0] == 0x8664, f"{f}: not x86-64"
    else:
        assert b[:4] == b"\x7fELF", f"{f}: not an ELF file"
        assert struct.unpack_from("<H", b, 18)[0] == 62, f"{f}: not x86-64"
    print(f"verified {os_name}: {f.name} ({len(b)} bytes)")
PY
