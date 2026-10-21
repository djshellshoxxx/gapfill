"""Tests for tools/qa/run_matrix.py (Q-01): the harness must fail on zero tests, never report a skipped validator as a pass,
write a complete evidence file and exit non-zero on any failed step. Uses a fake test binary (a Python one-liner)."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "qa"))

import run_matrix as rm  # noqa: E402
import summarise as sm  # noqa: E402


def fake(text: str, code: int = 0) -> list[str]:
    """A command that prints `text` and exits with `code`."""
    return [sys.executable, "-I", "-c", f"import sys; sys.stdout.write({text!r}); sys.exit({code})"]


class RunMatrixTests(unittest.TestCase):
    def run_one(self, spec: "rm.StepSpec") -> dict:
        with tempfile.TemporaryDirectory() as tmp:
            return rm.run_step(spec, cwd=Path(tmp), log_dir=Path(tmp) / "logs")

    def test_parse_counts_uses_last_line(self):
        self.assertEqual(rm.parse_counts("1 passed, 0 failed\nnoise\n12 passed, 3 failed\n"), (12, 3))
        self.assertIsNone(rm.parse_counts("no summary here"))

    def test_passing_step(self):
        row = self.run_one(rm.StepSpec("unit", fake("5 passed, 0 failed\n"), min_tests=5))
        self.assertEqual(row["status"], "pass")
        self.assertEqual((row["executed"], row["passed"], row["failed"]), (5, 5, 0))
        self.assertTrue(Path(row["log"]).name == "unit.log")

    def test_zero_tests_is_failure(self):
        row = self.run_one(rm.StepSpec("unit", fake("0 passed, 0 failed\n"), min_tests=1))
        self.assertEqual(row["status"], "fail")
        self.assertIn("at least 1", row["detail"])

    def test_failed_tests_and_nonzero_exit_fail(self):
        self.assertEqual(self.run_one(rm.StepSpec("a", fake("3 passed, 1 failed\n", 1), min_tests=1))["status"], "fail")
        self.assertEqual(self.run_one(rm.StepSpec("b", fake("3 passed, 1 failed\n", 0), min_tests=1))["status"], "fail")

    def test_skipped_validator_is_unverified_not_pass(self):
        steps = rm.default_steps(["x"], "full")
        by_name = {s.name: s for s in steps}
        self.assertIsNone(by_name["pluginval"].cmd)
        self.assertIsNone(by_name["clap-validator"].cmd)
        row = self.run_one(by_name["pluginval"])
        self.assertEqual(row["status"], "unverified")
        self.assertNotEqual(row["status"], "pass")
        self.assertIn("pluginval", row["detail"])

    def test_bad_filter_expected_to_fail_passes_only_when_nonzero(self):
        ok = self.run_one(rm.StepSpec("bad", fake("ERROR: test filter matched zero tests\n", 1), expect_nonzero=True))
        self.assertEqual(ok["status"], "pass")
        bad = self.run_one(rm.StepSpec("bad", fake("0 passed, 0 failed\n", 0), expect_nonzero=True))
        self.assertEqual(bad["status"], "fail")
        missing = self.run_one(rm.StepSpec("bad", ["/nonexistent/binary/for/test"], expect_nonzero=True))
        self.assertEqual(missing["status"], "fail")                 # a binary that cannot start does not prove the guard

    def test_nonzero_exit_by_signal_is_marked_as_a_crash(self):
        row = self.run_one(rm.StepSpec("crash", fake("", 139), min_tests=1))
        self.assertEqual(row["status"], "fail")
        self.assertTrue(row.get("crash"))
        self.assertIn("dump", row["detail"])

    def test_tiers_select_steps(self):
        fast = {s.name for s in rm.default_steps(["x"], "fast")}
        full = {s.name for s in rm.default_steps(["x"], "full")}
        self.assertIn("unit", fast)
        self.assertNotIn("unit-all", fast)
        self.assertIn("unit-all", full)
        self.assertIn("perf", full)
        self.assertIn("bad-filter-must-fail", fast & full)

    def test_evidence_schema_and_summary_exit_code(self):
        rows = [
            self.run_one(rm.StepSpec("good", fake("2 passed, 0 failed\n"), min_tests=1)),
            self.run_one(rm.StepSpec("skipped", None, unverified_reason="no tool")),
        ]
        evidence = rm.make_evidence(rows, sha="abc123", os_name="linux", command_line="run_matrix --binary x", tier="fast")

        for key in ("schema", "sha", "os", "python", "tier", "command_line", "steps", "manual_rows_open"):
            self.assertIn(key, evidence)

        for row in evidence["steps"]:
            for key in ("name", "status", "executed", "passed", "failed", "seconds", "log", "command"):
                self.assertIn(key, row)
            self.assertIn(row["status"], ("pass", "fail", "unverified"))

        self.assertEqual(evidence["sha"], "abc123")
        self.assertTrue(evidence["manual_rows_open"])
        text, code = rm.summarise(evidence)
        self.assertEqual(code, 0)                                   # unverified does not fail the run ...
        self.assertIn("unverified (not a pass): skipped", text)     # ... but is always listed

        rows.append(self.run_one(rm.StepSpec("broken", fake("0 passed, 1 failed\n", 1), min_tests=1)))
        _, code = rm.summarise(rm.make_evidence(rows, sha="abc123", os_name="linux", command_line="x", tier="fast"))
        self.assertEqual(code, 1)

    def test_summarise_cli_reads_files_and_flags_bad_ones(self):
        with tempfile.TemporaryDirectory() as tmp:
            good = Path(tmp) / "good.json"
            good.write_text(json.dumps(rm.make_evidence([], sha="s", os_name="linux", command_line="x", tier="fast")))
            bad = Path(tmp) / "bad.json"
            bad.write_text("{not json")
            self.assertEqual(sm.main([str(good)]), 0)
            self.assertEqual(sm.main([str(bad)]), 1)
            self.assertEqual(sm.main([]), 2)

    def test_sha_is_recorded(self):
        self.assertTrue(rm.git_sha())


if __name__ == "__main__":
    unittest.main()
