#!/usr/bin/env python3
"""Analyze timing-only host callback CSV; never retunes the production tracker.

Statistics are observations of supplied data, not DAW/hardware acceptance.
Capture loss, sample-position discontinuities and operator-marked events stay
separate from PPQ residuals. All provenance and input hashes are retained.
"""
from __future__ import annotations

import argparse
from array import array
from collections import Counter
import csv
import hashlib
import json
import math
from pathlib import Path

FIELDS = ("sequence", "playing", "ppq_valid", "tempo_valid", "ppq", "bpm", "block_samples",
          "sample_rate", "sample_position_valid", "sample_position", "non_realtime")
TOLERANCE_BEATS = .25
MAX_BYTES = 256 * 1024 * 1024
MAX_ROWS = 2_000_000


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def bounded_json(path: Path):
    if not path.is_file() or path.stat().st_size > 256 * 1024:
        raise ValueError("metadata/annotation file exceeds 256KiB or is missing")
    result = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(result, dict) or result.get("schema") != 1:
        raise ValueError("unsupported timing metadata/annotation schema")
    return result


def integer(value, low=0, high=2**63 - 1) -> int:
    if isinstance(value, bool) or not str(value).lstrip("-").isdigit():
        raise ValueError("trace counter must be an integer")
    result = int(value)
    if not low <= result <= high:
        raise ValueError("trace counter outside supported bounds")
    return result


def parse_row(raw: dict) -> dict:
    if set(raw) != set(FIELDS) or any(value is None or len(value) > 256 for value in raw.values()):
        raise ValueError("malformed timing CSV row")
    result = {}
    for flag in ("playing", "ppq_valid", "tempo_valid", "sample_position_valid", "non_realtime"):
        result[flag] = bool(integer(raw[flag], 0, 1))
    for field, low, high in (("sequence", 0, 2**63 - 1), ("block_samples", 0, 2**31 - 1),
                              ("sample_position", -(2**63), 2**63 - 1)):
        result[field] = integer(raw[field], low, high)
    for field, low, high in (("ppq", -1e12, 1e12), ("bpm", 1, 1000), ("sample_rate", 8000, 192000)):
        value = float(raw[field])
        if not math.isfinite(value) or not low <= value <= high:
            raise ValueError("nonfinite/unsupported timing value: " + field)
        result[field] = value
    return result


class Distribution:
    def __init__(self):
        self.values = array("d")

    def add(self, value):
        self.values.append(abs(value))

    def report(self):
        if not self.values:
            return {"count": 0, "status": "unverified: no eligible realtime intervals"}
        ordered = sorted(self.values)

        def percentile(fraction):
            return ordered[max(0, math.ceil(fraction * len(ordered)) - 1)]

        return {"count": len(ordered), "median_abs_beats": percentile(.5), "p95_abs_beats": percentile(.95),
                "p99_abs_beats": percentile(.99), "p999_abs_beats": percentile(.999),
                "max_abs_beats": ordered[-1], "over_tolerance": sum(value > TOLERANCE_BEATS for value in ordered)}


