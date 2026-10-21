#!/usr/bin/env python3
"""Fast repository-level validation for GapFill.

This intentionally has no third-party dependencies. It catches specification
and wiring mistakes before a JUCE checkout or compiler is needed.
"""

from __future__ import annotations

import re
import sys
from collections import Counter
from pathlib import Path


PARAM_RE = re.compile(r'X\s*\([^,]+,\s*"([^"]+)"\s*\)')
PARAM_ID_RE = re.compile(r"^[a-z][A-Za-z0-9]*$")
GROUP_CHAIN_RE = re.compile(r"GAPFILL_PARAMS_([A-Z0-9_]+)\(X\)")
GROUP_ORDER_FILE = "GROUP_ORDER"

PRESET_PARAM_RE = re.compile(r'\{\s*"([A-Za-z][A-Za-z0-9]*)"\s*,\s*([^"\s])')


def extract_parameter_ids(text: str) -> list[str]:
    return PARAM_RE.findall(text)


def extract_preset_parameter_ids(text: str) -> list[str]:
    return [parameter_id for parameter_id, _ in PRESET_PARAM_RE.findall(text)]


def duplicates(values: list[str]) -> list[str]:
    counts = Counter(values)
    return sorted(value for value, count in counts.items() if count > 1)


def read_lock(text: str) -> list[str]:
    """IDs of a tools/param_ids/<group>.lock file: one per line, '#' starts a comment."""
    ids = []
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            ids.append(line)
    return ids


def group_order_from_header(params_header: str) -> list[str]:
    """Group macro suffixes in the order GAPFILL_PARAMS chains them (CORE first)."""
    marker = "#define GAPFILL_PARAMS(X)"
    start = params_header.find(marker)
    if start < 0:
        return []
    block_lines = []
    for line in params_header[start:].splitlines():
        block_lines.append(line)
        if not line.rstrip().endswith("\\"):
            break
    return GROUP_CHAIN_RE.findall("\n".join(block_lines))


def check_lock(group: str, header_ids: list[str], lock_ids: list[str] | None) -> list[str]:
    """Append-only lock rules for one parameter group."""
    errors: list[str] = []

    if lock_ids is None:
        if header_ids:
            errors.append(f"parameter group '{group}' has IDs but no tools/param_ids/{group}.lock")
        return errors

    for duplicate in duplicates(lock_ids):
        errors.append(f"param lock '{group}' lists ID twice: {duplicate}")

    header_set, lock_set = set(header_ids), set(lock_ids)

    for missing in lock_ids:
        if missing not in header_set:
            errors.append(f"locked parameter ID missing or renamed in group '{group}': {missing}")

    for new in header_ids:
        if new not in lock_set:
            errors.append(f"new parameter ID '{new}' in group '{group}' is not in tools/param_ids/{group}.lock "
                          "(adding a permanent ID is a deliberate act: add the line)")

    if not errors:
        if [i for i in header_ids if i in lock_set] != [i for i in lock_ids if i in header_set]:
            errors.append(f"parameter IDs of group '{group}' were reordered relative to the lock file")

    return errors


def _read(root: Path, relative: str, errors: list[str]) -> str:
    path = root / relative

    if not path.is_file():
        errors.append(f"missing required file: {relative}")
        return ""

    try:
        return path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        errors.append(f"required text file is not UTF-8: {relative}")
        return ""


