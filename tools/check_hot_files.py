#!/usr/bin/env python3
"""Fails a feature/ticket PR that touches a hot file without permission.

Hot files are the merge hotspots listed in docs/dev/01-DEVELOPMENT-PLAN.md
section 3. A PR whose title starts with a ticket prefix (``E-1:``, ``C-5:``,
``F-L07:`` ...) or that carries the ``feature`` label may not change them unless
it carries the ``hot-file-ok`` label (justify in the PR body). Golden baselines
may only change in a PR titled with ``[golden]``; the perf baseline only with
``[perf]``. Phase 0 (``F0-n``) and ``docs``/``ci``/``release`` PRs are exempt
from the hot-file rule but not from the golden/perf title rule.

Usage:
    python tools/check_hot_files.py --title "E-1: Fill Audition" --labels feature \\
        [--base origin/master | --files-from changed.txt]
No dependencies beyond the standard library.
"""

from __future__ import annotations

import argparse
import fnmatch
import re
import subprocess
import sys
from pathlib import Path

HOT_FILES = [
    "Source/Parameters.h",
    "Source/Parameters.cpp",
    "CMakeLists.txt",
    "Source/Presets.cpp",
    "Source/PluginProcessor.cpp",
    "Source/PluginProcessor.h",
    "Source/dsp/FillEngine.cpp",
    "Source/dsp/FillVoice.cpp",
    "Source/PluginEditor.cpp",
    "Source/gui/Panels.cpp",
    "Source/gui/HelpView.cpp",
    "Tests/TestPlugin.cpp",
    "PROGRESS.md",
]
GOLDEN_GLOBS = ["Tests/golden/*", "Tests/fixtures/*"]
PERF_FILES = ["Tests/perf_baseline.json"]

TICKET_RE = re.compile(r"^\s*(?:\[[^\]]*\]\s*)*([A-Z][A-Z0-9]{0,3}-[A-Z]{0,3}\d+[a-z]?)\s*:")
EXEMPT_PREFIXES = ("F0-",)
EXEMPT_WORDS = ("docs:", "ci:", "release:", "chore:")


def is_ticket_title(title: str) -> bool:
    match = TICKET_RE.match(title)
    return bool(match) and not match.group(1).startswith(EXEMPT_PREFIXES)


def is_exempt(title: str) -> bool:
    lowered = title.strip().lower()
    return lowered.startswith(EXEMPT_WORDS) or title.strip().startswith(EXEMPT_PREFIXES)


def matches_golden(path: str) -> bool:
    return any(fnmatch.fnmatch(path, g) for g in GOLDEN_GLOBS)


def check(title: str, labels: set[str], files: list[str]) -> list[str]:
    errors: list[str] = []
    files = [f.replace("\\", "/") for f in files if f.strip()]

    golden = [f for f in files if matches_golden(f)]
    if golden and "[golden]" not in title.lower():
        errors.append("golden/fixture files changed but the PR title lacks [golden]: " + ", ".join(sorted(golden)))

    perf = [f for f in files if f in PERF_FILES]
    if perf and "[perf]" not in title.lower():
        errors.append("perf baseline changed but the PR title lacks [perf]: " + ", ".join(perf))

    feature_pr = ("feature" in labels or is_ticket_title(title)) and not is_exempt(title)
    if feature_pr and "hot-file-ok" not in labels:
        hot = [f for f in files if f in HOT_FILES]
        if hot:
            errors.append("feature PR touches hot files (add them to a feature fragment instead, "
                          "or label hot-file-ok with a justification): " + ", ".join(sorted(hot)))

    return errors


def changed_files(base: str) -> list[str]:
    out = subprocess.run(["git", "diff", "--name-only", f"{base}...HEAD"], check=True,
                         capture_output=True, text=True).stdout
    return out.splitlines()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--title", required=True)
    parser.add_argument("--labels", default="")
    parser.add_argument("--base", default="origin/master")
    parser.add_argument("--files-from", help="text file with one changed path per line (instead of git diff)")
    args = parser.parse_args(argv)

    labels = {label.strip() for label in args.labels.split(",") if label.strip()}
    files = Path(args.files_from).read_text(encoding="utf-8").splitlines() if args.files_from else changed_files(args.base)

    errors = check(args.title, labels, files)
    if errors:
        print("Hot-file check: FAILED")
        for error in errors:
            print(f" - {error}")
        return 1

    print("Hot-file check: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
