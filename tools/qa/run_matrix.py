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
import hashlib
import json
import platform
import plistlib
import re
import shlex
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from provenance import collect_provenance, tool_version

REPO = Path(__file__).resolve().parents[2]
COUNT_RE = re.compile(r"(\d+) passed, (\d+) failed")
PYTHON_COUNT_RE = re.compile(r"Ran (\d+) tests? in [^\n]+")

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


def parse_test_counts(text: str) -> dict | None:
    """Count test cases, not assertions/cleanup errors; skipped Python cases establish no executed coverage."""
    found = list(COUNT_RE.finditer(text))
    python_summaries = list(PYTHON_COUNT_RE.finditer(text))
    if not found or (python_summaries and python_summaries[-1].start() > found[-1].start()):
        python_counts = [match[1] for match in python_summaries]
        if not python_counts:
            return None
        total = int(python_counts[-1])
        failures = re.search(r"FAILED \(([^\n]+)\)", text)
        events = sum(int(n) for n in re.findall(r"(?:failures|errors)=(\d+)", failures[1])) if failures else 0
        skipped = re.findall(r"\bskipped=(\d+)", text)
        skipped = min(total, int(skipped[-1])) if skipped else 0
        # unittest can report FAIL and ERROR for one case (assertion then tearDown). Headers identify those cases.
        headers = re.findall(r"^(?:FAIL|ERROR): (.+? \([^)]+\))", text, re.M)
        failed = min(total - skipped, len(set(headers)) if headers else events)
        return {"passed": max(0, total - skipped - failed), "failed": failed, "reported_tests": total,
                "skipped_tests": skipped, "failure_events": events}
    passed, failed = found[-1].groups()
    return {"passed": int(passed), "failed": int(failed), "reported_tests": int(passed) + int(failed),
            "skipped_tests": 0, "failure_events": int(failed)}


def parse_counts(text: str) -> tuple[int, int] | None:
    counts = parse_test_counts(text)
    return (counts["passed"], counts["failed"]) if counts else None


def run_step(spec: StepSpec, *, cwd: Path, log_dir: Path, runner=subprocess.run) -> dict:
    """Runs one step and returns its evidence row. Never raises for a failing command."""
    row = {"name": spec.name, "status": "fail", "executed": 0, "passed": 0, "failed": 0,
           "seconds": 0.0, "log": "", "command": " ".join(shlex.quote(c) for c in (spec.cmd or [])), "detail": "",
           "command_argv": spec.cmd or [], "exit_code": None, "signal": None, "log_sha256": "", **spec.extra}

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
    row["exit_code"] = code
    row["signal"] = -code if code < 0 else None
    row["log_sha256"] = hashlib.sha256(log_path.read_bytes()).hexdigest()
    details = parse_test_counts(output)
    counts = (details["passed"], details["failed"]) if details else None

    if counts is not None:
        row.update(details)
        row["passed"], row["failed"] = counts
        row["executed"] = counts[0] + counts[1]

    if code < 0 or code >= 128:                              # killed by a signal / crash
        row["detail"] = f"crashed or was killed (exit {code}); collect a dump from the OS crash handler and attach it"
        row["crash"] = True
        return row

    if spec.expect_nonzero:
        # Guard step: the command must fail, and for a real reason (the test binary reports it), not a crash on start.
        ok = code == 1 and counts == (0, 0) and "ERROR: test filter matched zero tests" in output
        row["status"] = "pass" if ok else "fail"
        row["detail"] = "unmatched filter rejected as required" if ok else f"expected exit 1, zero-test summary and unmatched-filter diagnostic; got exit {code}"
        return row

    if code != 0:
        row["detail"] = f"exit {code}"
    elif spec.min_tests and row["executed"] < spec.min_tests:
        row["detail"] = f"executed {row['executed']} tests, at least {spec.min_tests} required (a step that runs nothing is a failure)"
    elif counts is not None and row["failed"] > 0:
        row["detail"] = f"{row['failed']} failed"
    else:
        row["status"] = "pass"

    return row


