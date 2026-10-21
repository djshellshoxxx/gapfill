#!/usr/bin/env python3
"""Q-01 release QA matrix harness (docs/dev/features/Q-01-release-qa-matrix-harness.md).

Runs the scripted part of the release matrix against a built ``GapFillTests`` binary and writes a machine-readable
evidence file per platform: ``<out>/<git sha>-<os>.json`` plus one raw log per step.

* A step that executes zero tests FAILS (the registry exits non-zero for a filter that matches nothing, and the
  harness additionally insists on ``min_tests``).
* A validator that is not installed is ``unverified``, never ``pass``. Real DAW and hardware rows are not in this file
  at all: the summary lists them as open (they are filled by the manual scripts in docs/qa/).
* Nothing is fabricated: the evidence records the git SHA, OS, interpreter, command line and the raw log path.

Usage:
    python tools/qa/run_matrix.py --binary build-ci/artefacts/GapFillTests [--tier fast|full] [--out qa-evidence]
        [--pluginval PATH --vst3 PATH] [--clap-validator PATH --clap PATH]
Exit status: 0 when no step failed (unverified rows do not fail the run but are listed), 1 otherwise.
Standard library only; safe under ``python -I``.
"""

from __future__ import annotations

import argparse
import json
import platform
import re
import shlex
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
COUNT_RE = re.compile(r"(\d+) passed, (\d+) failed")

# Manual rows of docs/dev/07-QA-AND-RELEASE.md that no script can fill. Listed, never marked as passed.
MANUAL_ROWS = [
    "real DAW sessions (REAPER, Bitwig, Cubase, FL Studio, Live, Logic)",
    "MIDI hardware / LED feedback",
    "drag-export into a DAW",
    "audibility of every effect and preset",
    "screen-reader behaviour of the editor",
    "screenshots at minimum / default / large size",
]


@dataclass
class StepSpec:
    name: str
    cmd: list[str] | None                    # None = cannot run here (reason in `unverified_reason`)
    min_tests: int = 0                       # a test-binary step must execute at least this many tests
    expect_nonzero: bool = False             # the step passes only when the command FAILS (bad-filter guard)
    timeout: int = 1800
    tiers: tuple[str, ...] = ("fast", "full")
    unverified_reason: str = ""
    extra: dict = field(default_factory=dict)


def parse_counts(text: str) -> tuple[int, int] | None:
    """(passed, failed) of the LAST ``N passed, M failed`` line of the test binary, or None."""
    found = COUNT_RE.findall(text)
    if not found:
        return None
    passed, failed = found[-1]
    return int(passed), int(failed)


def run_step(spec: StepSpec, *, cwd: Path, log_dir: Path, runner=subprocess.run) -> dict:
    """Runs one step and returns its evidence row. Never raises for a failing command."""
    row = {"name": spec.name, "status": "fail", "executed": 0, "passed": 0, "failed": 0,
           "seconds": 0.0, "log": "", "command": " ".join(shlex.quote(c) for c in (spec.cmd or [])), "detail": ""}

    if spec.cmd is None:
        row["status"] = "unverified"
        row["detail"] = spec.unverified_reason or "not available on this machine"
        return row

    log_dir.mkdir(parents=True, exist_ok=True)
    log_path = log_dir / f"{spec.name}.log"
    row["log"] = str(log_path)
    started = time.monotonic()

    try:
        proc = runner(spec.cmd, cwd=str(cwd), capture_output=True, text=True, timeout=spec.timeout, errors="replace")
        output = (proc.stdout or "") + (proc.stderr or "")
        code = proc.returncode
    except subprocess.TimeoutExpired as exc:
        output = ((exc.stdout or b"").decode("utf-8", "replace") if isinstance(exc.stdout, bytes) else (exc.stdout or "")) + "\nTIMEOUT"
        code = 124
    except OSError as exc:                                   # missing binary, not executable
        output = f"cannot run: {exc}"
        code = 127

    row["seconds"] = round(time.monotonic() - started, 2)
    log_path.write_text(output, encoding="utf-8")
    counts = parse_counts(output)

    if counts is not None:
        row["passed"], row["failed"] = counts
        row["executed"] = counts[0] + counts[1]

    if spec.expect_nonzero:
        # Guard step: the command must fail, and for a real reason (the test binary reports it), not a crash on start.
        ok = code != 0 and code not in (124, 127)
        row["status"] = "pass" if ok else "fail"
        row["detail"] = "failed as required" if ok else f"expected a non-zero exit, got {code}"
        return row

    if code < 0 or code >= 128:                              # killed by a signal / crash
        row["detail"] = f"crashed or was killed (exit {code}); collect a dump from the OS crash handler and attach it"
        row["crash"] = True
    elif code != 0:
        row["detail"] = f"exit {code}"
    elif spec.min_tests and row["executed"] < spec.min_tests:
        row["detail"] = f"executed {row['executed']} tests, at least {spec.min_tests} required (a step that runs nothing is a failure)"
    elif counts is not None and row["failed"] > 0:
        row["detail"] = f"{row['failed']} failed"
    else:
        row["status"] = "pass"

    return row


