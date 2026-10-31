#!/usr/bin/env python3
"""Execute one real test selection and retain its exact result/provenance.

An exit of zero without an executed-test count is a failure. This is automated
test evidence, never host, listening, signing or release-acceptance evidence.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import subprocess
import time

REPO = Path(__file__).resolve().parents[2]
COUNTS = re.compile(r"(\d+) passed, (\d+) failed")


def git_value(*args: str) -> str:
    result = subprocess.run(["git", *args], cwd=REPO, capture_output=True, text=True, timeout=30)
    return result.stdout.strip() if result.returncode == 0 else "unavailable"


def provenance(binary: Path, build_dir: Path | None, config: str) -> dict:
    cache = {}
    if build_dir is not None and (build_dir / "CMakeCache.txt").is_file():
        for line in (build_dir / "CMakeCache.txt").read_text(errors="replace").splitlines():
            if "=" in line and line.startswith(("CMAKE_CXX_", "CMAKE_C_", "CMAKE_BUILD_TYPE:")):
                key, value = line.split("=", 1)
                cache[key] = value
    return {"source_sha": git_value("rev-parse", "HEAD"),
            "source_dirty": bool(git_value("status", "--porcelain", "--untracked-files=no")),
            "os": platform.system(), "architecture": platform.machine(),
            "python": platform.python_version(), "build_config": config, "cmake_cache": cache,
            "binary": str(binary),
            "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest() if binary.is_file() else None,
            "dependencies": {name: git_value("-C", name, "rev-parse", "HEAD")
                             for name in ("JUCE", "clap-juce-extensions")}}


def run(binary: Path, test_filter: str | None, *, out: Path, label: str, timeout: float,
        min_tests: int = 1, build_dir: Path | None = None, config: str = "Release") -> dict:
    out.mkdir(parents=True, exist_ok=True)
    log = (out / (label + ".log")).resolve()
    command = [str(binary)] + ([test_filter] if test_filter else [])
    row = {"schema": 1, "kind": "automated-test-selection", "label": label, "command": command,
           "filter": test_filter, "log": str(log), **provenance(binary, build_dir, config)}
    started = time.monotonic()
    with log.open("w", encoding="utf-8") as stream:
        try:
            result = subprocess.run(command, cwd=REPO, stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
            code = result.returncode
        except subprocess.TimeoutExpired:
            stream.write("\nTIMEOUT\n")
            code = 124
        except OSError as exc:
            stream.write(f"cannot execute test binary: {exc}\n")
            code = 127
    matches = COUNTS.findall(log.read_text(encoding="utf-8", errors="replace"))
    passed, failed = map(int, matches[-1]) if matches else (0, 0)
    row.update(seconds=time.monotonic() - started, exit_code=code, signal=-code if code < 0 else None,
               passed=passed, failed=failed, executed=passed + failed,
               status="pass" if code == 0 and failed == 0 and passed >= min_tests else "fail")
    (out / (label + ".json")).write_text(json.dumps(row, indent=2) + "\n", encoding="utf-8")
    return row


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--filter")
    parser.add_argument("--label", required=True)
    parser.add_argument("--out", type=Path, default=Path("nightly-evidence"))
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--config", default="Release")
    parser.add_argument("--timeout", type=float, default=7200)
    parser.add_argument("--min-tests", type=int, default=1)
    args = parser.parse_args(argv)
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", args.label):
        parser.error("label must be a filename without directory components")
    if args.timeout <= 0 or args.min_tests <= 0:
        parser.error("timeout and min-tests must be positive")
    row = run(args.binary.resolve(), args.filter, out=args.out, label=args.label, timeout=args.timeout,
              min_tests=args.min_tests, build_dir=args.build_dir, config=args.config)
    print(f"{args.label}: {row['status']}; {row['executed']} executed; exit {row['exit_code']}; {row['log']}")
    return 0 if row["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
