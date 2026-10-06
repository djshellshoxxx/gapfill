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
PRESET_PARAM_RE = re.compile(r'\{\s*"([A-Za-z][A-Za-z0-9]*)"\s*,\s*(?!")')


def extract_parameter_ids(text: str) -> list[str]:
    return PARAM_RE.findall(text)


def extract_preset_parameter_ids(text: str) -> list[str]:
    return PRESET_PARAM_RE.findall(text)


def duplicates(values: list[str]) -> list[str]:
    counts = Counter(values)
    return sorted(value for value, count in counts.items() if count > 1)


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
    ci = _read(root, ".github/workflows/ci.yml", errors)

    parameter_ids = extract_parameter_ids(params)

    if not parameter_ids:
        errors.append("no stable parameter IDs found in GAPFILL_PARAMS")
    else:
        for duplicate in duplicates(parameter_ids):
            errors.append(f"duplicate stable parameter ID: {duplicate}")

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
        "GapFill_VST3",
        "GapFill_CLAP",
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

    required_ci_markers = [
        "python tools/validate_repo.py",
        "GapFillTests",
        "GapFill_VST3",
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
    print(f" - {len(extract_parameter_ids((root / 'Source/Parameters.h').read_text(encoding='utf-8')))} stable parameter IDs")
    print(" - factory preset parameter references resolve")
    print(" - required specs/docs are present")
    print(" - build and CI target markers are present")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