def default_steps(binary: list[str], tier: str, *, pluginval=None, vst3=None, clap_validator=None, clap=None) -> list[StepSpec]:
    py = sys.executable
    steps: list[StepSpec] = [
        StepSpec("preflight-validate-repo", [py, "-I", "tools/validate_repo.py"]),
        StepSpec("preflight-python-tests", [py, "-I", "-m", "unittest", "discover", "-s", "tools", "-p", "test_*.py"]),
        StepSpec("unit", binary + ["-Perf"], min_tests=100, tiers=("fast",)),
        StepSpec("unit-all", binary, min_tests=100, tiers=("full",)),
        StepSpec("bad-filter-must-fail", binary + ["nonexistent_filter_should_fail"], expect_nonzero=True),
        StepSpec("blocksweep", binary + ["Transport"], min_tests=5),
        StepSpec("blocksweep-every-size", binary + ["handles_every_common_block_size"], min_tests=1),
        StepSpec("ratesweep", binary + ["initialises_and_processes_at_every_supported_sample_rate"], min_tests=1),
        StepSpec("determinism", binary + ["repeatable"], min_tests=1),
        StepSpec("robust", binary + ["missing_tempo_information_is_handled"], min_tests=1),
        StepSpec("fixtures", binary + ["StateCompat"], min_tests=1),
        StepSpec("gui-lifecycle", binary + ["survives_reopen"], min_tests=1),
        StepSpec("perf", binary + ["Perf"], min_tests=1, tiers=("full",)),
    ]

    def validator(name: str, tool, target, build_cmd) -> StepSpec:
        if tool and target:
            return StepSpec(name, build_cmd(tool, target), tiers=("full",))
        return StepSpec(name, None, tiers=("full",),
                        unverified_reason=f"{name} not run: pass its tool and plug-in paths (never reported as pass when skipped)")

    steps.append(validator("pluginval", pluginval, vst3,
                           lambda t, v: [t, "--strictness-level", "10", "--validate-in-process", "--validate", v]))
    steps.append(validator("clap-validator", clap_validator, clap, lambda t, c: [t, "validate", c]))

    if platform.system() == "Darwin" and shutil.which("auval"):
        steps.append(StepSpec("auval", ["auval", "-v", "aufx", "GpFl", "CDLb"], tiers=("full",)))
    else:
        steps.append(StepSpec("auval", None, tiers=("full",), unverified_reason="auval runs on macOS only"))

    return [s for s in steps if tier in s.tiers]


def git_sha(repo: Path = REPO) -> str:
    try:
        out = subprocess.run(["git", "rev-parse", "--short=12", "HEAD"], cwd=str(repo), capture_output=True, text=True, timeout=30)
        return out.stdout.strip() or "unknown"
    except (OSError, subprocess.SubprocessError):
        return "unknown"


def make_evidence(rows: list[dict], *, sha: str, os_name: str, command_line: str, tier: str) -> dict:
    return {
        "schema": 1,
        "sha": sha,
        "os": os_name,
        "python": platform.python_version(),
        "tier": tier,
        "command_line": command_line,
        "steps": rows,
        "manual_rows_open": MANUAL_ROWS,
    }


def summarise(evidence: dict) -> tuple[str, int]:
    """A readable table and the exit status: 1 on any failed step, 0 otherwise."""
    lines = [f"QA matrix {evidence['sha']} on {evidence['os']} ({evidence['tier']} tier)", ""]
    width = max([len(r["name"]) for r in evidence["steps"]] + [4])

    for r in evidence["steps"]:
        tests = f"{r['passed']}/{r['executed']}" if r["executed"] else "-"
        lines.append(f"  {r['name']:<{width}}  {r['status']:<10} tests {tests:<9} {r['seconds']:>7}s  {r['detail']}")

    failed = [r["name"] for r in evidence["steps"] if r["status"] == "fail"]
    unverified = [r["name"] for r in evidence["steps"] if r["status"] == "unverified"]
    lines.append("")
    lines.append(f"failed: {', '.join(failed) if failed else 'none'}")
    lines.append(f"unverified (not a pass): {', '.join(unverified) if unverified else 'none'}")
    lines.append("manual rows still open: " + "; ".join(evidence.get("manual_rows_open", [])))
    return "\n".join(lines), (1 if failed else 0)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--binary", required=True, help="path to GapFillTests (arguments allowed, shell-quoted)")
    ap.add_argument("--tier", choices=("fast", "full"), default="fast")
    ap.add_argument("--out", default="qa-evidence")
    ap.add_argument("--pluginval")
    ap.add_argument("--vst3")
    ap.add_argument("--clap-validator")
    ap.add_argument("--clap")
    args = ap.parse_args(argv)

    out = Path(args.out)
    sha = git_sha()
    os_name = platform.system().lower()
    log_dir = out / f"{sha}-{os_name}-logs"
    steps = default_steps(shlex.split(args.binary), args.tier, pluginval=args.pluginval, vst3=args.vst3,
                          clap_validator=args.clap_validator, clap=args.clap)
    rows = [run_step(s, cwd=REPO, log_dir=log_dir) for s in steps]

    evidence = make_evidence(rows, sha=sha, os_name=os_name, command_line=" ".join(shlex.quote(a) for a in sys.argv), tier=args.tier)
    out.mkdir(parents=True, exist_ok=True)
    (out / f"{sha}-{os_name}.json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
    text, code = summarise(evidence)
    print(text)
    return code


if __name__ == "__main__":
    sys.exit(main())
