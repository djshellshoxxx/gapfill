#!/usr/bin/env python3
"""Run ONE persistent GapFillTests Soak process and measure its resident memory.

Use under xvfb-run on headless Linux. A short run verifies the harness; only
the recorded continuous measured duration establishes an 8-hour/7-day soak.
This exercises a processor without an audio device and cannot establish xruns
or listening quality in a real host.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import time

try:
    from .nightly_run import COUNTS, REPO, provenance
except ImportError:
    from nightly_run import COUNTS, REPO, provenance


def resident_bytes(pid: int) -> int | None:
    try:
        if platform.system() == "Linux":
            return int(Path(f"/proc/{pid}/statm").read_text().split()[1]) * os.sysconf("SC_PAGE_SIZE")
        if platform.system() == "Darwin":
            result = subprocess.run(["ps", "-o", "rss=", "-p", str(pid)], capture_output=True, text=True, timeout=5)
            return int(result.stdout.strip()) * 1024 if result.returncode == 0 else None
        if platform.system() == "Windows":
            from ctypes import wintypes

            class Counters(ctypes.Structure):
                _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
                    (name, ctypes.c_size_t) for name in ("PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
                                                       "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage",
                                                       "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage")]

            kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel.OpenProcess.restype = wintypes.HANDLE
            kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            handle = kernel.OpenProcess(0x1000 | 0x10, False, pid)
            if not handle:
                return None
            try:
                counters = Counters()
                counters.cb = ctypes.sizeof(counters)
                psapi = ctypes.WinDLL("psapi", use_last_error=True)
                psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
                if psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
                    return int(counters.WorkingSetSize)
            finally:
                kernel.CloseHandle(handle)
    except (OSError, ValueError, IndexError, subprocess.SubprocessError):
        return None
    return None


def rss_result(values: list[int]) -> dict:
    if len(values) < 2 or any(value <= 0 for value in values):
        return {"status": "unverified", "detail": "at least two valid post-warmup RSS samples required"}
    width = min(3, len(values) // 2)
    baseline = statistics.median(values[:width])
    final = statistics.median(values[-width:])
    growth = (final - baseline) * 100.0 / baseline
    return {"status": "pass" if growth <= 1.0 else "fail", "growth_percent": growth,
            "baseline_bytes": baseline, "final_bytes": final, "peak_bytes": max(values),
            "samples": len(values), "criterion": "final median RSS <= warmed baseline median + 1 percent"}


def run(binary: Path, *, seconds: float, warmup: float, seed: int, out: Path,
        build_dir: Path | None = None, config: str = "Release") -> dict:
    out.mkdir(parents=True, exist_ok=True)
    log = (out / "soak.log").resolve()
    env = dict(os.environ, GF_SOAK_SECONDS=str(seconds), GF_SOAK_WARMUP_SECONDS=str(warmup), GF_SOAK_SEED=str(seed))
    command = [str(binary), "Soak"]
    row = {"schema": 1, "kind": "continuous-headless-processor-soak", "command": command,
           "requested_seconds": seconds, "warmup_seconds": warmup, "seed": seed, "log": str(log),
           "process_starts": 1, "manual_open": ["real-host xruns", "hardware", "listening"],
           **provenance(binary, build_dir, config)}
    started = time.monotonic()
    samples = []
    code = 127
    with log.open("w", encoding="utf-8") as stream:
        try:
            process = subprocess.Popen(command, cwd=REPO, env=env, stdout=stream, stderr=subprocess.STDOUT)
            row["pid"] = process.pid
            while process.poll() is None:
                now = time.monotonic()
                if now - started > seconds + warmup + 120:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                    code = 124
                    break
                # The processor emits this only AFTER its own warmup. Launch/setup
                # time is never treated as measured processor-soak duration.
                progress = log.read_text(encoding="utf-8", errors="replace")
                if "GF_SOAK_BEGIN\n" in progress and "GF_SOAK_END\n" not in progress:
                    memory = resident_bytes(process.pid)
                    if memory is not None and memory > 0:
                        samples.append({"seconds_from_launch": now - started, "resident_bytes": memory})
                time.sleep(max(0.01, min(5.0, seconds / 10.0)))
            else:
                code = process.returncode
        except OSError as exc:
            stream.write(f"cannot execute processor soak: {exc}\n")
    text = log.read_text(encoding="utf-8", errors="replace")
    summary = None
    for line in text.splitlines():
        if line.startswith("GF_SOAK_SUMMARY "):
            try:
                summary = json.loads(line.removeprefix("GF_SOAK_SUMMARY "))
            except json.JSONDecodeError:
                summary = None
    counts = COUNTS.findall(text)
    passed, failed = map(int, counts[-1]) if counts else (0, 0)
    begin = text.find("GF_SOAK_BEGIN\n")
    end = text.find("GF_SOAK_END\n")
    valid = (0 <= begin < end < text.find("GF_SOAK_SUMMARY ") and isinstance(summary, dict) and all(key in summary for key in
        ("measured_seconds", "blocks", "processed_samples", "peak", "allocations", "engine_idle")))
    if valid:
        valid = (all(type(summary[key]) in (int, float) for key in
                     ("measured_seconds", "blocks", "processed_samples", "peak", "allocations"))
                 and math.isfinite(summary["measured_seconds"]) and summary["measured_seconds"] >= seconds
                 and summary["blocks"] > 0 and summary["processed_samples"] > 0
                 and math.isfinite(summary["peak"]) and summary["peak"] <= 2.0001
                 and summary["allocations"] == 0 and summary["engine_idle"] is True)
    memory = rss_result([sample["resident_bytes"] for sample in samples])
    processor_ok = code == 0 and valid and passed >= 1 and failed == 0
    row.update(seconds_from_launch=time.monotonic() - started, exit_code=code, processor=summary,
               processor_status="pass" if processor_ok else "fail",
               rss_measurement_window="GF_SOAK_BEGIN..GF_SOAK_END (excludes drain/destruction)",
               passed=passed, failed=failed, executed=passed + failed, rss=memory, rss_samples=samples)
    if not processor_ok:
        row.update(status="fail", detail="missing/invalid processor summary, failed tests, short duration or process failure")
    else:
        row.update(status=memory["status"], detail="processor completed; RSS criterion: " + memory["status"])
    (out / "soak.json").write_text(json.dumps(row, indent=2) + "\n", encoding="utf-8")
    return row


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--warmup", type=float, default=30)
    parser.add_argument("--seed", type=int, default=20261031)
    parser.add_argument("--out", type=Path, default=Path("soak-evidence"))
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--config", default="Release")
    args = parser.parse_args(argv)
    if not math.isfinite(args.seconds) or args.seconds <= 0 or not math.isfinite(args.warmup) or args.warmup < 0:
        parser.error("seconds must be finite and positive; warmup finite and nonnegative")
    row = run(args.binary.resolve(), seconds=args.seconds, warmup=args.warmup, seed=args.seed, out=args.out,
              build_dir=args.build_dir, config=args.config)
    print(f"soak: {row['status']}; continuous requested duration {args.seconds}s; {row['detail']}")
    return {"pass": 0, "fail": 1, "unverified": 2}[row["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
