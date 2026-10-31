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
TOKEN = re.compile(r'''//[^\n]*|/\*[\s\S]*?\*/|(?P<character>(?<![\w])(?:u8|u|U|L)?'(?:\\.|[^'\\\r\n])*')|R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\((?P<raw>[\s\S]*?)\)(?P=delimiter)"|(?:u8|u|U|L)?"(?:\\.|[^"\\])*"''')
PROTECTED = {"GapFill", "Circuit Drift Labs", "VST3", "CLAP", "AU", "AAX", "MIDI", "PPQ", "BPM",
             "Hz", "kHz", "ms", "dB", "-inf", "+inf", "inf", "NaN", "nan",
             "UTF-8", "ASCII", "JSON", "XML", "true", "false", "yes", "no", "en", "pseudo"}
ACCENTS = str.maketrans("AaEeIiOoUuCcNn", "ÁáËëÏïÖöÜüÇçÑñ")
MAX_CATALOG_BYTES = 256 * 1024


def literals(source: str):
    previous = None
    for match in TOKEN.finditer(source):
        token = match[0]
        if token.startswith(("//", "/*")) or match["character"] is not None:
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
    if (re.search(r"(?:containsOnly|retainCharacters|removeCharacters|indexOfAnyOf|containsAnyOf)\s*\(\s*$", prefix)
            or re.search(r"(?:createNewChildElement|getChildByName|hasTagName|setAttribute|getStringAttribute|hasAttribute|getChildFile)\s*\(\s*$", prefix)
            or re.search(r"\bXmlElement\s+\w+\s*[{(]\s*$", prefix)
            or path.name == "MidiLearn.h" and re.search(r"\bstateType\s*=\s*$", prefix)):
        # Syntax alphabets and XML/state keys describe bytes/identities. The
        # same value still gets a key if it appears at a presentation sink.
        return "protected-or-nonprose"
    csv = value.split(",")
    if len(csv) > 1 and all(part in ids for part in csv):
        return "protected-or-nonprose"  # permanent parameter-ID scope lists, never presentation
    if (path.name in ("StateSubtrees.cpp", "SchedulerTypes.cpp")
            or path.name in ("Presets.cpp", "Suggest.cpp") and re.search(r"\{\s*$", prefix)
            or re.search(r"\bstatic_assert\s*\([^;]*,\s*$", prefix)):
        return "protected-or-nonprose"  # state/reason codes, factory identities and compiler diagnostics
    if path.name == "ControllerProfile.cpp":
        command_keys = source.find("kCommandKeys")
        if command_keys >= 0 and command_keys < start < source.find("};", command_keys):
            return "protected-or-nonprose"
    if (not re.search(r"[A-Za-z]", value) or value in PROTECTED or value in ids or len(value) > 8192
            or re.fullmatch(r"[A-Z]", value) or re.fullmatch(r"[0-9eE .,+/\-]+", value)
            or "\0" in value or re.search(r"#[ \t]*include[^\n]*$", prefix)
            or value.startswith(("http:", "https:", "com.", "JUCE_", "GAPFILL_", "GF_"))
            or value.startswith(("{\"", "[{", "<?xml"))
            or re.fullmatch(r"[A-Za-z][A-Za-z0-9]*(?:[._][A-Za-z0-9]+)+", value)
            or re.search(r"(?:setProperty|getProperty|getChildWithName|hasProperty|hasType|Identifier)\s*\(\s*$", prefix)
            or re.search(r"(?:Identifier|ValueTree)\s*(?:\w+\s*)?[{(]\s*$", prefix)
            or re.search(r"(?:juce::Thread|ThreadWithProgressWindow)\s*\(\s*$", prefix)
            or re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", value) and value[0].islower()
            or re.search(r"\.(?:h|cpp|json|xml|gapfill|gfrecipe|gapbank|wav|png|zip)$", value)):
        return "protected-or-nonprose"
    if value != value.strip() or re.match(r"\s*\+", suffix) or re.search(r"\+\s*$", prefix):
        return "pending-dynamic-template"
    if re.search(r"(?:setText|setButtonText|setTitle|setDescription|setTooltip|addItem)\s*\([^;]*$", prefix):
        if re.search(r"(?:menu|Menu)\.[^;]*$", prefix):
            return "pending-menu-adapter"
        return "component-adapter"
    if (re.search(r"(?:\.body|\.title|\.entry|HelpRegistrar)\s*\([^;]*$", prefix)
            or re.search(r"\bHelpRegistrar\s+\w+\s*\([^;]*$", prefix) or path.name == "HelpSections.cpp"):
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
    parts = re.split(r"(\{\d+\}|\{\{|\}\}|\s+)", value)
    accent_applied = False
    def accent_word(match):
        nonlocal accent_applied
        word = match[0]
        if accent_applied:
            return word
        for index, character in enumerate(word):
            replacement = character.translate(ACCENTS)
            if replacement != character:
                accent_applied = True
                return word[:index] + replacement + word[index + 1:]
        return word
    # One accent per entry exercises non-ASCII glyphs while preserving catalog
    # room for real UI sentences. Expansion still measures Unicode codepoints,
    # not bytes, and remains at least +40% for every complete presentation key.
    target = max(len(value) + 2, math.ceil(len(value) * 1.4))
    padding = max(0, target - len(value) - 2)
    eligible = [index for index, part in enumerate(parts) if part and not part.isspace()
                and not re.search(r"\{|\}", part) and len(part) <= 20]
    weight = sum(len(parts[index]) for index in eligible)
    cumulative = applied = 0
    output = []
    for index, part in enumerate(parts):
        placeholder = re.fullmatch(r"\{\d+\}|\{\{|\}\}", part)
        output.append(part if placeholder else re.sub(r"[A-Za-z]+", accent_word, part))
        if index in eligible:
            cumulative += len(part)
            desired = padding * cumulative // weight
            output.append("~" * (desired - applied))
            applied = desired
    if applied < padding:
        # A template with only placeholders or one long identifier still expands
        # without producing an unbreakable paragraph-long padding token.
        output.extend(" " + "~" * min(12, padding - index) for index in range(0, padding - applied, 12))
    return "[" + "".join(output) + "]"


