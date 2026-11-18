#!/usr/bin/env python3
"""Q-01: prints the table of one or more QA evidence files and exits non-zero when any step failed.

Usage: python tools/qa/summarise.py qa-evidence/<sha>-<os>.json [...]
CI uploads the evidence directory as an artifact even when this exits non-zero.
"""

from __future__ import annotations

import json
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from run_matrix import summarise, parse_test_counts  # noqa: E402
from evidence import strict_summary, validate_structure  # noqa: E402


def main(argv: list[str]) -> int:
    if not argv:
        print(__doc__)
        return 2

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strict", action="store_true", help="require complete applicable automated coverage and build provenance; never establishes manual release acceptance")
    parser.add_argument("--summary-out", type=Path, help="write the strict coverage links and acceptance status as JSON")
    parser.add_argument("files", nargs="+")
    args = parser.parse_args(argv)
    worst, reports = 0, []

    for name in args.files:
        try:
            evidence = json.loads(Path(name).read_text(encoding="utf-8"))
            validate_structure(evidence)
        except (OSError, ValueError) as exc:
            print(f"{name}: unreadable evidence file ({exc})")
            worst = 1
            continue

        text, code = summarise(evidence)
        print(text)
        print()
        worst = max(worst, code)
        reports.append((Path(name), evidence))

    if args.strict:
        result = strict_summary(reports, parse_test_counts)
        if worst or not reports:
            result["status"] = "blocked"
            result["issues"].append("at least one requested evidence file was unreadable or failed")
        print(f"Strict automated acceptance: {result['status']}")
        for issue in result["issues"]:
            print(f"  {issue}")
        print(result["release_acceptance"])
        if args.summary_out:
            args.summary_out.parent.mkdir(parents=True, exist_ok=True)
            args.summary_out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        worst = max(worst, int(result["status"] != "complete"))

    return worst


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
