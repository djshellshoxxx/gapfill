#!/usr/bin/env python3
"""Inventory C++ string literals and maintain English/pseudo catalogs.

Never rewrites C++. Protocol/identity literals and runtime sentence fragments
are reported separately. Full sentences are keyed by stable SHA-256 prefixes;
new translations remain data-only. Review INVENTORY.json's pending sinks before
claiming language coverage. --check verifies the generated catalogs are current.
"""
from __future__ import annotations

import argparse
import ast
import hashlib
import json
import math
from pathlib import Path
import re

REPO = Path(__file__).resolve().parents[3]
LANG = REPO / "Resources/lang"
ROOT = Path(__file__).resolve().parent
TOKEN = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\((?P<raw>[\s\S]*?)\)(?P=delimiter)"|(?:u8|u|U|L)?"(?:\\.|[^"\\])*"')
PROTECTED = {"GapFill", "Circuit Drift Labs", "VST3", "CLAP", "AU", "AAX", "MIDI", "PPQ", "BPM",
             "Hz", "kHz", "ms", "dB", "UTF-8", "ASCII", "JSON", "XML", "true", "false", "yes", "no", "en", "pseudo"}
ACCENTS = str.maketrans("AaEeIiOoUuCcNn", "ÁáËëÏïÖöÜüÇçÑñ")


def literals(source: str):
    previous = None
    for match in TOKEN.finditer(source):
        token = match[0]
        if token.startswith(("//", "/*")):
            continue
        try:
            value = match["raw"] if match["delimiter"] is not None else ast.literal_eval(re.sub(r'^(?:u8|u|U|L)', "", token))
        except (SyntaxError, ValueError):
            continue
        if previous and not re.sub(r'//[^\n]*|/\*[\s\S]*?\*/', "", source[previous[2]:match.start()]).strip():
            previous = (previous[0] + value, previous[1], match.end())
        else:
            if previous:
                yield previous
            previous = (value, match.start(), match.end())
    if previous:
        yield previous


def classify(value: str, source: str, start: int, end: int, path: Path, ids: set[str]):
    prefix = source[max(0, start - 180):start]
    suffix = source[end:end + 80]
    if re.search(r"(?:i18n::)?(?:text|format)\s*\(\s*$", prefix) and re.search(r"[A-Za-z]", value):
        return "explicit-translation"
    if (not re.search(r"[A-Za-z]", value) or value in PROTECTED or value in ids or len(value) > 8192
            or "\0" in value or re.search(r"#[ \t]*include[^\n]*$", prefix)
            or value.startswith(("http:", "https:", "com.", "JUCE_", "GAPFILL_", "GF_"))
            or value.startswith(("{\"", "[{", "<?xml"))
            or re.fullmatch(r"[A-Za-z][A-Za-z0-9]*(?:[._][A-Za-z0-9]+)+", value)
            or re.search(r"(?:setProperty|getProperty|getChildWithName|hasProperty|hasType|Identifier)\s*\(\s*$", prefix)
            or re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", value) and value[0].islower()
            or re.search(r"\.(?:h|cpp|json|xml|gapfill|gfrecipe|gapbank|wav|png|zip)$", value)):
        return "protected-or-nonprose"
    if value != value.strip() or re.match(r"\s*\+", suffix) or re.search(r"\+\s*$", prefix):
        return "pending-dynamic-template"
    if re.search(r"(?:setText|setButtonText|setTitle|setDescription|setTooltip|addItem)\s*\([^;]*$", prefix):
        if re.search(r"(?:menu|Menu)\.[^;]*$", prefix):
            return "pending-menu-adapter"
        return "component-adapter"
    if re.search(r"(?:\.body|\.title|\.entry|HelpRegistrar)\s*\([^;]*$", prefix) or path.name == "HelpSections.cpp":
        return "help-writer-adapter"
    if re.search(r"(?:drawText|drawFittedText)\s*\([^;]*$", prefix):
        return "pending-custom-drawing"
    if "Info" in path.stem or "ParamInfo" in source[max(0, start - 600):start]:
        return "parameter-presentation-adapter"
    if re.search(r"(?:TextButton|ToggleButton|Label|ComboBox)\s+\w+\s*\{\s*$", prefix):
        return "component-adapter"
    return "pending-sink-review"


def pseudo(value: str) -> str:
    # Numbered placeholders are copied exactly. Their argument data is never
    # accented, padded or interpreted by the runtime formatter.
    parts = re.split(r"(\{\d+\}|\{\{|\}\})", value)
    accented = "".join(part if re.fullmatch(r"\{\d+\}|\{\{|\}\}", part) else part.translate(ACCENTS) for part in parts)
    target = max(len(value) + 2, math.ceil(len(value) * 1.4))
    return "[" + accented + "~" * max(0, target - len(accented) - 2) + "]"


def build():
    english = {key: value for key, value in json.loads((LANG / "en.json").read_text()).items()
               if not key.startswith("s.") and not key.startswith("example.")}
    identifiers = set()
    for path in (REPO / "tools/param_ids").glob("*.lock"):
        identifiers.update(re.findall(r"\b[a-z][A-Za-z0-9]*\b", path.read_text()))
    by_value = {value: key for key, value in english.items() if not key.startswith("locale.")}
    records = []
    for path in sorted((REPO / "Source").rglob("*")):
        if path.suffix not in (".h", ".cpp"):
            continue
        source = path.read_text(encoding="utf-8")
        for value, start, end in literals(source):
            status = classify(value, source, start, end, path, identifiers)
            record = {"file": path.relative_to(REPO).as_posix(), "line": source.count("\n", 0, start) + 1,
                      "english": value, "wiring": status, "key": None}
            if status not in ("protected-or-nonprose", "pending-dynamic-template"):
                key = by_value.get(value, "s." + hashlib.sha256(value.encode("utf-8")).hexdigest()[:12])
                if key in english and english[key] != value:
                    raise ValueError("catalog key collision")
                english[key] = value
                by_value[value] = key
                record["key"] = key
            records.append(record)
    localized = {key: pseudo(value) for key, value in english.items()}
    localized["locale.nativeName"] = "Pseudo (+40%)"
    for code, catalog in (("en", english), ("pseudo", localized)):
        encoded = (json.dumps(catalog, ensure_ascii=False, indent=0, separators=(",", ":"), sort_keys=True) + "\n").encode("utf-8")
        if len(encoded) > 256 * 1024:
            raise ValueError(f"{code} catalog exceeds 256 KiB: {len(encoded)} bytes; review protected/fragments first")
    return english, localized, records


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    english, localized, records = build()
    outputs = {LANG / "en.json": english, LANG / "pseudo.json": localized, ROOT / "INVENTORY.json": records}
    failed = []
    for path, value in outputs.items():
        encoded = json.dumps(value, ensure_ascii=False, indent=0 if path.parent == LANG else 2,
                             separators=(",", ":") if path.parent == LANG else None, sort_keys=True) + "\n"
        if args.check:
            if not path.is_file() or path.read_text(encoding="utf-8") != encoded:
                failed.append(path.relative_to(REPO).as_posix())
        else:
            path.write_text(encoded, encoding="utf-8")
    counts = {}
    for record in records:
        counts[record["wiring"]] = counts.get(record["wiring"], 0) + 1
    print(json.dumps({"catalog_keys": len(english), "inventory": counts, "stale": failed}, sort_keys=True))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