def catalog_text(catalog: dict[str, str], code: str = "language") -> str:
    """One canonical, compact JSON encoding; whitespace inside values is data."""
    encoded = json.dumps(catalog, ensure_ascii=False, separators=(",", ":"), sort_keys=True) + "\n"
    if len(encoded.encode("utf-8")) > MAX_CATALOG_BYTES:
        raise ValueError(f"{code} catalog exceeds 256 KiB: {len(encoded.encode('utf-8'))} bytes; review protected/fragments first")
    return encoded


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
                # The plain HelpRegistrar overload splits its body at newlines
                # before calling HelpWriter. Catalog those actual paragraph
                # sinks, rather than a never-displayed combined literal.
                prefix = source[max(0, start - 180):start]
                paragraphs = value.splitlines() if "\n" in value and re.search(r"\bHelpRegistrar\s+\w+\s*\([^;]*$", prefix) else [value]
                keys = []
                for paragraph in paragraphs:
                    if not paragraph:
                        continue
                    key = by_value.get(paragraph, "s." + hashlib.sha256(paragraph.encode("utf-8")).hexdigest()[:12])
                    if key in english and english[key] != paragraph:
                        raise ValueError("catalog key collision")
                    english[key] = paragraph
                    by_value[paragraph] = key
                    keys.append(key)
                if len(paragraphs) == 1:
                    record["key"] = keys[0] if keys else None
                else:
                    record["paragraph_keys"] = keys
            records.append(record)
    localized = {key: pseudo(value) for key, value in english.items()}
    localized["locale.nativeName"] = "Pseudo (+40%)"
    for code, catalog in (("en", english), ("pseudo", localized)):
        catalog_text(catalog, code)
    return english, localized, records


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    english, localized, records = build()
    outputs = {LANG / "en.json": english, LANG / "pseudo.json": localized, ROOT / "INVENTORY.json": records}
    failed = []
    for path, value in outputs.items():
        encoded = catalog_text(value, path.stem) if path.parent == LANG else json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
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
