#!/usr/bin/env python3
"""T-02: layout spacing literals must be multiples of 4 (theme.md: multiples of 8, or 4 for fine detail).

Scans the top-bar / panel layout code for integer literals passed to the rectangle helpers (``reduced``, ``removeFrom*``,
``withTrimmed*``, ``expanded``, ``translated``) and reports those that are not multiples of 4.

The existing non-conforming values cannot be changed blind (T-02 forbids a layout pass without screenshots at minimum, default
and large size), so this script is a RATCHET: ``tools/layout_literals_baseline.json`` records the count per file and the
check fails when a file gets MORE non-conforming literals than its baseline, or when a baseline is stale (fewer literals
than recorded: lower the number). The visual pass itself stays open and is tracked in docs/dev/features/T-02.

Usage:
    python tools/check_layout_literals.py            # check against the baseline
    python tools/check_layout_literals.py --list     # print every offending literal
    python tools/check_layout_literals.py --update   # rewrite the baseline (only when it went DOWN)
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILES = ["Source/PluginEditor.cpp", "Source/gui/Panels.cpp"]
BASELINE = ROOT / "tools" / "layout_literals_baseline.json"

CALL_RE = re.compile(r"\b(reduced|removeFromLeft|removeFromRight|removeFromTop|removeFromBottom|"
                     r"withTrimmedLeft|withTrimmedRight|withTrimmedTop|withTrimmedBottom|expanded|translated)\s*\(([^()]*)\)")
INT_RE = re.compile(r"(?<![\w.])(\d+)(?![\w.])")


def offending_literals(text: str) -> list[tuple[int, str, int]]:
    """(line, call, value) for each integer literal that is not a multiple of 4. Zero is allowed. Comments are ignored."""
    stripped = re.sub(r"//[^\n]*", lambda m: " " * len(m.group(0)), text)
    stripped = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), stripped, flags=re.S)
    found = []

    for match in CALL_RE.finditer(stripped):
        args = match.group(2)

        if "(" in args:
            continue

        line = stripped.count("\n", 0, match.start()) + 1

        for lit in INT_RE.findall(args):
            value = int(lit)

            if value % 4 != 0:
                found.append((line, match.group(1), value))

    return found


def count_for(path: Path) -> int:
    return len(offending_literals(path.read_text(encoding="utf-8")))


def load_baseline() -> dict[str, int]:
    try:
        return json.loads(BASELINE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def check(counts: dict[str, int], baseline: dict[str, int]) -> list[str]:
    errors = []

    for name, count in counts.items():
        allowed = baseline.get(name)

        if allowed is None:
            errors.append(f"{name}: no baseline entry (run with --update after reviewing --list)")
        elif count > allowed:
            errors.append(f"{name}: {count} layout literals are not multiples of 4, the baseline allows {allowed}; "
                          f"use metrics::grid multiples (run with --list)")
        elif count < allowed:
            errors.append(f"{name}: only {count} left, the baseline says {allowed}; lower it with --update so it cannot grow back")

    return errors


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--update", action="store_true")
    args = ap.parse_args(argv)

    counts = {name: count_for(ROOT / name) for name in FILES}

    if args.list:
        for name in FILES:
            for line, call, value in offending_literals((ROOT / name).read_text(encoding="utf-8")):
                print(f"{name}:{line}: {call}(... {value} ...)")

    if args.update:
        BASELINE.write_text(json.dumps(counts, indent=2) + "\n", encoding="utf-8")
        print(f"baseline written: {counts}")
        return 0

    errors = check(counts, load_baseline())

    for e in errors:
        print("ERROR:", e)

    if not errors:
        print(f"layout literals OK: {counts}")

    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
