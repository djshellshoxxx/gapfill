"""Strict automated-coverage reconciliation. Raw step statuses are never rewritten.

This is an offline evidence consistency gate, not host/signing/release approval.
Files are trusted local CI outputs; hashes detect stale or damaged attachments, not forged reports.
"""

from __future__ import annotations

import hashlib
import json
import math
import re
from pathlib import Path

COMMON = {"preflight-validate-repo", "preflight-python-tests", "bad-filter-must-fail", "blocksweep",
          "blocksweep-every-size", "ratesweep", "determinism", "robust", "fixtures", "gui-lifecycle"}
FULL = COMMON | {"unit-all", "perf", "pluginval", "clap-validator", "auval"}
TEST_STEPS = {"preflight-python-tests", "unit", "unit-all", "blocksweep", "blocksweep-every-size",
              "ratesweep", "determinism", "robust", "fixtures", "gui-lifecycle", "perf"}


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_structure(evidence: object) -> None:
    if not isinstance(evidence, dict):
        raise ValueError("evidence must be a JSON object")
    for key in ("sha", "os", "tier", "steps"):
        if key not in evidence:
            raise ValueError(f"evidence lacks {key}")
    if not all(isinstance(evidence[key], str) and evidence[key] for key in ("sha", "os")):
        raise ValueError("invalid SHA or OS")
    if evidence["tier"] not in ("fast", "full") or not isinstance(evidence["steps"], list):
        raise ValueError("invalid tier or steps")
    seen = set()
    for row in evidence["steps"]:
        if not isinstance(row, dict) or not isinstance(row.get("name"), str) or row["name"] in seen:
            raise ValueError("invalid or duplicate step")
        seen.add(row["name"])
        if row.get("status") not in ("pass", "fail", "unverified"):
            raise ValueError(f"invalid status for {row['name']}")
        for key in ("passed", "failed", "executed"):
            if type(row.get(key)) is not int or row[key] < 0:
                raise ValueError(f"invalid {key} for {row['name']}")
        if row["executed"] != row["passed"] + row["failed"]:
            raise ValueError(f"inconsistent counts for {row['name']}")
        seconds = row.get("seconds")
        if not isinstance(seconds, (int, float)) or not math.isfinite(seconds) or seconds < 0:
            raise ValueError(f"invalid duration for {row['name']}")
        if not all(isinstance(row.get(k), str) for k in ("command", "log", "detail")):
            raise ValueError(f"invalid command/log/detail for {row['name']}")


