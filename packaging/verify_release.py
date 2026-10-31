#!/usr/bin/env python3
"""Verify downloaded checksums, safely unpack, then validate the packaged binaries.

Run once per ZIP, specifying its actual formats. Native validators must run on
the target OS. Signing and manual installer/DAW acceptance are separate rows;
--require-signing additionally requires trusted checksum and code signatures.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import platform
import plistlib
import re
import shutil
import stat
import subprocess
import time
import unicodedata
import zipfile


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def checksums(manifest: Path) -> dict[str, str]:
    if manifest.stat().st_size > 1024 * 1024:
        raise ValueError("checksum manifest exceeds 1 MiB")
    root = manifest.resolve().parent
    entries = {}
    for line in manifest.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"([a-fA-F0-9]{64}) [ *]([^/\\:\x00]+)", line)
        if not match or match[2] in (".", "..") or match[2] in entries:
            raise ValueError("malformed, duplicate or unsafe checksum entry")
        expected, name = match.groups()
        asset = root / name
        if asset.is_symlink() or not asset.is_file() or digest(asset).lower() != expected.lower():
            raise ValueError(f"missing or mismatched downloaded asset: {name}")
        entries[name] = expected.lower()
    if not entries:
        raise ValueError("empty checksum manifest")
    return entries


def extract(archive: Path, out: Path) -> None:
    if out.exists() and any(out.iterdir()):
        raise ValueError("extraction directory must be empty")
    out.mkdir(parents=True, exist_ok=True)
    root = out.resolve()
    with zipfile.ZipFile(archive) as zipped:
        if len(zipped.infolist()) > 20000 or sum(member.file_size for member in zipped.infolist()) > 2 * 1024**3:
            raise ValueError("package exceeds bounded extraction limits")
        seen = set()
        for member in zipped.infolist():
            name = PurePosixPath(member.filename)
            destination = (root / member.filename).resolve()
            canonical = unicodedata.normalize("NFC", destination.relative_to(root).as_posix()).casefold() if destination.is_relative_to(root) else None
            mode = member.external_attr >> 16
            reserved = re.compile(r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", re.IGNORECASE)
            if (name.is_absolute() or ".." in name.parts or "\\" in member.filename or ":" in member.filename
                    or any(part.endswith((".", " ")) or reserved.fullmatch(part) or re.search(r'[<>"|?*]', part) for part in name.parts)
                    or not destination.is_relative_to(root) or stat.S_ISLNK(mode)
                    or canonical in seen):
                raise ValueError("package contains unsafe paths, symlinks or duplicate names")
            seen.add(canonical)
        # Validate the WHOLE archive before writing the first entry.
        zipped.extractall(root)
        for member in zipped.infolist():
            if member.external_attr >> 16 & 0o111 and not member.is_dir():
                destination = root / member.filename
                destination.chmod(destination.stat().st_mode | 0o111)


def unique(paths, description: str) -> Path:
    values = list(paths)
    if len(values) != 1:
        raise ValueError(f"expected one {description}, found {len(values)}")
    return values[0]


def execute(command: list[str], out: Path, name: str, timeout: float = 900) -> dict:
    log = out / (name + ".log")
    started = time.monotonic()
    if not Path(command[0]).is_file() and shutil.which(command[0]) is None:
        log.write_text("Native tool is unavailable: " + command[0] + "\n", encoding="utf-8")
        return {"command": command, "status": "unverified", "exit_code": None,
                "detail": "native validation tool is unavailable", "log": str(log), "log_sha256": digest(log)}
    with log.open("w", encoding="utf-8") as stream:
        try:
            result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
            code = result.returncode
        except subprocess.TimeoutExpired:
            stream.write("TIMEOUT\n")
            code = 124
        except OSError as exc:
            stream.write(str(exc) + "\n")
            code = 127
    return {"command": command, "exit_code": code, "seconds": time.monotonic() - started,
            "status": "pass" if code == 0 else "fail", "log": str(log), "log_sha256": digest(log)}


def architecture(binary: Path, target: str, arch: str) -> None:
    with binary.open("rb") as stream:
        header = stream.read(64)
        if target == "linux":
            if header[:6] != b"\x7fELF\x02\x01" or int.from_bytes(header[18:20], "little") != 62:
                raise ValueError(f"not a Linux x86_64 ELF: {binary.name}")
        elif target == "windows":
            if len(header) < 64 or header[:2] != b"MZ":
                raise ValueError("missing PE header")
            offset = int.from_bytes(header[60:64], "little")
            if offset > 8 * 1024**2:
                raise ValueError("invalid PE header offset")
            stream.seek(offset)
            if stream.read(6) != b"PE\0\0\x64\x86":
                raise ValueError("not a Windows x86_64 PE")
        elif subprocess.run(["lipo", str(binary), "-verify_arch", arch], capture_output=True).returncode:
            raise ValueError("macOS binary architecture mismatch")


def validate(args) -> dict:
    args.out.mkdir(parents=True, exist_ok=True)
    row = {"schema": 1, "kind": "downloaded-package-validation", "archive": str(args.archive),
           "archive_sha256": digest(args.archive), "platform": args.platform, "architecture": args.arch,
           "manual_open": ["installer upgrade/uninstall", "DAW acceptance", "hardware", "listening"],
           "source_provenance": "unverified: retain the build/release attestation separately", "checks": {}}
    entries = checksums(args.manifest)
    if args.archive.resolve().parent != args.manifest.resolve().parent or args.archive.name not in entries:
        raise ValueError("archive must be covered by its downloaded manifest")
    row["checks"]["downloaded-checksums"] = {"status": "pass", "manifest_sha256": digest(args.manifest)}
    signatures = []
    if args.require_signing:
        if not args.signature or not args.trusted_keyring or shutil.which("gpgv") is None:
            row.update(status="unverified", detail="trusted detached checksum signature/keyring not provided")
            return row
        signatures.append(execute(["gpgv", "--keyring", str(args.trusted_keyring), str(args.signature), str(args.manifest)], args.out, "checksum-signature"))
        row["checks"]["checksum-signature"] = signatures[-1]
        if signatures[-1]["status"] != "pass":
            row.update(status="fail", detail="checksum signature is not trusted")
            return row
    native = {"linux": "Linux", "windows": "Windows", "macos": "Darwin"}[args.platform]
    if platform.system() != native:
        row.update(status="unverified", detail="native validation requires " + native)
        return row
    if args.platform == "macos" and shutil.which("lipo") is None:
        row.update(status="unverified", detail="native architecture tool lipo is unavailable")
        return row
    unpacked = args.out / "unpacked"
    extract(args.archive, unpacked)
    licenses = list(unpacked.rglob("LICENSE.txt"))
    if not licenses or any(p.stat().st_size > 1024 * 1024 for p in licenses):
        raise ValueError("package is missing a bounded license notice")
    if not any("AGPL-3.0-or-later" in p.read_text(encoding="utf-8", errors="replace") for p in licenses):
        raise ValueError("packaged license notice does not identify the repository's AGPL-3.0-or-later grant")
    row["checks"]["license-notice"] = {"status": "pass", "sha256": [digest(p) for p in licenses]}
    formats = args.formats or (["vst3", "clap", "standalone", "au"] if args.platform == "macos" else ["vst3", "clap", "standalone"])
    artifacts = {}
    for format_name in formats:
        if format_name == "vst3":
            bundle = unique((p for p in unpacked.rglob("GapFill.vst3") if p.is_dir()), "VST3 bundle")
            binary = unique((p for p in bundle.rglob("*") if p.is_file() and p.suffix in (".so", ".vst3")
                             or p.is_file() and p.name == "GapFill" and p.parent.name == "MacOS"), "VST3 binary")
        elif format_name == "clap":
            bundle = unique(unpacked.rglob("GapFill.clap"), "CLAP artifact")
            binary = bundle if bundle.is_file() else bundle / "Contents/MacOS/GapFill"
        elif format_name == "au":
            bundle = unique(unpacked.rglob("GapFill.component"), "AU component")
            binary = bundle / "Contents/MacOS/GapFill"
        elif args.platform == "macos":
            bundle = unique(unpacked.rglob("GapFill.app"), "standalone app")
            binary = bundle / "Contents/MacOS/GapFill"
        else:
            bundle = binary = unique((p for p in unpacked.rglob("GapFill.exe" if args.platform == "windows" else "GapFill")
                                      if p.is_file()), "standalone executable")
        architecture(binary, args.platform, args.arch)
        binary.chmod(binary.stat().st_mode | 0o111)
        artifacts[format_name] = {"bundle": str(bundle), "binary": str(binary), "sha256": digest(binary)}
        if args.require_signing and args.platform == "windows":
            signatures.append(execute(["signtool", "verify", "/pa", "/all", "/v", str(binary)], args.out, format_name + "-signature"))
        elif args.require_signing and args.platform == "macos":
            signatures.append(execute(["codesign", "--verify", "--deep", "--strict", str(bundle)], args.out, format_name + "-signature"))
            identity = subprocess.run(["codesign", "-dv", "--verbose=4", str(bundle)], capture_output=True, text=True)
            if identity.returncode or "Authority=Developer ID Application:" not in identity.stderr:
                raise ValueError("macOS bundle lacks a Developer ID Application signature")
            if format_name == "standalone":
                signatures.append(execute(["xcrun", "stapler", "validate", str(bundle)], args.out, "notarization-ticket"))
                signatures.append(execute(["spctl", "--assess", "--type", "execute", "--verbose=4", str(bundle)], args.out, "gatekeeper"))
        if format_name in ("vst3", "clap"):
            tool = args.pluginval if format_name == "vst3" else args.clap_validator
            if not tool:
                row["checks"][format_name] = {"status": "unverified", "detail": "native validator not supplied"}
            else:
                command = [str(tool), "--strictness-level", "10", "--validate", str(bundle)] if format_name == "vst3" else [str(tool), "validate", str(bundle)]
                row["checks"][format_name] = execute(command, args.out, format_name)
        elif format_name == "au":
            # auval scans installed components. Preserve an existing user's install.
            destination = Path.home() / "Library/Audio/Plug-Ins/Components/GapFill.component"
            if destination.exists() or destination.is_symlink():
                row["checks"]["au"] = {"status": "unverified", "detail": "existing AU preserved; use clean validation account"}
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                try:
                    shutil.copytree(bundle, destination)
                    with (bundle / "Contents/Info.plist").open("rb") as stream:
                        component = plistlib.load(stream)["AudioComponents"][0]
                    row["checks"]["au"] = execute(["auval", "-v", component["type"], component["subtype"], component["manufacturer"]], args.out, "au")
                finally:
                    if destination.exists():
                        shutil.rmtree(destination)
        else:
            log = args.out / "standalone.log"
            with log.open("w") as stream:
                process = subprocess.Popen([str(binary)], stdout=stream, stderr=subprocess.STDOUT)
                try:
                    process.wait(timeout=5)
                    row["checks"]["standalone"] = {"status": "fail", "detail": "exited before 5-second smoke window", "exit_code": process.returncode}
                except subprocess.TimeoutExpired:
                    row["checks"]["standalone"] = {"status": "pass", "detail": "alive for 5 seconds; audio device/host use unverified"}
                finally:
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
    row["artifacts"] = artifacts
    row["checks"]["code-signatures"] = ({"status": "unverified", "applicable": False,
                                         "detail": "Linux authenticity is covered by the separate detached manifest signature"}
                                        if args.platform == "linux" else
                                        {"status": "pass" if signatures and all(r["status"] == "pass" for r in signatures) else "unverified"})
    if any(r["status"] == "fail" for r in [*row["checks"].values(), *signatures]):
        row["status"] = "fail"
    elif any(row["checks"][name]["status"] == "unverified" for name in formats) or (args.require_signing and not all(r["status"] == "pass" for r in signatures)):
        row["status"] = "unverified"
    else:
        row["status"] = "pass"
    row["release_acceptance"] = "incomplete: source attestation and manual acceptance remain separate"
    return row


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    check = sub.add_parser("checksums")
    check.add_argument("--manifest", required=True, type=Path)
    unpack = sub.add_parser("extract")
    unpack.add_argument("--archive", required=True, type=Path)
    unpack.add_argument("--out", required=True, type=Path)
    verify = sub.add_parser("validate")
    for flag in ("archive", "manifest", "out"):
        verify.add_argument("--" + flag, required=True, type=Path)
    verify.add_argument("--platform", choices=("linux", "windows", "macos"), required=True)
    verify.add_argument("--arch", choices=("x86_64", "arm64"), default="x86_64")
    verify.add_argument("--formats", nargs="+", choices=("vst3", "clap", "standalone", "au"))
    verify.add_argument("--pluginval", type=Path)
    verify.add_argument("--clap-validator", type=Path)
    verify.add_argument("--require-signing", action="store_true")
    verify.add_argument("--signature", type=Path)
    verify.add_argument("--trusted-keyring", type=Path)
    args = parser.parse_args(argv)
    if args.action == "validate" and args.platform != "macos" and args.arch != "x86_64":
        parser.error("the current Windows/Linux release targets are x86_64")
    try:
        if args.action == "checksums":
            print(f"Verified {len(checksums(args.manifest))} downloaded checksums; authenticity not implied.")
            return 0
        if args.action == "extract":
            extract(args.archive, args.out)
            return 0
        row = validate(args)
    except (OSError, ValueError, KeyError, IndexError, zipfile.BadZipFile, subprocess.SubprocessError) as exc:
        row = {"status": "fail", "detail": str(exc)}
        if args.action != "validate":
            print(row["detail"])
            return 1
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "package-validation.json").write_text(json.dumps(row, indent=2) + "\n")
    print("Package validation: " + row["status"])
    return {"pass": 0, "fail": 1, "unverified": 2}[row["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
