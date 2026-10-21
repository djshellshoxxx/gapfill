#!/usr/bin/env python3
"""Mechanical completeness audit for GapFill (docs/audits/2026-10-21-completeness-audit.md).

Checks what can be checked without a compiler. Every finding is either FAIL (a
gap that must be fixed or recorded in KNOWN_EXEMPTIONS with a reason) or INFO.
Exit status 0 when there are no un-exempted FAIL findings.

    python3 tools/audit/completeness_audit.py            # report
    python3 tools/audit/completeness_audit.py --matrix   # also print the per-parameter matrix
    python3 tools/audit/completeness_audit.py --selftest # unit tests of the parsers

Checks (see CHECKS below): parameter ids vs lock files, ParamInfo, GUI control,
Random/Surprise classification, cmake fragments (sources + tests), registrars,
command ids, state subtree rows, spec tickets vs IMPLEMENTATION-STATUS.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

PARAM_RE = re.compile(r'X\s*\(\s*([A-Za-z0-9_]+)\s*,\s*"([^"]+)"\s*\)')
GROUP_RE = re.compile(r"#define\s+GAPFILL_PARAMS_([A-Z0-9_]+)\(X\)(.*?)(?=\n\s*\n|\n#|\Z)", re.S)

# ---------------------------------------------------------------------------
# Documented exemptions: (check, key) -> reason. A FAIL whose (check, key) is
# listed here is reported as EXEMPT and does not fail the run. Keep this list
# short and honest; every entry is mirrored in the audit report.
# ---------------------------------------------------------------------------
KNOWN_EXEMPTIONS: dict[tuple[str, str], str] = {
    ("state_handlers", "Learn"): "reserved row of the original F-X05 plan; the shipped design stores ratings in <Taste> (WP7). Row is empty by design.",
}
for _id in ("fm10_stutter", "fm10_freeze", "fm10_reverse", "fm10_tape"):
    KNOWN_EXEMPTIONS[("param_gui", _id)] = ("host-automation / MIDI-learn mirror of a hold; the PERFORMANCE pads (HoldPadsSlot.cpp) drive the same "
                                            "holds through the momentary commands 20-23, so no second control is drawn")
for _name in ("SkipLatest", "SkipRecipe"):
    KNOWN_EXEMPTIONS[("command_map_range", _name)] = ("F-X05 ratings are given in the Taste strip; MIDI binding needs a controller-profile "
                                                      "key (the v1 profile schema lists commands 1-30 only)")
KNOWN_EXEMPTIONS[("feature_tests", "recipes")] = "RecipeServices/RecipeState are exercised by TestAudition, TestHistory, TestExport and TestAbCompare"


def read(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def strip_cpp_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


# ---------------------------------------------------------------------------
# Parsers (unit-tested by --selftest)
# ---------------------------------------------------------------------------
def parse_param_groups(root: Path) -> dict[str, list[str]]:
    """group name -> ordered ids, from Source/Parameters.h and Source/params/*.h."""
    groups: dict[str, list[str]] = {}
    files = [root / "Source" / "Parameters.h"] + sorted((root / "Source" / "params").glob("*.h"))
    for f in files:
        text = read(f)
        for m in GROUP_RE.finditer(text):
            ids = [i for _, i in PARAM_RE.findall(m.group(2))]
            if ids or m.group(1) not in groups:
                groups[m.group(1)] = ids
    return groups


def parse_lock(text: str) -> list[str]:
    return [l.strip() for l in text.splitlines() if l.strip() and not l.lstrip().startswith("#")]


def parse_info_ids(root: Path) -> set[str]:
    ids: set[str] = set()
    for f in (root / "Source").rglob("*.cpp"):
        t = read(f)
        if "ParamInfoRegistrar" not in t and "ParamInfo" not in t:
            continue
        ids.update(re.findall(r'\{\s*"([A-Za-z][A-Za-z0-9_]*)"\s*,\s*"[^"]*"\s*,\s*"', t))
    return ids


def parse_allowlist(root: Path) -> set[str]:
    return set(parse_lock(read(root / "Tests" / "param_info_allowlist.txt")))


def parse_command_enum(root: Path) -> dict[str, int]:
    t = strip_cpp_comments(read(root / "Source" / "dsp" / "EngineEvents.h"))
    m = re.search(r"enum class CommandType[^{]*\{(.*?)\};", t, re.S)
    out: dict[str, int] = {}
    nxt = 0
    if m:
        for item in m.group(1).split(","):
            item = item.strip()
            if not item:
                continue
            if "=" in item:
                name, val = [s.strip() for s in item.split("=", 1)]
                nxt = int(val)
            else:
                name = item
            out[name] = nxt
            nxt += 1
    return out


GUI_NAME_RE = re.compile(r"(Gui|Strip|Slot|Panel|Card|View|Section|Editor|Drawer|Window|Tab|Menu)", re.I)


def gui_files(root: Path) -> list[Path]:
    out = []
    for f in (root / "Source").rglob("*.cpp"):
        rel = f.relative_to(root).as_posix()
        if "/params/" in rel or rel.endswith(("Info.cpp", "Help.cpp")):
            continue
        if rel.startswith("Source/gui/") or GUI_NAME_RE.search(f.name) or "AuditionGui" in f.name:
            out.append(f)
    return out


def parse_status_rows(text: str) -> dict[str, str]:
    rows = {}
    for line in text.splitlines():
        m = re.match(r"\|\s*([A-Za-z0-9-]+)\s*\|\s*(yes|partial|no|out of scope)[^|]*\|", line)
        if m:
            rows[m.group(1)] = m.group(2)
    return rows


# ---------------------------------------------------------------------------
class Report:
    def __init__(self) -> None:
        self.findings: list[tuple[str, str, str, str]] = []   # (level, check, key, text)

    def fail(self, check: str, key: str, text: str) -> None:
        reason = KNOWN_EXEMPTIONS.get((check, key))
        if reason:
            self.findings.append(("EXEMPT", check, key, f"{text} -- {reason}"))
        else:
            self.findings.append(("FAIL", check, key, text))

    def info(self, check: str, key: str, text: str) -> None:
        self.findings.append(("INFO", check, key, text))

    def failures(self) -> list[tuple[str, str, str, str]]:
        return [f for f in self.findings if f[0] == "FAIL"]


def check_params(root: Path, rep: Report, matrix: bool) -> None:
    groups = parse_param_groups(root)
    order = [g.strip() for g in read(root / "tools" / "param_ids" / "GROUP_ORDER").splitlines() if g.strip() and not g.startswith("#")]
    info_ids = parse_info_ids(root)
    allow = parse_allowlist(root)
    guis = {f: read(f) for f in gui_files(root)}
    gui_blob = "\n".join(guis.values())
    perf_src = {}
    for f in (root / "Source" / "params").glob("*.cpp"):
        perf_src[f.stem] = read(f)
    parameters_cpp = read(root / "Source" / "Parameters.cpp")
    # Random / Random All draw only from these two functions; a feature parameter must never appear in them.
    random_ids: set[str] = set()
    for fn in ("creativeParameterIDs", "randomisableParameterIDs"):
        m = re.search(r"juce::StringArray " + fn + r"\(\)\s*\{(.*?)\n\}", parameters_cpp, re.S)
        if m:
            random_ids.update(re.findall(r'"([A-Za-z][A-Za-z0-9_]*)"', m.group(1)))
    presets_cpp = read(root / "Source" / "Presets.cpp")

    total = 0
    rows = []
    for g in order:
        ids = groups.get(g, [])
        lock_file = root / "tools" / "param_ids" / f"{g.lower()}.lock"
        if g != "CORE" and ids:
            if not lock_file.exists():
                rep.fail("lock_file", g, "no tools/param_ids lock file")
            elif parse_lock(read(lock_file)) != ids:
                rep.fail("lock_file", g, "lock file differs from the X-macro list")
        for pid in ids:
            total += 1
            has_info = pid in info_ids
            if not has_info and pid not in allow:
                rep.fail("param_info", pid, "no ParamInfo entry and not allow-listed")
            if pid in allow and has_info:
                rep.fail("param_info", pid, "allow-listed but already has an entry (stale line)")
            has_gui = f'"{pid}"' in gui_blob or (re.fullmatch(r"w[A-Z][A-Za-z]+", pid) is not None and "algoWeightID" in gui_blob)
            if not has_gui:
                rep.fail("param_gui", pid, f"group {g}: id literal not found in any GUI source (control missing or reached by a table)")
            if g != "CORE" and pid in random_ids:
                rep.fail("param_classification", pid, f"group {g}: a feature parameter is in the Random / Random All lists")
            rows.append((g, pid, has_info, has_gui))
    rep.info("params", "total", f"{total} parameter ids in {len(order)} groups")
    if matrix:
        print("group,id,info,gui")
        for g, pid, i, u in rows:
            print(f"{g},{pid},{int(i)},{int(u)}")


def check_cmake(root: Path, rep: Report) -> None:
    listed = set()
    for f in [root / "CMakeLists.txt"] + list((root / "cmake").rglob("*.cmake")):
        listed.update(re.findall(r"((?:Source|Tests)/[A-Za-z0-9_./+-]+\.(?:cpp|mm|h))", read(f)))
    for f in (root / "Source").rglob("*.cpp"):
        rel = f.relative_to(root).as_posix()
        if rel not in listed:
            rep.fail("cmake_source", rel, "source file is not in any cmake list (dead code)")
    for f in (root / "Tests").rglob("Test*.cpp"):
        rel = f.relative_to(root).as_posix()
        if rel not in listed and "golden" not in rel:
            rep.fail("cmake_test", rel, "test file is not compiled by any cmake fragment")
    for rel in sorted(listed):
        if rel.endswith(".cpp") and not (root / rel).exists():
            rep.fail("cmake_missing", rel, "cmake lists a file that does not exist")


def check_feature_tests(root: Path, rep: Report) -> None:
    """Every cmake/sources fragment has a cmake/tests fragment or a test file that mentions the feature."""
    tests_text = "\n".join(read(f) for f in (root / "cmake" / "tests").glob("*.cmake"))
    for f in sorted((root / "cmake" / "sources").glob("*.cmake")):
        if f.stem in ("core", "params", "foundations", "ui"):
            continue
        if not (root / "cmake" / "tests" / f.name).exists() and f.stem.lower() not in tests_text.lower():
            rep.fail("feature_tests", f.stem, "source fragment without a same-named test fragment")


def check_registrars(root: Path, rep: Report) -> None:
    src = (root / "Source")
    listed = set()
    for f in [root / "CMakeLists.txt"] + list((root / "cmake").rglob("*.cmake")):
        listed.update(re.findall(r"(Source/[A-Za-z0-9_./+-]+\.cpp)", read(f)))
    for f in src.rglob("*.cpp"):
        rel = f.relative_to(root).as_posix()
        t = read(f)
        if re.search(r"\b(SlotRegistrar|HelpRegistrar|ParamInfoRegistrar)\b\s+\w+\s*[({]?", t) and rel not in listed:
            rep.fail("registrar_reachable", rel, "defines a static registrar but is not compiled")


def check_commands(root: Path, rep: Report) -> None:
    enum = parse_command_enum(root)
    corpus = ""
    for f in (root / "Source").rglob("*"):
        if f.suffix in (".cpp", ".h") and f.name != "EngineEvents.h":
            corpus += read(f)
    for name in enum:
        if name == "None":
            continue
        if not re.search(r"CommandType::" + name + r"\b", corpus):
            rep.fail("command_handler", name, "command id has no handler or producer")
    cm = read(root / "Source" / "features" / "commands" / "CommandMap.cpp")
    m = re.search(r"v\s*>=\s*1\s*&&\s*v\s*<=\s*(\d+)", cm)
    top = int(m.group(1)) if m else 0
    for name, value in enum.items():
        if name != "None" and value > top:
            rep.fail("command_map_range", name, f"command id {value} is above the X-1 bindable range 1..{top}")


def check_state(root: Path, rep: Report) -> None:
    t = read(root / "Source" / "features" / "state" / "StateSubtrees.cpp")
    for m in re.finditer(r'\{\s*"([A-Za-z]+)"\s*,\s*([^,]+),\s*([^}]*)\}', t):
        name, _, handlers = m.groups()
        if "nullptr" in handlers.split("//")[0]:
            rep.fail("state_handlers", name, "subtree row with null handlers")


def check_tickets(root: Path, rep: Report) -> None:
    status = read(root / "docs" / "dev" / "features" / "IMPLEMENTATION-STATUS.md")
    rows = parse_status_rows(status)
    for f in sorted((root / "docs" / "dev" / "features").glob("*.md")):
        if f.stem in ("DECISIONS", "00-SPEC-GAP-REPORT", "IMPLEMENTATION-STATUS"):
            continue
        if f.stem not in rows:
            rep.fail("ticket_status", f.stem, "feature spec has no IMPLEMENTATION-STATUS row")
    for tid in ("E-3", "N-USE-a", "N-USE-b", "N-EASY", "N-FX1", "N-FX2", "N-MUS", "N-VAL", "X-1", "FX-0", "SP-0", "UI-0", "F-D01", "F-D02",
                "F-D03", "F-D04", "F-P02", "F-S08", "F-S07"):
        if tid not in status:
            rep.fail("ticket_status", tid, "doc-12 ticket without an IMPLEMENTATION-STATUS line")
    counts = defaultdict(int)
    for v in rows.values():
        counts[v] += 1
    rep.info("tickets", "counts", str(dict(counts)))


def check_help(root: Path, rep: Report) -> None:
    """Feature directories that own parameters must register a Help section."""
    help_text = "\n".join(read(f) for f in (root / "Source").rglob("*.cpp") if "HelpRegistrar" in read(f))
    for g, key in [("swell", "Swell"), ("cloud", "Cloud"), ("lock", "Fill Lock"), ("scrub", "Scrub"), ("ratchet", "atchet"),
                   ("rcrush", "rush"), ("tapedestr", "ape"), ("bandfx", "per lane")]:
        if key.lower() not in help_text.lower():
            rep.fail("help_section", g, f"no Help text mentions '{key}'")


def check_slots_and_help(root: Path, rep: Report) -> None:
    editor = read(root / "Source" / "PluginEditor.cpp") + read(root / "Source" / "gui" / "SlotHost.cpp")
    used: dict[str, str] = {}
    orders: dict[tuple[str, int], str] = {}
    helps: dict[int, str] = {}
    for f in (root / "Source").rglob("*.cpp"):
        t = read(f)
        for m in re.finditer(r"SlotRegistrar\s+\w+\s*[({]\s*\{\s*Slot::(\w+)\s*,\s*(\d+)", t):
            used.setdefault(m.group(1), f.name)
            key = (m.group(1), int(m.group(2)))
            if key in orders and orders[key] != f.name:
                rep.fail("slot_order", f"{m.group(1)}:{m.group(2)}", f"two components share an order ({orders[key]}, {f.name})")
            orders[key] = f.name
        for m in re.finditer(r'HelpRegistrar\s+\w+\s*[({]\s*(\d+)\s*,\s*"([^"]+)"', t):
            order = int(m.group(1))
            if order in helps and helps[order] != m.group(2):
                rep.fail("help_order", str(order), f"'{helps[order]}' and '{m.group(2)}' share a Help order")
            helps[order] = m.group(2)
    for slot, fname in sorted(used.items()):
        if f"Slot::{slot}" not in editor:
            rep.fail("slot_reachable", slot, f"components registered (e.g. {fname}) but the editor never gives the slot a rectangle")
    rep.info("slots", "count", f"{len(used)} slots in use, {len(helps)} Help sections")


CHECKS = [check_slots_and_help, check_params, check_cmake, check_feature_tests, check_registrars, check_commands, check_state, check_tickets, check_help]


def selftest() -> int:
    import unittest

    class T(unittest.TestCase):
        def test_param_re(self):
            self.assertEqual(PARAM_RE.findall('X (fl01_lock, "fl01_lock") \\\n X (a, "b")'), [("fl01_lock", "fl01_lock"), ("a", "b")])

        def test_lock(self):
            self.assertEqual(parse_lock("# c\na\n\nb\n"), ["a", "b"])

        def test_status(self):
            self.assertEqual(parse_status_rows("| F-L01-x | yes | a |\n| F-L02 | partial (x) | b |"), {"F-L01-x": "yes", "F-L02": "partial"})

        def test_exemption(self):
            r = Report()
            r.fail("state_handlers", "Learn", "x")
            r.fail("state_handlers", "Other", "x")
            self.assertEqual([f[0] for f in r.findings], ["EXEMPT", "FAIL"])

    return 0 if unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(T)).wasSuccessful() else 1


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--matrix", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--root", default=str(ROOT))
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    root = Path(args.root)
    rep = Report()
    for chk in CHECKS:
        if chk is check_params:
            chk(root, rep, args.matrix)
        else:
            chk(root, rep)
    for level, check, key, text in rep.findings:
        print(f"{level:6} {check:22} {key}: {text}")
    fails = rep.failures()
    print(f"\n{len(fails)} failing finding(s), {sum(1 for f in rep.findings if f[0] == 'EXEMPT')} exempt")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