def candidate_key(evidence: dict) -> str:
    """Only complete, clean, identical build identities can share coverage."""
    p = evidence.get("provenance", {})
    if not isinstance(p, dict) or p.get("dirty") is not False:
        raise ValueError("clean source provenance is required")
    sha = p.get("tested_sha", "")
    if not isinstance(sha, str) or not re.fullmatch(r"[0-9a-f]{40}", sha) or not sha.startswith(evidence["sha"]):
        raise ValueError("exact tested SHA is missing or conflicts with the report SHA")
    for key in ("architecture", "build_configuration"):
        if not p.get(key) or p[key] == "unverified":
            raise ValueError(f"missing {key}")
    compiler = p.get("compiler", {})
    if not isinstance(compiler, dict) or not all(compiler.get(k) and compiler[k] != "unverified" for k in ("id", "version")):
        raise ValueError("compiler ID/version is unverified")
    deps = p.get("dependencies", {})
    if not isinstance(deps, dict) or not all(isinstance(deps.get(k), str) and re.fullmatch(r"[0-9a-f]{40}", deps[k])
                                             for k in ("JUCE", "clap-juce-extensions")):
        raise ValueError("JUCE/CLAP dependency revisions are unverified")
    dependency_dirty = p.get("dependency_dirty", {})
    if not isinstance(dependency_dirty, dict) or not all(dependency_dirty.get(k) is False for k in deps):
        raise ValueError("clean dependency checkouts are not established")
    if not isinstance(p.get("binary_sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", p["binary_sha256"]):
        raise ValueError("test binary hash is unverified")
    identity = {k: p[k] for k in ("tested_sha", "architecture", "build_configuration", "compiler", "dependencies", "binary_sha256")}
    identity["os"] = evidence["os"]
    return json.dumps(identity, sort_keys=True)


def row_problem(row: dict, evidence_path: Path, count_parser) -> str:
    if row.get("status") != "pass":
        return "not passed"
    expected_exit = 1 if row["name"] == "bad-filter-must-fail" else 0
    if row.get("exit_code") != expected_exit or row.get("crash") or row.get("signal"):
        return "exit/signal does not establish success"
    if row.get("failed", 0) != 0:
        return "failed tests were reported"
    minimum = 100 if row["name"] in ("unit", "unit-all") else 1
    if row["name"] in TEST_STEPS and (not isinstance(row.get("executed"), int) or row["executed"] < minimum):
        return "missing or insufficient executed-test count"
    log = Path(row.get("log", ""))
    relative = row.get("log_relative")
    if relative:
        log = evidence_path.parent / relative
    elif not log.is_absolute():
        log = evidence_path.parent / log
    try:
        data = log.read_bytes()
        if not row.get("log_sha256") or hashlib.sha256(data).hexdigest() != row["log_sha256"]:
            return "raw log hash is missing or mismatched"
    except (OSError, ValueError):
        return "raw log is unavailable"
    output = data.decode("utf-8", "replace")
    if row["name"] in TEST_STEPS:
        counts = count_parser(output)
        if not counts or any(counts[k] != row[k] for k in ("passed", "failed")):
            return "raw log test counts do not match claimed coverage"
    if row["name"] == "bad-filter-must-fail":
        counts = count_parser(output)
        if not counts or counts["reported_tests"] != 0 or "ERROR: test filter matched zero tests" not in output:
            return "raw log does not establish unmatched-filter rejection"
    return ""


def strict_summary(reports: list[tuple[Path, dict]], count_parser) -> dict:
    """Reconcile each requested report; `unit-all` may cover an explicitly skipped `unit` only."""
    issues, links, manual = [], [], set()
    keys = {}
    for path, evidence in reports:
        manual.update(evidence.get("manual_rows_open", []))
        try:
            keys[str(path)] = candidate_key(evidence)
        except (TypeError, ValueError) as exc:
            issues.append(f"{path}: {exc}")
    full_keys = {keys[str(path)] for path, evidence in reports if evidence["tier"] == "full" and str(path) in keys}
    for path, evidence in reports:
        if str(path) in keys and keys[str(path)] not in full_keys:
            issues.append(f"{path}: full-tier evidence for this candidate is required; fast-tier success is incomplete")
    for path, evidence in reports:
        rows = {r["name"]: r for r in evidence["steps"]}
        required = FULL if evidence["tier"] == "full" else COMMON | {"unit"}
        missing = required - rows.keys()
        if missing:
            issues.append(f"{path}: missing required steps: {', '.join(sorted(missing))}")
        for name, row in rows.items():
            if name == "auval" and evidence["os"] != "darwin" and row.get("applicable") is False:
                continue
            if row.get("status") == "pass":
                problem = row_problem(row, path, count_parser)
                if problem:
                    issues.append(f"{path}: {name}: {problem}")
                continue
            linked = False
            if name == "unit" and row.get("skipped") is True and str(path) in keys:
                for donor_path, donor in reports:
                    if keys.get(str(donor_path)) != keys[str(path)] or donor["tier"] != "full":
                        continue
                    source = next((r for r in donor["steps"] if r["name"] == "unit-all"), None)
                    if source and not row_problem(source, donor_path, count_parser):
                        links.append({"evidence_file": str(path), "step": "unit", "source_file": str(donor_path),
                                      "source_step": "unit-all", "evidence_sha256": file_hash(donor_path),
                                      "log_sha256": source["log_sha256"]})
                        linked = True
                        break
            if not linked:
                issues.append(f"{path}: {name}: {row.get('status')} ({row.get('detail', '')})")
    return {"schema": 1, "scope": "automated acceptance only", "status": "blocked" if issues else "complete",
            "issues": issues, "coverage_links": links, "manual_rows_open": sorted(manual),
            "release_acceptance": "not established: host/hardware/listening/signing acceptance is separate"}
