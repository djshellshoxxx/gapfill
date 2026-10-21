import tempfile
import unittest
from pathlib import Path

from tools import validate_repo


def make_minimal_repo(root: Path, core_ids=("fill", "wet"), lock_ids=None) -> None:
    """Writes the smallest repository validate() accepts."""
    (root / "Source").mkdir(exist_ok=True)
    (root / "docs" / "standards").mkdir(parents=True, exist_ok=True)
    (root / ".github" / "workflows").mkdir(parents=True, exist_ok=True)
    (root / "tools" / "param_ids").mkdir(parents=True, exist_ok=True)

    entries = " \\\n".join(f'    X ({i}, "{i}")' for i in core_ids)
    (root / "Source" / "Parameters.h").write_text(
        "#define GAPFILL_PARAMS_CORE(X) \\\n" + entries + "\n"
        "#define GAPFILL_PARAMS(X) \\\n    GAPFILL_PARAMS_CORE(X)\n",
        encoding="utf-8",
    )
    (root / "tools" / "param_ids" / "core.lock").write_text(
        "# lock\n" + "\n".join(core_ids if lock_ids is None else lock_ids) + "\n", encoding="utf-8"
    )
    (root / "Source" / "Presets.cpp").write_text(
        '{ "Good", "desc", { { "fill", 0.5f }, { "wet", 1.0f } } }', encoding="utf-8"
    )
    (root / "CMakeLists.txt").write_text(
        "project(GapFill VERSION 1.0.0)\n"
        "juce_add_plugin(GapFill FORMATS VST3 Standalone "
        "EDITOR_WANTS_KEYBOARD_FOCUS TRUE)\n"
        "GapFillTests\nclap_juce_extensions_plugin\n",
        encoding="utf-8",
    )
    (root / "README.md").write_text("docs/standards/CDL_PLUGIN_BASELINE.md\n", encoding="utf-8")
    (root / "docs" / "standards" / "CDL_PLUGIN_BASELINE.md").write_text("MUST", encoding="utf-8")
    (root / "docs" / "COMPLIANCE.md").write_text("## Profiles\n", encoding="utf-8")
    (root / "docs" / "VALIDATION.md").write_text(
        "pluginval clap-validator Standalone MIDI Learn Keyboard navigation", encoding="utf-8"
    )
    (root / ".github" / "workflows" / "ci.yml").write_text(
        "python tools/validate_repo.py\n"
        "GapFillTests GapFill_VST3 GapFill_CLAP GapFill_Standalone\n"
        "clap-validator validate\n"
        "--strictness-level 10\n"
        "Smoke-launch standalone\n",
        encoding="utf-8",
    )


def add_group(root: Path, group: str, ids, lock_ids=None) -> None:
    """Adds Source/params/<group>.h (and its lock) with the given IDs and chains it."""
    params = root / "Source" / "params"
    params.mkdir(exist_ok=True)
    entries = " \\\n".join(f'    X ({i}, "{i}")' for i in ids)
    body = f"#define GAPFILL_PARAMS_{group.upper()}(X)" + (" \\\n" + entries if ids else "") + "\n"
    (params / f"{group}.h").write_text(body, encoding="utf-8")
    (root / "tools" / "param_ids" / f"{group}.lock").write_text(
        "\n".join(ids if lock_ids is None else lock_ids) + "\n", encoding="utf-8"
    )
    header = root / "Source" / "Parameters.h"
    text = header.read_text(encoding="utf-8").rstrip("\n")
    header.write_text(text + f" \\\n    GAPFILL_PARAMS_{group.upper()}(X)\n", encoding="utf-8")


