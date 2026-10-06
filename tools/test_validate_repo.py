import tempfile
import unittest
from pathlib import Path

from tools import validate_repo


class ValidateRepoTests(unittest.TestCase):
    def test_extract_parameter_ids_rejects_duplicates(self):
        text = '''
#define GAPFILL_PARAMS(X) \\
    X (one, "one") \\
    X (two, "two") \\
    X (again, "one")
'''
        ids = validate_repo.extract_parameter_ids(text)
        self.assertEqual(ids, ["one", "two", "one"])
        self.assertEqual(validate_repo.duplicates(ids), ["one"])

    def test_factory_preset_ids_are_extracted(self):
        text = '''
{ "Preset", "desc", { { "fill", 25.0f }, { "wet", 50.0f } } }
'''
        self.assertEqual(validate_repo.extract_preset_parameter_ids(text), ["fill", "wet"])

    def test_minimal_valid_repository_has_no_errors(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "Source").mkdir()
            (root / "docs" / "standards").mkdir(parents=True)
            (root / ".github" / "workflows").mkdir(parents=True)

            (root / "Source" / "Parameters.h").write_text(
                '#define GAPFILL_PARAMS(X) \\\n'
                '    X (fill, "fill") \\\n'
                '    X (wet, "wet")\n',
                encoding="utf-8",
            )
            (root / "Source" / "Presets.cpp").write_text(
                '{ "Good", "desc", { { "fill", 0.5f }, { "wet", 1.0f } } }',
                encoding="utf-8",
            )
            (root / "CMakeLists.txt").write_text(
                'project(GapFill VERSION 1.0.0)\n'
                'juce_add_plugin(GapFill FORMATS VST3 Standalone '
                'EDITOR_WANTS_KEYBOARD_FOCUS TRUE)\n'
                'GapFillTests\nclap_juce_extensions_plugin\n',
                encoding="utf-8",
            )
            (root / "README.md").write_text(
                'docs/standards/CDL_PLUGIN_BASELINE.md\n',
                encoding="utf-8",
            )
            (root / "docs" / "standards" / "CDL_PLUGIN_BASELINE.md").write_text(
                "MUST", encoding="utf-8"
            )
            (root / "docs" / "COMPLIANCE.md").write_text(
                "## Profiles\n", encoding="utf-8"
            )
            (root / "docs" / "VALIDATION.md").write_text(
                "pluginval clap-validator Standalone MIDI Learn Keyboard navigation",
                encoding="utf-8",
            )
            (root / ".github" / "workflows" / "ci.yml").write_text(
                "python tools/validate_repo.py\n"
                "GapFillTests GapFill_VST3 GapFill_CLAP GapFill_Standalone\n"
                "clap-validator validate\n"
                "--strictness-level 10\n"
                "Smoke-launch standalone\n",
                encoding="utf-8",
            )

            self.assertEqual(validate_repo.validate(root), [])

    def test_validate_reports_unknown_factory_preset_parameter(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "Source").mkdir()
            (root / "docs" / "standards").mkdir(parents=True)
            (root / ".github" / "workflows").mkdir(parents=True)

            (root / "Source" / "Parameters.h").write_text(
                '#define GAPFILL_PARAMS(X) \\\n    X (fill, "fill")\n',
                encoding="utf-8",
            )
            (root / "Source" / "Presets.cpp").write_text(
                '{ "Bad", "desc", { { "notReal", 1.0f } } }',
                encoding="utf-8",
            )
            (root / "CMakeLists.txt").write_text(
                'project(GapFill VERSION 1.0.0)\nFORMATS VST3 Standalone\nGapFill_CLAP\n',
                encoding="utf-8",
            )
            (root / "README.md").write_text(
                'docs/standards/CDL_PLUGIN_BASELINE.md\n',
                encoding="utf-8",
            )
            (root / "docs" / "standards" / "CDL_PLUGIN_BASELINE.md").write_text(
                "baseline", encoding="utf-8"
            )
            (root / "docs" / "COMPLIANCE.md").write_text(
                "compliance", encoding="utf-8"
            )
            (root / ".github" / "workflows" / "ci.yml").write_text(
                "python tools/validate_repo.py\nGapFillTests\nGapFill_VST3\n",
                encoding="utf-8",
            )

            errors = validate_repo.validate(root)
            self.assertTrue(any("notReal" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