def python_test_files() -> list[str]:
    """tools/test_*.py as repository-relative paths, sorted, so the step is the same on every OS."""
    return sorted(p.relative_to(REPO).as_posix() for p in (REPO / "tools").glob("test_*.py"))


def auval_command(bundle: Path | None = None, *, cmake_file: Path = REPO / "CMakeLists.txt") -> list[str]:
    """Use built AudioComponents metadata when supplied; otherwise read this target's literal JUCE declarations."""
    if bundle is not None:
        with (bundle / "Contents" / "Info.plist").open("rb") as stream:
            components = plistlib.load(stream)["AudioComponents"]
        if len(components) != 1:
            raise ValueError("the AU bundle must declare exactly one AudioComponent")
        component = components[0]
        identity = [component[k] for k in ("type", "subtype", "manufacturer")]
    else:
        text = cmake_file.read_text(encoding="utf-8")
        target = re.search(r"juce_add_plugin\(GapFill\s+(.*?)\n\)", text, re.S)
        if target is None:
            raise ValueError("GapFill target declarations were not found; supply --au with the built bundle")
        def literal(name):
            match = re.search(r"\b" + name + r"\s+([A-Za-z0-9]+)\b", target[1])
            if match is None:
                raise ValueError(f"cannot resolve {name}; supply --au with the built bundle")
            return match[1]
        synth, midi = literal("IS_SYNTH"), literal("NEEDS_MIDI_INPUT")
        if synth not in ("TRUE", "FALSE") or midi not in ("TRUE", "FALSE"):
            raise ValueError("cannot resolve AU type; supply --au with the built bundle")
        kind = "aumu" if synth == "TRUE" else "aumf" if midi == "TRUE" else "aufx"
        identity = [kind, literal("PLUGIN_CODE"), literal("PLUGIN_MANUFACTURER_CODE")]
    if any(not isinstance(code, str) or len(code) != 4 or not code.isascii() for code in identity):
        raise ValueError("AudioComponent identity requires three ASCII FourCC values")
    return ["auval", "-v", *identity]


