#!/usr/bin/env python3
"""Sign prepared candidate files using ALREADY provisioned identities.

Does not import certificates, acquire credentials, publish or tag. Invoke on
staged copies before packaging; notarize signed archives, staple the app/pkg,
repackage, then generate and sign the final checksum manifest. Missing identity
or native tools returns 2 with an explicit unverified report, never success.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile
import time


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def command(args: list[str], row: dict) -> str:
    started = time.monotonic()
    result = subprocess.run(args, capture_output=True, text=True, timeout=1800)
    # Identity selectors are public certificate/key identifiers; no private key,
    # password, API token or credential contents are accepted on this CLI.
    row["commands"].append({"command": args, "exit_code": result.returncode,
                            "seconds": time.monotonic() - started, "stdout": result.stdout, "stderr": result.stderr})
    if result.returncode:
        raise RuntimeError(f"{Path(args[0]).name} failed (exit {result.returncode})")
    return result.stdout


def require_identity(row: dict, variable: str, system: str | None, tools: tuple[str, ...]) -> str | None:
    identity = os.environ.get(variable)
    if not identity:
        row.update(status="unverified", detail=f"provisioned identity selector {variable} is missing")
    elif system and platform.system() != system:
        row.update(status="unverified", detail=f"signing requires {system}")
    elif any(shutil.which(tool) is None for tool in tools):
        row.update(status="unverified", detail="required native signing tools are unavailable")
    else:
        return identity
    return None


def execute(args, row: dict) -> dict:
    if args.action == "checksums":
        if not args.directory.is_dir():
            raise ValueError("candidate asset directory does not exist")
        manifest = args.directory / "SHA256SUMS.txt"
        signature = args.directory / "SHA256SUMS.txt.asc"
        key = require_identity(row, "GAPFILL_RELEASE_GPG_KEY", None, ("gpg",))
        if key:
            public = command(["gpg", "--batch", "--no-tty", "--armor", "--export", key], row)
            if "BEGIN PGP PUBLIC KEY BLOCK" not in public:
                raise ValueError("provisioned release key has no exportable public key")
            (args.directory / "release-signing-key.asc").write_text(public, encoding="utf-8")
        assets = sorted(p for p in args.directory.iterdir() if p.is_file()
                        and p.name not in (manifest.name, signature.name) and p.resolve() != args.out.resolve())
        if not assets or any(p.is_symlink() or "\n" in p.name or "\r" in p.name or "\\" in p.name or ":" in p.name for p in assets):
            raise ValueError("candidate assets must be regular files with safe names")
        manifest.write_text("".join(sha256(p) + "  " + p.name + "\n" for p in assets), encoding="utf-8")
        row.update(manifest=str(manifest), manifest_sha256=sha256(manifest), assets=len(assets))
        if key:
            command(["gpg", "--batch", "--no-tty", "--yes", "--armor", "--local-user", key,
                     "--detach-sign", "--output", str(signature), str(manifest)], row)
            command(["gpg", "--batch", "--no-tty", "--verify", str(signature), str(manifest)], row)
            row.update(status="pass", detail="final checksums and detached signature generated and verified",
                       signature=str(signature), trusted_fingerprint=key)
        return row

    variable, system, tools = {
        "windows": ("GAPFILL_WINDOWS_CERT_SHA1", "Windows", ("signtool",)),
        "macos": ("GAPFILL_APPLE_SIGN_IDENTITY", "Darwin", ("codesign",)),
        "notarize": ("GAPFILL_NOTARY_PROFILE", "Darwin", ("xcrun",)),
        "macos-installer": ("GAPFILL_APPLE_INSTALLER_IDENTITY", "Darwin", ("pkgbuild", "pkgutil", "codesign")),
    }[args.action]
    identity = require_identity(row, variable, system, tools)
    if identity is None:
        return row
    if any(not path.exists() or path.is_symlink() for path in args.input):
        raise ValueError("signing inputs must be existing staged files/bundles, not symlinks")
    if args.action == "macos-installer":
        if len(args.input) != 1 or not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
            raise ValueError("installer requires one staging directory and a numeric x.y.z version")
        stage = args.input[0]
        args.package.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="gapfill-pkg-") as temp:
            payload = Path(temp) / "payload"
            for source, target in (("VST3/GapFill.vst3", "Library/Audio/Plug-Ins/VST3/GapFill.vst3"),
                                   ("AU/GapFill.component", "Library/Audio/Plug-Ins/Components/GapFill.component"),
                                   ("CLAP/GapFill.clap", "Library/Audio/Plug-Ins/CLAP/GapFill.clap"),
                                   ("Standalone/GapFill.app", "Applications/GapFill.app")):
                bundle = stage / source
                command(["codesign", "--verify", "--deep", "--strict", str(bundle)], row)
                details = subprocess.run(["codesign", "-dv", "--verbose=4", str(bundle)], capture_output=True, text=True)
                if details.returncode or "Authority=Developer ID Application:" not in details.stderr:
                    raise ValueError("installer payload must already have Developer ID Application signatures")
                destination = payload / target
                destination.parent.mkdir(parents=True, exist_ok=True)
                if bundle.is_dir():
                    shutil.copytree(bundle, destination, symlinks=True)
                else:
                    shutil.copy2(bundle, destination)
            command(["pkgbuild", "--root", str(payload), "--identifier", "com.gapfillaudio.gapfill.installer",
                     "--version", args.version, "--install-location", "/", "--sign", identity, str(args.package)], row)
        signature = command(["pkgutil", "--check-signature", str(args.package)], row)
        if "Developer ID Installer:" not in signature:
            raise ValueError("installer lacks a Developer ID Installer signature")
        row.update(status="pass", detail="signed system installer built; notarization, install/upgrade and host acceptance remain separate",
                   package=str(args.package), package_sha256=sha256(args.package))
        return row
    for path in args.input:
        if args.action == "windows":
            command(["signtool", "sign", "/fd", "SHA256", "/td", "SHA256", "/tr", args.timestamp_url,
                     "/sha1", identity, str(path)], row)
            command(["signtool", "verify", "/pa", "/all", "/v", str(path)], row)
        elif args.action == "macos":
            nested = sorted([p for p in path.rglob("*") if p.suffix in (".dylib", ".framework")],
                            key=lambda p: len(p.parts), reverse=True) if path.is_dir() else []
            for item in [*nested, path]:
                command(["codesign", "--force", "--options", "runtime", "--timestamp", "--sign", identity, str(item)], row)
            command(["codesign", "--verify", "--deep", "--strict", "--verbose=2", str(path)], row)
            details = subprocess.run(["codesign", "-dv", "--verbose=4", str(path)], capture_output=True, text=True)
            if details.returncode or "Authority=Developer ID Application:" not in details.stderr:
                raise ValueError("signing identity is not a Developer ID Application certificate")
        else:
            output = command(["xcrun", "notarytool", "submit", str(path), "--keychain-profile", identity,
                              "--wait", "--output-format", "json"], row)
            result = json.loads(output)
            if result.get("status") != "Accepted":
                raise ValueError("Apple notarization was not accepted")
            row.setdefault("notarizations", []).append(result)
    if args.action == "notarize":
        for path in args.staple:
            if path.suffix not in (".app", ".pkg", ".dmg") or not path.exists():
                raise ValueError("staple only an existing app/pkg/dmg from the submitted signed archive")
            command(["xcrun", "stapler", "staple", str(path)], row)
            command(["xcrun", "stapler", "validate", str(path)], row)
    row.update(status="pass", detail="requested candidate signing operation verified; publication and host acceptance separate")
    return row


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    for name in ("windows", "macos", "macos-installer", "notarize", "checksums"):
        child = sub.add_parser(name)
        child.add_argument("--out", required=True, type=Path)
        if name == "checksums":
            child.add_argument("--directory", required=True, type=Path)
        else:
            child.add_argument("--input", required=True, action="append", type=Path)
        if name == "windows":
            child.add_argument("--timestamp-url", default="https://timestamp.digicert.com")
        if name == "notarize":
            child.add_argument("--staple", action="append", default=[], type=Path)
        if name == "macos-installer":
            child.add_argument("--package", required=True, type=Path)
            child.add_argument("--version", required=True)
    args = parser.parse_args(argv)
    row = {"schema": 1, "kind": "candidate-signing", "action": args.action, "status": "unverified", "commands": []}
    try:
        row = execute(args, row)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        row.update(status="fail", detail=str(exc))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(row, indent=2) + "\n", encoding="utf-8")
    print(f"Candidate {args.action}: {row['status']}; {row['detail']}")
    return {"pass": 0, "fail": 1, "unverified": 2}[row["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