def validate(root: Path) -> list[str]:
    errors: list[str] = []

    params = _read(root, "Source/Parameters.h", errors)
    presets = _read(root, "Source/Presets.cpp", errors)
    cmake = _read(root, "CMakeLists.txt", errors)
    readme = _read(root, "README.md", errors)
    baseline = _read(root, "docs/standards/CDL_PLUGIN_BASELINE.md", errors)
    compliance = _read(root, "docs/COMPLIANCE.md", errors)
    validation = _read(root, "docs/VALIDATION.md", errors)
    ci = _read(root, ".github/workflows/ci.yml", errors)

    # Parameter IDs live in Source/Parameters.h (core group) and in one header
    # per feature group, Source/params/<group>.h.
    groups: dict[str, list[str]] = {"core": extract_parameter_ids(params)}
    params_dir = root / "Source" / "params"
    group_headers: dict[str, str] = {}
    if params_dir.is_dir():
        for header in sorted(params_dir.glob("*.h")):
            text = header.read_text(encoding="utf-8")
            if re.search(r"#define\s+GAPFILL_PARAMS_" + re.escape(header.stem.upper()) + r"\b", text):
                groups[header.stem] = extract_parameter_ids(text)
                group_headers[header.stem] = text

    parameter_ids = [i for ids in groups.values() for i in ids]

    if not groups["core"]:
        errors.append("no stable parameter IDs found in GAPFILL_PARAMS")

    for duplicate in duplicates(parameter_ids):
        errors.append(f"duplicate stable parameter ID: {duplicate}")

    for parameter_id in parameter_ids:
        if not PARAM_ID_RE.match(parameter_id):
            errors.append(f"parameter ID does not match ^[a-z][A-Za-z0-9]*$: {parameter_id}")

    lock_dir = root / "tools" / "param_ids"
    for group, ids in groups.items():
        lock_path = lock_dir / f"{group}.lock"
        lock_ids = read_lock(lock_path.read_text(encoding="utf-8")) if lock_path.is_file() else None
        if group == "core" and lock_ids is None and not lock_dir.is_dir():
            errors.append("missing tools/param_ids/core.lock (append-only parameter ID lock)")
            continue
        errors.extend(check_lock(group, ids, lock_ids))

    # Every group header must be chained into GAPFILL_PARAMS, and the chain order
    # (which fixes parameter index order) is append-only.
    chain = group_order_from_header(params)
    for group in group_headers:
        if group.upper() not in chain:
            errors.append(f"Source/params/{group}.h is not chained into GAPFILL_PARAMS in Source/Parameters.h")
    order_path = lock_dir / GROUP_ORDER_FILE
    if order_path.is_file():
        locked = read_lock(order_path.read_text(encoding="utf-8"))
        if chain[: len(locked)] != locked:
            errors.append("GAPFILL_PARAMS group order changed (it is permanent and append-only): "
                          f"expected prefix {locked}, found {chain[: len(locked)]}")

    known = set(parameter_ids)
    for preset_id in sorted(set(extract_preset_parameter_ids(presets))):
        if preset_id not in known:
            errors.append(f"factory preset references unknown parameter ID: {preset_id}")

    required_cmake_markers = [
        "project(GapFill VERSION",
        "FORMATS",
        "VST3",
        "Standalone",
        "GapFillTests",
        "juce_add_plugin(GapFill",
        "clap_juce_extensions_plugin",
        "EDITOR_WANTS_KEYBOARD_FOCUS TRUE",
    ]
    for marker in required_cmake_markers:
        if marker not in cmake:
            errors.append(f"CMake is missing expected target/metadata marker: {marker}")

    if "docs/standards/CDL_PLUGIN_BASELINE.md" not in readme:
        errors.append("README does not link the required CDL plugin baseline")

    if baseline and "MUST" not in baseline:
        errors.append("CDL baseline file does not look like the required specification")

    if compliance and "Profiles" not in compliance:
        errors.append("docs/COMPLIANCE.md does not declare applicable profiles")

    for marker in ["pluginval", "clap-validator", "Standalone", "MIDI Learn", "Keyboard navigation"]:
        if validation and marker not in validation:
            errors.append(f"docs/VALIDATION.md is missing validation area: {marker}")

    required_ci_markers = [
        "python tools/validate_repo.py",
        "GapFillTests",
        "GapFill_VST3",
        "GapFill_CLAP",
        "GapFill_Standalone",
        "clap-validator validate",
        "--strictness-level 10",
        "Smoke-launch standalone",
    ]
    for marker in required_ci_markers:
        if marker not in ci:
            errors.append(f"CI is missing required validation marker: {marker}")

    return errors


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    errors = validate(root)

    if errors:
        print("GapFill preflight: FAILED")
        for error in errors:
            print(f" - {error}")
        return 1

    print("GapFill preflight: OK")
    total = len(extract_parameter_ids((root / "Source/Parameters.h").read_text(encoding="utf-8")))
    for header in sorted((root / "Source" / "params").glob("*.h")):
        total += len(extract_parameter_ids(header.read_text(encoding="utf-8")))
    print(f" - {total} stable parameter IDs (locked per group in tools/param_ids)")
    print(" - factory preset parameter references resolve")
    print(" - required specs/docs are present")
    print(" - build and CI target markers are present")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
