#!/usr/bin/env python3
"""Prepare pinned validators and run the full native QA matrix, retaining evidence.

Build first. Run this under xvfb-run on headless Linux. macOS uses temporary
ad-hoc signatures for validation, which are never release-signing evidence.
An existing user AU install is preserved; use a clean CI account for AU QA.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import sys
import urllib.request
import zipfile

try:
    from .nightly_run import REPO
except ImportError:
    from nightly_run import REPO

PLUGINVAL_VERSION = "1.0.4"
CLAP_VALIDATOR_REV = "152b9823e992d782c5c1fd33bca0295478b919aa"  # upstream 0.4.1


def run(command):
    subprocess.run([str(arg) for arg in command], cwd=REPO, check=True)


def prepare_tools(directory: Path) -> tuple[Path, Path, dict]:
    system = platform.system()
    suffix = ".exe" if system == "Windows" else ""
    archive_os = {"Linux": "Linux", "Windows": "Windows", "Darwin": "macOS"}[system]
    directory.mkdir(parents=True, exist_ok=True)
    archive = directory / "pluginval.zip"
    url = f"https://github.com/Tracktion/pluginval/releases/download/v{PLUGINVAL_VERSION}/pluginval_{archive_os}.zip"
    urllib.request.urlretrieve(url, archive)
    root = directory / "pluginval"
    root.mkdir(exist_ok=True)
    with zipfile.ZipFile(archive) as zipped:
        for member in zipped.infolist():
            if not (root / member.filename).resolve().is_relative_to(root.resolve()):
                raise ValueError("validator archive contains an unsafe path")
        zipped.extractall(root)
    candidates = [p for p in root.rglob("pluginval" + suffix) if p.is_file()]
    if len(candidates) != 1:
        raise ValueError("expected exactly one pluginval executable")
    pluginval = candidates[0]
    pluginval.chmod(pluginval.stat().st_mode | 0o111)
    cargo_root = directory / "cargo"
    run(["cargo", "install", "--git", "https://github.com/free-audio/clap-validator", "--rev",
         CLAP_VALIDATOR_REV, "--locked", "--root", cargo_root, "clap-validator"])
    clap = cargo_root / "bin" / ("clap-validator" + suffix)
    tools = {"pluginval": {"version": PLUGINVAL_VERSION, "url": url},
             "clap-validator": {"source_revision": CLAP_VALIDATOR_REV}}
    for name, path in (("pluginval", pluginval), ("clap-validator", clap)):
        tools[name].update(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    return pluginval, clap, tools


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--tools-dir", required=True, type=Path)
    parser.add_argument("--config", default="Release")
    args = parser.parse_args(argv)
    args.out.mkdir(parents=True, exist_ok=True)
    binary_name = "GapFillTests.exe" if platform.system() == "Windows" else "GapFillTests"
    binary = args.build_dir / "artefacts" / args.config / binary_name
    if not binary.is_file():
        binary = args.build_dir / "artefacts" / binary_name
    formats = args.build_dir / "GapFill_artefacts" / args.config
    component = formats / "AU/GapFill.component"
    installed_component = None
    try:
        if not binary.is_file():
            raise FileNotFoundError(f"test binary not found: {binary}")
        pluginval, clap, tools = prepare_tools(args.tools_dir)
        (args.out / "validators.json").write_text(json.dumps(tools, indent=2) + "\n")
        if platform.system() == "Darwin":
            apps = list((args.build_dir / "artefacts").rglob("GapFill.app"))
            if len(apps) != 1:
                raise ValueError("expected exactly one native standalone app")
            for bundle in (formats / "VST3/GapFill.vst3", formats / "CLAP/GapFill.clap", component,
                           apps[0]):
                if bundle.is_dir():
                    (bundle / "Contents/Resources").mkdir(parents=True, exist_ok=True)
                run(["codesign", "--force", "--deep", "--sign", "-", bundle])
                run(["codesign", "--verify", "--deep", "--strict", bundle])
            destination = Path.home() / "Library/Audio/Plug-Ins/Components/GapFill.component"
            if destination.exists() or destination.is_symlink():
                raise FileExistsError("existing GapFill AU preserved; run native nightly QA in a clean account")
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(component, destination)
            installed_component = destination
        evidence = args.out / "qa-matrix"
        command = [sys.executable, REPO / "tools/qa/run_matrix.py", "--binary", shlex.quote(binary.as_posix()),
                   "--tier", "full", "--out", evidence, "--build-dir", args.build_dir,
                   "--build-config", args.config, "--pluginval", pluginval,
                   "--vst3", formats / "VST3/GapFill.vst3", "--clap-validator", clap,
                   "--clap", formats / "CLAP/GapFill.clap"]
        if os.environ.get("GITHUB_SHA"):
            command += ["--source-head", os.environ["GITHUB_SHA"]]
        if platform.system() == "Darwin":
            command += ["--au", component]
        result = subprocess.run([str(arg) for arg in command], cwd=REPO)
        files = sorted(evidence.glob("*.json"))
        if not files:
            raise RuntimeError("full QA emitted no evidence")
        strict = subprocess.run([sys.executable, str(REPO / "tools/qa/summarise.py"), "--strict",
                                 "--summary-out", str(args.out / "strict-summary.json"), *map(str, files)], cwd=REPO)
        return 0 if result.returncode == strict.returncode == 0 else 1
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        (args.out / "preparation-failure.json").write_text(json.dumps({"status": "fail", "detail": str(exc)}, indent=2) + "\n")
        print(f"native nightly QA failed: {exc}", file=sys.stderr)
        return 1
    finally:
        if installed_component is not None:
            shutil.rmtree(installed_component)


if __name__ == "__main__":
    raise SystemExit(main())
