#!/usr/bin/env python3
"""Q-01: prints the table of one or more QA evidence files and exits non-zero when any step failed.

Usage: python tools/qa/summarise.py qa-evidence/<sha>-<os>.json [...]
CI uploads the evidence directory as an artifact even when this exits non-zero.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from run_matrix import summarise  # noqa: E402


def main(argv: list[str]) -> int:
    if not argv:
        print(__doc__)
        return 2

    worst = 0

    for name in argv:
        try:
            evidence = json.loads(Path(name).read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            print(f"{name}: unreadable evidence file ({exc})")
            worst = 1
            continue

        text, code = summarise(evidence)
        print(text)
        print()
        worst = max(worst, code)

    return worst


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
