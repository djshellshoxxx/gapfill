#!/usr/bin/env python3
"""Native release-candidate orchestration; never publishes, tags or imports keys.

Signing selectors refer only to identities already provisioned on the chosen
runner. Missing signing credentials retain unverified evidence in beta mode;
--require-signing fails closed. Validator failures always fail the candidate.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

import sign_release
import verify_release

REPO = Path(__file__).resolve().parents[1]


def asset_tag(value: str) -> str:
    if not re.fullmatch(r"\d+\.\d+\.\d+-beta\d+", value):
        raise argparse.ArgumentTypeError("asset tag must look like 0.0.2-beta1")
    return value


def invoke_sign(action: str, out: Path, required: bool, *arguments: str) -> dict:
    code = sign_release.main([action, "--out", str(out), *map(str, arguments)])
    report = json.loads(out.read_text(encoding="utf-8"))
    if code == 1 or (required and code != 0):
        raise RuntimeError(f"required {action} operation did not pass: {report.get('detail')}")
    return report


def windows_tools() -> None:
    # Hosted runners have the SDK installed, but signtool need not be on PATH.
    if platform.system() == "Windows" and shutil.which("signtool") is None:
        sdk = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Windows Kits/10/bin"
        candidates = sorted(sdk.glob("*/x64/signtool.exe"), reverse=True)
        if candidates:
            os.environ["PATH"] = str(candidates[0].parent) + os.pathsep + os.environ.get("PATH", "")


def windows(args) -> dict:
    windows_tools()
    if args.action == "windows-raw":
        inputs = [verify_release.unique((p for p in args.directory.rglob(name) if p.is_file()), name)
                  for name in ("GapFill.exe", "GapFill.clap")]
        inputs += [verify_release.unique((p for p in args.directory.rglob("*.vst3") if p.is_file()), "VST3 binary")]
    else:
        inputs = [args.directory / f"GapFill-{args.asset_tag}-Windows-x64-Setup.exe"]
    return invoke_sign("windows", args.out / "windows-signing.json", args.require_signing,
                       *[part for path in inputs for part in ("--input", str(path))])


def package_macos(args) -> dict:
    args.dist.mkdir(parents=True, exist_ok=True)
    bundles = [args.directory / item for item in
               ("VST3/GapFill.vst3", "AU/GapFill.component", "CLAP/GapFill.clap", "Standalone/GapFill.app")]
    signing = invoke_sign("macos", args.out / "macos-signing.json", args.require_signing,
                          *[part for path in bundles for part in ("--input", str(path))])
    package = args.dist / f"GapFill-{args.asset_tag}-macOS-{args.arch}.pkg"
    installer = invoke_sign("macos-installer", args.out / "macos-installer.json", args.require_signing,
                            "--input", str(args.directory), "--version", args.asset_tag.split("-")[0],
                            "--arch", args.arch, "--package", str(package))
    env = dict(os.environ, GAPFILL_ASSET_TAG=args.asset_tag,
               GAPFILL_DISPLAY_VERSION=args.asset_tag.replace("-", " "))

    def archive():
        subprocess.run(["bash", str(REPO / "packaging/package_macos.sh"), str(args.directory), args.arch,
                        str(args.dist)], check=True, env=env)

    archive()
    inputs = [args.dist / f"GapFill-{args.asset_tag}-macOS-{args.arch}.zip"]
    if installer["status"] == "pass":
        inputs.append(package)
    notarization = invoke_sign("notarize", args.out / "notarization.json", args.require_signing,
                               *[part for path in inputs for part in ("--input", str(path))],
                               "--staple", str(bundles[-1]),
                               *(["--staple", str(package)] if package.exists() else []))
    if notarization["status"] == "pass":
        # The final archive must contain the stapled app. Signatures on code
        # remain identical; checksums are generated only after this repack.
        env["GAPFILL_MACOS_SIGNING_STATUS"] = "Developer ID signed; app notarization ticket stapled and verified."
        archive()
    return {"signing": signing["status"], "installer": installer["status"],
            "notarization": notarization["status"]}


def manifest(args) -> dict:
    return invoke_sign("checksums", args.out / "checksum-signing.json", args.require_signing,
                       "--directory", str(args.directory), "--manifest-name",
                       f"GapFill-{args.asset_tag}-SHA256SUMS.txt")


def package_specs(target: str, tag: str, arch: str) -> list[tuple[str, list[str]]]:
    prefix = f"GapFill-{tag}-"
    if target == "macos":
        return [(prefix + f"macOS-{arch}.zip", ["vst3", "clap", "standalone", "au"])]
    label = "Windows" if target == "windows" else "Linux"
    return [(prefix + f"{label}-x64-{kind}.zip", formats) for kind, formats in
            (("Portable", ["vst3", "clap", "standalone"] if target == "windows" else ["standalone"]),
             ("VST3", ["vst3"]), ("CLAP", ["clap"]))]


def validate(args) -> dict:
    windows_tools()
    sys.path.insert(0, str(REPO))
    from tools.qa.nightly_native import prepare_tools
    pluginval, clap, tools = prepare_tools(args.tools_dir)
    (args.out / "validators.json").write_text(json.dumps(tools, indent=2) + "\n", encoding="utf-8")
    manifest_path = args.directory / f"GapFill-{args.asset_tag}-SHA256SUMS.txt"
    # This intentionally verifies EVERY listed asset on each runner. Download
    # the entire candidate/release, not only that runner's platform subset.
    verify_release.checksums(manifest_path)
    reports = []
    for archive, formats in package_specs(args.platform, args.asset_tag, args.arch):
        command = ["validate", "--archive", str(args.directory / archive), "--manifest", str(manifest_path),
                   "--out", str(args.out / archive.removesuffix(".zip")), "--platform", args.platform,
                   "--arch", args.arch, "--formats", *formats, "--pluginval", str(pluginval),
                   "--clap-validator", str(clap)]
        if args.require_signing:
            command += ["--require-signing", "--signature", str(manifest_path) + ".asc"]
            trusted = os.environ.get("GAPFILL_RELEASE_TRUSTED_KEYRING")
            if trusted:
                command += ["--trusted-keyring", trusted]
        code = verify_release.main(command)
        if code:
            raise RuntimeError(f"downloaded package validation did not pass: {archive} (exit {code})")
        reports.append(archive)
    installer_checks = []
    if args.require_signing and args.platform in ("windows", "macos"):
        installer = args.directory / (f"GapFill-{args.asset_tag}-Windows-x64-Setup.exe" if args.platform == "windows"
                                      else f"GapFill-{args.asset_tag}-macOS-{args.arch}.pkg")
        commands = [["signtool", "verify", "/pa", "/all", "/v", str(installer)]] if args.platform == "windows" else [
            ["pkgutil", "--check-signature", str(installer)], ["xcrun", "stapler", "validate", str(installer)],
            ["spctl", "--assess", "--type", "install", "--verbose=4", str(installer)]]
        for index, command in enumerate(commands):
            result = verify_release.execute(command, args.out, f"installer-signature-{index}")
            installer_checks.append(result)
            if result["status"] != "pass":
                raise RuntimeError("downloaded installer signing check failed")
    return {"status": "pass", "packages": reports, "installer_signatures": installer_checks,
            "signing_acceptance": "pass" if args.require_signing else "unverified: signing not required for this beta",
            "manual_acceptance": "unverified: install/upgrade/uninstall, DAWs, hardware and listening remain separate"}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("windows-raw", "windows-installer", "macos", "manifest", "validate"))
    parser.add_argument("--directory", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--asset-tag", required=True, type=asset_tag)
    parser.add_argument("--require-signing", action="store_true")
    parser.add_argument("--dist", type=Path)
    parser.add_argument("--tools-dir", type=Path)
    parser.add_argument("--platform", choices=("linux", "windows", "macos"))
    parser.add_argument("--arch", choices=("arm64", "x86_64"), default="x86_64")
    args = parser.parse_args(argv)
    if args.action == "macos" and args.dist is None or args.action == "validate" and (args.tools_dir is None or args.platform is None):
        parser.error("macos requires --dist; validate requires --tools-dir and --platform")
    args.out.mkdir(parents=True, exist_ok=True)
    try:
        result = (windows(args) if args.action.startswith("windows-") else package_macos(args) if args.action == "macos"
                  else manifest(args) if args.action == "manifest" else validate(args))
        result.setdefault("status", "pass")
        code = 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        result = {"status": "fail", "detail": str(exc)}
        code = 1
    (args.out / "pipeline.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
