#!/usr/bin/env python3
"""Require complete automated assurance from the selected release source run.

Input is the paginated GitHub jobs response from ``gh api --paginate``.
The caller still validates the CI workflow, successful run, exact commit and
unexpired platform artifacts. This gate distinguishes a full-assurance run from
an ordinary successful CI run; it does not establish manual release acceptance.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys


ASSURANCE_PREFIX = "Candidate native and sanitizer assurance / "
# These labels are the public job names in nightly.yml when called from ci.yml.
# A matrix/label change must update this contract together with its tests.
REQUIRED_ASSURANCE = (
    "Linux ASan-UBSan-full",
    "Linux TSan-threading",
    "Linux Release performance and continuous processor soak",
    "Full QA ubuntu-22.04 MIDI_OUT=OFF",
    "Full QA windows-2022 MIDI_OUT=OFF",
    "Full QA macos-15 MIDI_OUT=OFF",
    "Full QA macos-15-intel MIDI_OUT=OFF",
    "Full QA ubuntu-22.04 MIDI_OUT=ON",
)


def parse_job_pages(text: str) -> list:
    """Read consecutive JSON pages without requiring a newer gh --slurp option."""
    pages, offset = [], 0
    decoder = json.JSONDecoder()
    while offset < len(text):
        while offset < len(text) and text[offset].isspace():
            offset += 1
        if offset == len(text):
            break
        page, offset = decoder.raw_decode(text, offset)
        pages.append(page)
    return pages


def validate_assurance(pages: object, run_id: int, sha: str) -> None:
    """Raise ValueError when the selected run has incomplete or stale assurance."""
    if run_id <= 0 or not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise ValueError("a positive CI run ID and exact lowercase commit SHA are required")
    if not isinstance(pages, list) or not pages:
        raise ValueError("the complete paginated jobs response is required")

    jobs, totals = [], set()
    for page in pages:
        if (not isinstance(page, dict) or type(page.get("total_count")) is not int
                or page["total_count"] < 0 or not isinstance(page.get("jobs"), list)):
            raise ValueError("invalid jobs response page")
        totals.add(page["total_count"])
        jobs.extend(page["jobs"])
    if totals != {len(jobs)}:
        raise ValueError("jobs response is incomplete or changed during pagination; fetch it again")

    assurance = {}
    seen_ids = set()
    for job in jobs:
        if (not isinstance(job, dict) or type(job.get("id")) is not int
                or not isinstance(job.get("name"), str) or job["id"] in seen_ids):
            raise ValueError("invalid or duplicate job in jobs response")
        seen_ids.add(job["id"])
        name = job["name"]
        if not name.startswith(ASSURANCE_PREFIX):
            continue
        if name in assurance:
            raise ValueError(f"duplicate assurance job: {name}; fetch only the latest jobs")
        if job.get("run_id") != run_id or job.get("head_sha") != sha:
            raise ValueError(f"assurance belongs to a different run or commit: {name}")
        if job.get("status") != "completed" or job.get("conclusion") != "success":
            raise ValueError(f"assurance did not pass: {name} "
                             f"({job.get('status')}/{job.get('conclusion')})")
        assurance[name] = job

    missing = [name for name in REQUIRED_ASSURANCE if ASSURANCE_PREFIX + name not in assurance]
    if missing:
        raise ValueError("missing full assurance: " + ", ".join(missing)
                         + ". Run CI with full_assurance=true on this commit, "
                         "or select a completed full-assurance source_run_id")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=Path, required=True)
    parser.add_argument("--run-id", type=int, required=True)
    parser.add_argument("--sha", required=True)
    args = parser.parse_args(argv)
    try:
        validate_assurance(parse_job_pages(args.jobs.read_text(encoding="utf-8")), args.run_id, args.sha)
    except (OSError, ValueError) as error:
        print(f"Release source blocked: {error}", file=sys.stderr)
        return 1
    print(f"CI run {args.run_id}: full native, sanitizer and soak assurance passed for {args.sha}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