def default_steps(binary: list[str], tier: str, *, pluginval=None, vst3=None, clap_validator=None, clap=None,
                  au_bundle: Path | None = None) -> list[StepSpec]:
    py = sys.executable
    steps: list[StepSpec] = [
        StepSpec("preflight-validate-repo", [py, "-I", "tools/validate_repo.py"]),
        # Not `-I` and not `discover -s tools`: the tests do `from tools import ...`, which needs the repository root on
        # sys.path (what `-m` provides) and a file list (tools/ is a namespace package, which discover refuses).
        StepSpec("preflight-python-tests", [py, "-m", "unittest", *python_test_files()], min_tests=1),
        StepSpec("unit", binary + ["-Perf"], min_tests=100, tiers=("fast",)),
        StepSpec("unit-all", binary, min_tests=100, tiers=("full",)),
        StepSpec("bad-filter-must-fail", binary + ["nonexistent_filter_should_fail"], expect_nonzero=True),
        StepSpec("blocksweep", binary + ["Transport"], min_tests=5),
        StepSpec("blocksweep-every-size", binary + ["handles_every_common_block_size"], min_tests=1),
        StepSpec("ratesweep", binary + ["initialises_and_processes_at_every_supported_sample_rate"], min_tests=1),
        StepSpec("determinism", binary + ["repeatable"], min_tests=1),
        StepSpec("robust", binary + ["missing_tempo_information_is_handled"], min_tests=1),
        StepSpec("fixtures", binary + ["StateCompat"], min_tests=1),
        StepSpec("gui-lifecycle", binary + ["EditorLifecycle"], min_tests=1),
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
        steps.append(StepSpec("auval", auval_command(au_bundle), tiers=("full",)))
    else:
        steps.append(StepSpec("auval", None, tiers=("full",),
                              unverified_reason="auval unavailable" if platform.system() == "Darwin" else "not applicable: auval runs on macOS only",
                              extra={"applicable": platform.system() == "Darwin"}))

    return [s for s in steps if tier in s.tiers]


def apply_skips(steps: list[StepSpec], skip: list[str], reason: str) -> list[StepSpec]:
    """Replaces the named steps with `unverified` rows carrying `reason`: a skipped step stays visible in the evidence
    and is never reported as a pass. An unknown name raises ValueError so a typo cannot silently skip nothing."""
    known = {s.name for s in steps}
    unknown = sorted(set(skip) - known)
    if unknown:
        raise ValueError("--skip names no step in this tier: " + ", ".join(unknown))
    return [StepSpec(s.name, None, tiers=s.tiers, unverified_reason=reason,
                     extra={**s.extra, "skipped": True}) if s.name in skip else s for s in steps]


def git_sha(repo: Path = REPO) -> str:
    try:
        out = subprocess.run(["git", "rev-parse", "--short=12", "HEAD"], cwd=str(repo), capture_output=True, text=True, timeout=30)
        return out.stdout.strip() or "unknown"
    except (OSError, subprocess.SubprocessError):
        return "unknown"


def make_evidence(rows: list[dict], *, sha: str, os_name: str, command_line: str, tier: str,
                  provenance: dict | None = None) -> dict:
    return {
        "schema": 2,
        "sha": sha,
        "os": os_name,
        "python": platform.python_version(),
        "tier": tier,
        "command_line": command_line,
        "steps": rows,
        "provenance": provenance or {"build_configuration": "unverified", "tested_sha": sha},
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
    ap.add_argument("--skip", action="append", default=[], metavar="STEP",
                    help="step already run elsewhere (repeatable); recorded as unverified, never as a pass")
    ap.add_argument("--skip-reason", default="skipped on request: run by another job",
                    help="text recorded for every --skip step")
    ap.add_argument("--pluginval")
    ap.add_argument("--vst3")
    ap.add_argument("--clap-validator")
    ap.add_argument("--clap")
    ap.add_argument("--au", type=Path, help="built .component bundle whose AudioComponents identity auval must validate")
    ap.add_argument("--build-dir", type=Path, help="CMake build directory used for the tested binaries")
    ap.add_argument("--build-config", help="tested configuration (required for multi-config generators)")
    ap.add_argument("--source-head", help="exact PR source head when checkout is a synthetic tested merge commit")
    args = ap.parse_args(argv)

    out = Path(args.out)
    sha = git_sha()
    os_name = platform.system().lower()
    log_dir = out / f"{sha}-{os_name}-logs"
    binary = shlex.split(args.binary)
    if not binary:
        ap.error("--binary must select an executable")
    try:
        provenance = collect_provenance(REPO, binary, build_dir=args.build_dir, build_config=args.build_config,
                                        source_head=args.source_head, output_dir=out)
        steps = default_steps(binary, args.tier, pluginval=args.pluginval, vst3=args.vst3,
                              clap_validator=args.clap_validator, clap=args.clap, au_bundle=args.au)
    except (OSError, ValueError, TypeError, KeyError, plistlib.InvalidFileException) as exc:
        ap.error(str(exc))
    steps = apply_skips(steps, args.skip, args.skip_reason)
    rows = [run_step(s, cwd=REPO, log_dir=log_dir) for s in steps]
    versions = {"python": {"status": "observed", "text": sys.version}}
    for name, tool, options in (("pluginval", args.pluginval, ["--version"]),
                                 ("clap-validator", args.clap_validator, ["--version"]),
                                 ("auval", shutil.which("auval") if platform.system() == "Darwin" else None, ["-h"])):
        versions[name] = tool_version([tool, *options]) if tool else {"status": "unverified", "text": "tool unavailable"}
    for row in rows:
        if row["log"]:
            row["log_relative"] = Path(row["log"]).resolve().relative_to(out.resolve()).as_posix()
        row["tool_version"] = versions.get(row["name"], versions["python"] if row["name"].startswith("preflight") else
                                           {"status": "identified-by-hash", "text": provenance["binary_sha256"]})

    evidence = make_evidence(rows, sha=sha, os_name=os_name, command_line=" ".join(shlex.quote(a) for a in sys.argv),
                             tier=args.tier, provenance=provenance)
    out.mkdir(parents=True, exist_ok=True)
    (out / f"{sha}-{os_name}.json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
    text, code = summarise(evidence)
    print(text)
    return code


if __name__ == "__main__":
    sys.exit(main())
