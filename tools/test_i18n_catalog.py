"""Data-only localization completeness, size and pseudo-layout invariants."""
import json
import math
import re
import unittest
from pathlib import Path

from Source.features.i18n import build_catalog


class CatalogTests(unittest.TestCase):
    def test_character_literals_cannot_hide_following_presentation(self):
        source = r'''const auto quote = '"'; const auto escaped = '\''; const auto slash = '\\';
            const auto wide = L'"'; const auto count = 100'000;
            auto label = "A complete " /* join */ "visible sentence.";
            auto raw = R"tag(A 'character' and "quoted" text.)tag";
            auto error = "The file could not be read.";'''
        self.assertEqual([value for value, _, _ in build_catalog.literals(source)], [
            "A complete visible sentence.", "A 'character' and \"quoted\" text.",
            "The file could not be read."])

    def test_compact_encoding_preserves_values_keys_utf8_and_determinism(self):
        values = {"s.unchanged": "A full sentence: {0}.\nKeep  two spaces.", "locale.nativeName": "Français"}
        encoded = build_catalog.catalog_text(values)
        self.assertEqual(json.loads(encoded), values)
        self.assertEqual(encoded, build_catalog.catalog_text(dict(reversed(list(values.items())))))
        self.assertEqual(encoded.count("\n"), 1)
        self.assertIn("Français", encoded)

    def test_byte_cap_stays_strict_for_multibyte_utf8(self):
        self.assertLessEqual(len(build_catalog.catalog_text({"s.ok": "é" * 8192}).encode("utf-8")), build_catalog.MAX_CATALOG_BYTES)
        self.assertEqual(build_catalog.MAX_CATALOG_BYTES, 1024 * 1024)
        with self.assertRaisesRegex(ValueError, "exceeds 1 MiB"):
            build_catalog.catalog_text({"s.large": "é" * (512 * 1024)})

    def test_protocol_context_does_not_exclude_same_word_at_a_display_sink(self):
        cases = [
            ('juce::XmlElement root ("GapBank");', "GapBank", "protected-or-nonprose"),
            ('root.createNewChildElement ("Scene");', "Scene", "protected-or-nonprose"),
            ('root.hasTagName ("GFRECIPE");', "GFRECIPE", "protected-or-nonprose"),
            ('folder.getChildFile ("Recipes");', "Recipes", "protected-or-nonprose"),
            ('name.containsOnly ("abcdefghijklmnopqrstuvwxyz._-");', "abcdefghijklmnopqrstuvwxyz._-", "protected-or-nonprose"),
            ('label.setText ("Scene", juce::dontSendNotification);', "Scene", "component-adapter"),
            ('error = "The scene bank is invalid.";', "The scene bank is invalid.", "pending-sink-review"),
        ]
        for source, expected_value, category in cases:
            with self.subTest(source=source):
                value, start, end = next(build_catalog.literals(source))
                self.assertEqual(value, expected_value)
                self.assertEqual(build_catalog.classify(value, source, start, end, Path("Example.cpp"), set()), category)

    def test_catalogs_and_inventory_match_current_source(self):
        english, pseudo, inventory = build_catalog.build()
        self.assertEqual(json.loads((build_catalog.LANG / "en.json").read_text()), english)
        self.assertEqual(json.loads((build_catalog.LANG / "pseudo.json").read_text()), pseudo)
        self.assertEqual(json.loads((build_catalog.ROOT / "INVENTORY.json").read_text()), inventory)

    def test_utf8_catalogs_fit_cap_and_pseudo_preserves_slots(self):
        english = json.loads((build_catalog.LANG / "en.json").read_text())
        pseudo = json.loads((build_catalog.LANG / "pseudo.json").read_text())
        for code in ("en", "pseudo"):
            data = (build_catalog.LANG / (code + ".json")).read_bytes()
            self.assertLessEqual(len(data), build_catalog.MAX_CATALOG_BYTES)
            data.decode("utf-8", errors="strict")
        for key, original in english.items():
            translated = pseudo[key]
            if key != "locale.nativeName":
                self.assertGreaterEqual(len(translated), math.ceil(len(original) * 1.4), key)
            self.assertEqual(re.findall(r"\{\d+\}", original), re.findall(r"\{\d+\}", translated), key)

    def test_inventory_keeps_protected_ids_out_of_the_catalog(self):
        inventory = json.loads((build_catalog.ROOT / "INVENTORY.json").read_text())
        self.assertTrue(any(row["wiring"] == "protected-or-nonprose" and row["key"] is None for row in inventory))
        for row in inventory:
            if row["wiring"] in ("protected-or-nonprose", "pending-dynamic-template"):
                self.assertIsNone(row["key"])


if __name__ == "__main__":
    unittest.main()