def analyze(trace: Path, metadata: Path | None = None, markers: Path | None = None) -> dict:
    if not trace.is_file() or trace.stat().st_size > MAX_BYTES:
        raise ValueError("trace missing or exceeds 256MiB")
    original_hash = digest(trace)
    provenance = bounded_json(metadata) if metadata else {}
    annotations = {}
    if markers:
        for event in bounded_json(markers).get("events", []):
            sequence = integer(event["sequence"])
            kind = event["kind"]
            if kind not in ("seek", "loop", "pdc", "rate_change") or sequence in annotations:
                raise ValueError("invalid/duplicate operator event annotation")
            annotations[sequence] = kind
    models = {name: Distribution() for name in ("previous_bpm", "endpoint_average", "explicit_hypotheses")}
    events = Counter(missing_rows=0)
    block_sizes, sample_rates = Counter(), Counter()
    minimum_bpm, maximum_bpm = None, None
    rows = 0
    previous = None
    seconds = 0.0
    old_limit = csv.field_size_limit(512)
    try:
        with trace.open(encoding="utf-8", newline="") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != list(FIELDS):
                raise ValueError("unsupported timing CSV header")
            for raw in reader:
                rows += 1
                if rows > MAX_ROWS:
                    raise ValueError("trace exceeds two million callbacks")
                current = parse_row(raw)
                block_sizes[str(current["block_samples"])] += 1
                sample_rates[str(current["sample_rate"])] += 1
                if current["tempo_valid"]:
                    minimum_bpm = current["bpm"] if minimum_bpm is None else min(minimum_bpm, current["bpm"])
                    maximum_bpm = current["bpm"] if maximum_bpm is None else max(maximum_bpm, current["bpm"])
                if previous and current["sequence"] <= previous["sequence"]:
                    raise ValueError("nonincreasing callback sequence")
                expected_sequence = previous["sequence"] + 1 if previous else 0
                missing = current["sequence"] - expected_sequence
                eligible = previous is not None
                if missing:
                    events["missing_rows"] += missing
                    events["sequence_gaps"] += 1
                    eligible = False
                if current["sequence"] in annotations:
                    events["annotated_" + annotations[current["sequence"]]] += 1
                    eligible = False
                if previous:
                    if previous["non_realtime"] or current["non_realtime"]:
                        events["offline_interval"] += 1
                        eligible = False
                    if not previous["playing"] or not current["playing"]:
                        events["play_state_transition" if previous["playing"] != current["playing"] else "stopped_interval"] += 1
                        eligible = False
                    if not all(row[flag] for row in (previous, current) for flag in ("ppq_valid", "tempo_valid")):
                        events["timing_unavailable"] += 1
                        eligible = False
                    if current["sample_rate"] != previous["sample_rate"]:
                        events["sample_rate_change"] += 1
                        eligible = False
                    if not missing and previous["sample_position_valid"] and current["sample_position_valid"]:
                        if current["sample_position"] != previous["sample_position"] + previous["block_samples"]:
                            events["sample_position_discontinuity"] += 1
                            eligible = False
                if eligible:
                    duration = previous["block_samples"] / previous["sample_rate"]
                    expected_step = previous["ppq"] + previous["block_samples"] / (60 / previous["bpm"] * previous["sample_rate"])
                    expected_ramp = (previous["ppq"] + duration * (previous["bpm"] + current["bpm"]) / 120
                                     if previous["bpm"] != current["bpm"] else expected_step)
                    step_error = current["ppq"] - expected_step
                    ramp_error = current["ppq"] - expected_ramp
                    models["previous_bpm"].add(step_error)
                    models["endpoint_average"].add(ramp_error)
                    models["explicit_hypotheses"].add(min(abs(step_error), abs(ramp_error)))
                    seconds += duration
                previous = current
    finally:
        csv.field_size_limit(old_limit)
    if not rows:
        raise ValueError("empty timing trace")
    if digest(trace) != original_hash:
        raise ValueError("trace changed while it was being analyzed; finalize capture first")
    complete = provenance.get("complete") is True and provenance.get("io_error") is False
    if provenance:
        written = integer(provenance.get("rows_written"))
        callbacks = integer(provenance.get("callbacks_seen"))
        dropped = integer(provenance.get("dropped_rows"))
        if written != rows or callbacks < rows or callbacks - rows != dropped or previous["sequence"] >= callbacks:
            raise ValueError("CSV and declared capture counters disagree")
        events["missing_rows"] += callbacks - previous["sequence"] - 1
        if events["missing_rows"] != dropped:
            raise ValueError("declared dropped rows disagree with callback sequence")
        complete = complete and dropped == 0
    else:
        complete = False
    result = {"schema": 1, "kind": "host-ppq-trace-analysis", "input": {"name": trace.name, "sha256": original_hash,
               "bytes": trace.stat().st_size}, "declared_provenance": provenance,
              "rows": rows, "eligible_audio_seconds": seconds,
              "duration_basis": "sum of eligible previous callback frames/rate; not elapsed wall time",
              "block_sizes": dict(sorted(block_sizes.items())), "sample_rates": dict(sorted(sample_rates.items())),
              "valid_bpm_range": [minimum_bpm, maximum_bpm], "events": dict(events),
              "data_integrity": "complete" if complete else "incomplete", "tolerance_beats": TOLERANCE_BEATS,
              "models": {name: model.report() for name, model in models.items()},
              "real_host_acceptance": "unverified: requires operator confirmation of host/scenario and retained build evidence",
              "tolerance_change": "none; two real-host measurements and review remain required"}
    if metadata:
        result["input"]["metadata_sha256"] = digest(metadata)
    if markers:
        result["input"]["markers_sha256"] = digest(markers)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--metadata", type=Path)
    parser.add_argument("--markers", type=Path)
    parser.add_argument("--probe-binary", type=Path)
    parser.add_argument("--gapfill-binary", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        result = analyze(args.trace, args.metadata, args.markers)
        if args.probe_binary:
            result["probe_binary_sha256"] = digest(args.probe_binary)
        if args.gapfill_binary:
            result["gapfill_binary_sha256"] = digest(args.gapfill_binary)
        code = 0
    except (OSError, ValueError, KeyError, TypeError, csv.Error) as exc:
        result = {"schema": 1, "status": "fail", "detail": str(exc), "real_host_acceptance": "unverified"}
        code = 1
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