class ParamLockTests(unittest.TestCase):
    def errors(self, build) -> list[str]:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build(root)
            return validate_repo.validate(root)

    def test_clean_groups_pass(self):
        def build(root):
            make_minimal_repo(root)
            add_group(root, "macros", ["macroActivity", "macrosOn"])
            add_group(root, "lock", [])

        self.assertEqual(self.errors(build), [])

    def test_new_id_without_lock_line_fails(self):
        def build(root):
            make_minimal_repo(root)
            add_group(root, "macros", ["macroActivity", "macroChaos"], lock_ids=["macroActivity"])

        errors = self.errors(build)
        self.assertTrue(any("macroChaos" in e and "not in tools/param_ids/macros.lock" in e for e in errors), errors)

    def test_removed_or_renamed_locked_id_fails(self):
        def build(root):
            make_minimal_repo(root)
            add_group(root, "macros", ["macroActivityRenamed"], lock_ids=["macroActivity"])

        errors = self.errors(build)
        self.assertTrue(any("missing or renamed" in e and "macroActivity" in e for e in errors), errors)

    def test_reordered_ids_fail(self):
        def build(root):
            make_minimal_repo(root)
            add_group(root, "macros", ["b", "a"], lock_ids=["a", "b"])

        self.assertTrue(any("reordered" in e for e in self.errors(build)))

    def test_core_reorder_and_removal_fail(self):
        def reordered(root):
            make_minimal_repo(root, core_ids=("wet", "fill"), lock_ids=["fill", "wet"])

        self.assertTrue(any("reordered" in e for e in self.errors(reordered)))

        def removed(root):
            make_minimal_repo(root, core_ids=("fill",), lock_ids=["fill", "wet"])

        self.assertTrue(any("missing or renamed" in e for e in self.errors(removed)))

    def test_duplicate_id_across_groups_fails(self):
        def build(root):
            make_minimal_repo(root)
            add_group(root, "macros", ["fill"])

        self.assertTrue(any("duplicate stable parameter ID: fill" in e for e in self.errors(build)))

    def test_invalid_id_spelling_fails(self):
        def build(root):
            make_minimal_repo(root)
            add_group(root, "macros", ["Bad_Id"])

        self.assertTrue(any("^[a-z][A-Za-z0-9_]*$" in e for e in self.errors(build)))

    def test_group_with_ids_but_no_lock_file_fails(self):
        def build(root):
            make_minimal_repo(root)
            add_group(root, "macros", ["macroActivity"])
            (root / "tools" / "param_ids" / "macros.lock").unlink()

        self.assertTrue(any("no tools/param_ids/macros.lock" in e for e in self.errors(build)))

    def test_group_header_not_chained_fails(self):
        def build(root):
            make_minimal_repo(root)
            add_group(root, "macros", [])
            header = root / "Source" / "Parameters.h"
            header.write_text(header.read_text().replace(" \\\n    GAPFILL_PARAMS_MACROS(X)", ""))

        self.assertTrue(any("not chained" in e for e in self.errors(build)))

    def test_group_order_is_append_only(self):
        def build(root):
            make_minimal_repo(root)
            add_group(root, "macros", [])
            add_group(root, "lock", [])
            (root / "tools" / "param_ids" / "GROUP_ORDER").write_text("CORE\nLOCK\nMACROS\n")

        self.assertTrue(any("group order changed" in e for e in self.errors(build)))

        def build_ok(root):
            make_minimal_repo(root)
            add_group(root, "macros", [])
            add_group(root, "lock", [])
            (root / "tools" / "param_ids" / "GROUP_ORDER").write_text("CORE\nMACROS\n")

        self.assertEqual(self.errors(build_ok), [])

    def test_real_repository_validates(self):
        root = Path(__file__).resolve().parents[1]
        self.assertEqual(validate_repo.validate(root), [])


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
            make_minimal_repo(root)
            self.assertEqual(validate_repo.validate(root), [])

    def test_validate_reports_unknown_factory_preset_parameter(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_minimal_repo(root, core_ids=("fill",))
            (root / "Source" / "Presets.cpp").write_text(
                '{ "Bad", "desc", { { "notReal", 1.0f } } }', encoding="utf-8"
            )

            errors = validate_repo.validate(root)
            self.assertTrue(any("notReal" in error for error in errors))

    def test_read_lock_ignores_comments_and_blank_lines(self):
        self.assertEqual(validate_repo.read_lock("# c\n\na # trailing\nb\n"), ["a", "b"])


if __name__ == "__main__":
    unittest.main()
