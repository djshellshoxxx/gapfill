"""Observe checkout, CMake configuration and executable identity without guessing missing build data.

CI must build from an immutable checkout: observing a source SHA and binary hash alone cannot prove that the binary
was compiled from that checkout. The evidence records that association's limits explicitly.
"""

from __future__ import annotations

import platform
import re
import shutil
import subprocess
from pathlib import Path

from evidence import file_hash


def git_text(repo: Path, *args: str) -> str | None:
    try:
        result = subprocess.run(["git", *args], cwd=repo, capture_output=True, text=True, timeout=15)
        return result.stdout.strip() if result.returncode == 0 else None
    except (OSError, subprocess.TimeoutExpired):
        return None


def executable_path(command: str, repo: Path) -> Path | None:
    candidate = Path(command)
    if not candidate.is_absolute():
        candidate = repo / candidate
    if candidate.is_file():
        return candidate.resolve()
    found = shutil.which(command)
    return Path(found).resolve() if found else None


def build_metadata(build_dir: Path | None, requested_config: str | None) -> tuple[dict, str]:
    compiler = {"id": "unverified", "version": "unverified", "path": "unverified"}
    configuration = requested_config or "unverified"
    if build_dir is None:
        return compiler, configuration
    cache_file = build_dir / "CMakeCache.txt"
    if not cache_file.is_file():
        return compiler, configuration
    cache = cache_file.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"^CMAKE_BUILD_TYPE:[^=]+=(.*)$", cache, re.M)
    if match and match[1].strip():
        observed = match[1].strip()
        if requested_config and requested_config != observed:
            raise ValueError(f"--build-config {requested_config} conflicts with CMake build type {observed}")
        configuration = observed
    match = re.search(r"^CMAKE_CXX_COMPILER:[^=]+=(.*)$", cache, re.M)
    if match:
        compiler["path"] = match[1].strip()
    metadata = sorted((build_dir / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
    # Several CMake versions may leave stale compiler records; ambiguous metadata remains unverified.
    identities = set()
    for path in metadata:
        text = path.read_text(encoding="utf-8", errors="replace")
        identity = []
        for key in ("ID", "VERSION"):
            match = re.search(r'set\(CMAKE_CXX_COMPILER_' + key + r' "([^"\n]+)"\)', text)
            identity.append(match[1] if match else "unverified")
        identities.add(tuple(identity))
    if len(identities) == 1:
        compiler["id"], compiler["version"] = identities.pop()
    return compiler, configuration


def collect_provenance(repo: Path, binary: list[str], *, build_dir: Path | None = None,
                       build_config: str | None = None, source_head: str | None = None,
                       output_dir: Path | None = None) -> dict:
    tested_sha = git_text(repo, "rev-parse", "HEAD") or "unverified"
    if source_head and not re.fullmatch(r"[0-9a-f]{40}", source_head):
        raise ValueError("--source-head must be an exact 40-character Git commit SHA")
    status = git_text(repo, "status", "--porcelain", "--untracked-files=all")
    # Generated, untracked QA outputs are allowed; tracked changes always invalidate a clean candidate.
    lines = []
    for line in (status or "").splitlines():
        generated = False
        if line.startswith("?? ") and output_dir and output_dir.resolve() != repo.resolve():
            try:
                (repo / line[3:]).resolve().relative_to(output_dir.resolve())
                generated = True
            except ValueError:
                pass
        if not generated:
            lines.append(line)
    compiler, configuration = build_metadata(build_dir, build_config)
    executable = executable_path(binary[0], repo) if binary else None
    dependencies, dependency_dirty = {}, {}
    for name in ("JUCE", "clap-juce-extensions"):
        path = repo / name
        dependencies[name] = git_text(path, "rev-parse", "HEAD") if (path / ".git").exists() else "unverified"
        dirty = git_text(path, "status", "--porcelain", "--untracked-files=all") if (path / ".git").exists() else None
        dependency_dirty[name] = bool(dirty) if dirty is not None else None
    return {"tested_sha": tested_sha, "source_head": source_head or tested_sha,
            "source_head_origin": "explicit" if source_head else "checkout (PR head not separately supplied)",
            "dirty": bool(lines) if status is not None else None, "architecture": platform.machine(),
            "build_configuration": configuration, "compiler": compiler, "dependencies": dependencies,
            "dependency_dirty": dependency_dirty,
            "binary_path": str(executable) if executable else "unverified", "binary_argv": binary,
            "binary_sha256": file_hash(executable) if executable else "unverified",
            "build_source_association": "observed checkout/configuration/binary; build association requires immutable CI inputs"}


def tool_version(command: list[str] | None) -> dict:
    if not command:
        return {"status": "unverified", "text": "no supported version query supplied"}
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=10, errors="replace")
        text = ((result.stdout or "") + (result.stderr or "")).strip()[:4096]
        return {"status": "observed" if result.returncode == 0 and text else "unverified",
                "command": command, "exit_code": result.returncode, "text": text}
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {"status": "unverified", "command": command, "text": type(exc).__name__}
