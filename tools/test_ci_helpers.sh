#!/usr/bin/env bash
# Regression test for tools/ci_detect_native_changes.sh using a throwaway repo.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
export GAPFILL_REPO="$TMP"
g () { git -C "$TMP" -c user.email=t@t -c user.name=t "$@"; }
detect () { bash "$ROOT/tools/ci_detect_native_changes.sh" "$@"; }

g init -q
mkdir -p "$TMP/docs" "$TMP/Source" "$TMP/.github/workflows"
echo a > "$TMP/Source/a.cpp"; echo a > "$TMP/docs/a.md"
g add -A; g commit -qm base
base="$(g rev-parse HEAD)"

echo b >> "$TMP/docs/a.md"; echo c > "$TMP/README.md"
g add -A; g commit -qm docs
test "$(detect "$base" HEAD)" = native=false || { echo 'FAIL: docs-only must be native=false'; exit 1; }

echo b >> "$TMP/Source/a.cpp"
g add -A; g commit -qm src
test "$(detect "$base" HEAD)" = native=true || { echo 'FAIL: source change must be native=true'; exit 1; }

test "$(detect 0000000000000000000000000000000000000000 HEAD)" = native=true
test "$(detect '' HEAD)" = native=true
test "$(detect "$base" "$base")" = native=false

g reset -q --hard "$base"
echo w > "$TMP/.github/workflows/x.yml"
g add -A; g commit -qm wf
test "$(detect "$base" HEAD)" = native=true || { echo 'FAIL: workflow change must be native=true'; exit 1; }

# release_stage_artifacts.sh: rebuild a build tree from a flat/odd artifact layout.
python3 -I - "$TMP" <<'PY'
import struct, sys
from pathlib import Path
t = Path(sys.argv[1])
pe = bytearray(0x100); pe[:2] = b"MZ"; struct.pack_into("<I", pe, 0x3C, 0x80)
pe[0x80:0x84] = b"PE\0\0"; struct.pack_into("<H", pe, 0x84, 0x8664)
elf = bytearray(64); elf[:4] = b"\x7fELF"; struct.pack_into("<H", elf, 18, 62)
w = t / "rawwin" / "x" / "GapFill.vst3" / "Contents" / "x86_64-win"; w.mkdir(parents=True)
(w / "GapFill.vst3").write_bytes(pe)
(t / "rawwin" / "x" / "GapFill.clap").write_bytes(pe)
(t / "rawwin" / "GapFill.exe").write_bytes(pe)
(t / "rawwin" / "icon.ico").write_bytes(b"ico")
l = t / "rawlin" / "GapFill.vst3" / "Contents" / "x86_64-linux"; l.mkdir(parents=True)
(l / "GapFill.so").write_bytes(elf)
(t / "rawlin" / "GapFill.clap").write_bytes(elf)
(t / "rawlin" / "GapFill").write_bytes(elf)
(t / "badlin").mkdir(); (t / "badlin" / "GapFill.clap").write_bytes(b"nope")
PY
bash "$ROOT/tools/release_stage_artifacts.sh" windows "$TMP/rawwin" "$TMP/stage-win" > /dev/null
test -s "$TMP/stage-win/artefacts/Release/GapFill.exe"
test -s "$TMP/stage-win/GapFill_artefacts/JuceLibraryCode/icon.ico"
bash "$ROOT/tools/release_stage_artifacts.sh" linux "$TMP/rawlin" "$TMP/stage-lin" > /dev/null
test -x "$TMP/stage-lin/artefacts/GapFill"
if bash "$ROOT/tools/release_stage_artifacts.sh" linux "$TMP/badlin" "$TMP/stage-bad" > /dev/null 2>&1; then
    echo 'FAIL: incomplete artifact must be rejected'; exit 1
fi

echo 'PASS: CI helper scripts'
