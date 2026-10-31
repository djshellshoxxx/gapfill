"""Data-only localization completeness, size and pseudo-layout invariants."""
import json
import math
import re
import unittest

from Source.features.i18n import build_catalog


class CatalogTests(unittest.TestCase):
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
            self.assertLessEqual(len(data), 256 * 1024)
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
