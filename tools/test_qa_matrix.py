"""Tests for tools/qa/run_matrix.py (Q-01): the harness must fail on zero tests, never report a skipped validator as a pass,
write a complete evidence file and exit non-zero on any failed step. Uses a fake test binary (a Python one-liner)."""

import json
import copy
import hashlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
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
        ok = self.run_one(rm.StepSpec("bad", fake("0 passed, 0 failed\nERROR: test filter matched zero tests: nonexistent_filter_should_fail\n", 1), expect_nonzero=True))
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

    def test_python_test_step_can_import_the_tools_package(self):
        step = {s.name: s for s in rm.default_steps(["x"], "fast")}["preflight-python-tests"]
        self.assertNotIn("-I", step.cmd)                                # isolated mode drops the repo root from sys.path
        self.assertNotIn("discover", step.cmd)                          # tools/ has no __init__.py
        files = [a for a in step.cmd if a.endswith(".py")]
        self.assertIn("tools/test_validate_repo.py", files)
        self.assertTrue(all((rm.REPO / f).is_file() for f in files))

    def test_skip_marks_a_step_unverified_with_the_reason_never_pass(self):
        steps = rm.apply_skips(rm.default_steps(["x"], "fast"), ["unit"], "full suite ran in the previous CI step")
        by_name = {s.name: s for s in steps}
        self.assertIsNone(by_name["unit"].cmd)
        self.assertTrue(by_name["unit"].extra.get("skipped"))
        row = self.run_one(by_name["unit"])
        self.assertEqual(row["status"], "unverified")
        self.assertIn("previous CI step", row["detail"])
        self.assertIsNotNone(by_name["blocksweep"].cmd)                 # other steps untouched
        self.assertEqual([s.name for s in steps], [s.name for s in rm.default_steps(["x"], "fast")])

    def test_skip_of_an_unknown_step_is_an_error_not_a_silent_no_op(self):
        with self.assertRaises(ValueError):
            rm.apply_skips(rm.default_steps(["x"], "fast"), ["unti"], "typo")

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

    def test_bad_filter_rejects_crash_exit_even_with_expected_diagnostic(self):
        diagnostic = "0 passed, 0 failed\nERROR: test filter matched zero tests: nonexistent_filter_should_fail\n"
        row = self.run_one(rm.StepSpec("bad", fake(diagnostic, 139), expect_nonzero=True))
        self.assertEqual(row["status"], "fail")
        self.assertTrue(row["crash"])

    def test_bad_filter_rejects_unrelated_failure_and_missing_summary(self):
        for text, code in (("segmentation fault", 1),
                           ("ERROR: test filter matched zero tests\n", 1),
                           ("0 passed, 0 failed\nERROR: test filter matched zero tests\n", 2)):
            with self.subTest(text=text, code=code):
                row = self.run_one(rm.StepSpec("bad", fake(text, code), expect_nonzero=True))
                self.assertEqual(row["status"], "fail")

    def test_step_records_exit_and_log_hash_for_evidence_integrity(self):
        row = self.run_one(rm.StepSpec("unit", fake("5 passed, 0 failed\n"), min_tests=1))
        self.assertEqual(row.get("exit_code"), 0)
        self.assertRegex(row.get("log_sha256", ""), r"^[0-9a-f]{64}$")
        self.assertEqual(row.get("command_argv"), fake("5 passed, 0 failed\n"))

    def test_python_unittest_counts_are_required_and_recorded(self):
        for output, expected in (("Ran 3 tests in 0.01s\n\nOK\n", "pass"),
                                 ("Ran 0 tests in 0.01s\n\nOK\n", "fail"),
                                 ("Ran 3 tests in 0.01s\n\nFAILED (failures=1)\n", "fail")):
            with self.subTest(output=output):
                row = self.run_one(rm.StepSpec("python", fake(output), min_tests=1))
                self.assertEqual(row["status"], expected)
                if expected == "pass":
                    self.assertEqual(row["executed"], 3)

    def test_real_unittest_all_skipped_is_not_executed_coverage(self):
        command = [sys.executable, "-I", "-c", "import unittest\n@unittest.skip('no device')\nclass Example(unittest.TestCase):\n def test_device(self): pass\nunittest.main()"]
        row = self.run_one(rm.StepSpec("python", command, min_tests=1))
        self.assertEqual(row["status"], "fail")
        self.assertEqual(row["executed"], 0)
        self.assertEqual(row.get("reported_tests"), 1)
        self.assertEqual(row.get("skipped_tests"), 1)

    def test_nested_cpp_output_cannot_inflate_skipped_python_coverage(self):
        command = [sys.executable, "-I", "-c", "import unittest\nprint('999 passed, 0 failed')\n@unittest.skip('no device')\nclass Example(unittest.TestCase):\n def test_device(self): pass\nunittest.main()"]
        row = self.run_one(rm.StepSpec("python", command, min_tests=1))
        self.assertEqual(row["status"], "fail")
        self.assertEqual(row["executed"], 0)
        self.assertEqual(row.get("reported_tests"), 1)

    def test_real_unittest_failure_and_teardown_error_count_one_failed_case(self):
        command = [sys.executable, "-I", "-c", "import unittest\nclass Example(unittest.TestCase):\n def test_failure(self): self.fail('assertion')\n def tearDown(self): raise RuntimeError('cleanup')\nunittest.main()"]
        row = self.run_one(rm.StepSpec("python", command, min_tests=1))
        self.assertEqual(row["status"], "fail")
        self.assertEqual((row["executed"], row["passed"], row["failed"]), (1, 0, 1))
        self.assertEqual(row.get("failure_events"), 2)

    def test_qa_lifecycle_selects_concurrent_stress_instead_of_history_reopen(self):
        step = {s.name: s for s in rm.default_steps(["GapFillTests"], "fast")}["gui-lifecycle"]
        self.assertEqual(step.cmd[-1], "EditorLifecycle")

    def test_auval_uses_the_target_identity_and_nonmac_is_not_applicable(self):
        with mock.patch.object(rm.platform, "system", return_value="Darwin"), \
             mock.patch.object(rm.shutil, "which", return_value="/usr/bin/auval"):
            step = {s.name: s for s in rm.default_steps(["x"], "full")}["auval"]
            self.assertEqual(step.cmd[-3:], ["aumf", "Gpfl", "Gfau"])
        with mock.patch.object(rm.platform, "system", return_value="Linux"):
            step = {s.name: s for s in rm.default_steps(["x"], "full")}["auval"]
            self.assertFalse(step.extra.get("applicable", True))

    def evidence_fixture(self, directory, tier="full"):
        directory = Path(directory)
        names = ["preflight-validate-repo", "preflight-python-tests", "unit-all", "blocksweep",
                 "blocksweep-every-size", "ratesweep", "determinism", "robust", "fixtures",
                 "gui-lifecycle", "perf", "pluginval", "clap-validator"]
        base = rm.run_step(rm.StepSpec("executed", fake("100 passed, 0 failed\n"), min_tests=1),
                           cwd=directory, log_dir=directory / "logs")
        rows = [{**base, "name": name} for name in names]
        guard = rm.run_step(rm.StepSpec("bad-filter-must-fail", fake(
            "0 passed, 0 failed\nERROR: test filter matched zero tests: nonexistent_filter_should_fail\n", 1),
            expect_nonzero=True), cwd=directory, log_dir=directory / "logs")
        rows += [guard, {"name": "auval", "status": "unverified", "applicable": False,
                         "executed": 0, "passed": 0, "failed": 0, "seconds": 0, "detail": "not applicable", "log": "", "command": ""}]
        evidence = rm.make_evidence(rows, sha="a" * 40, os_name="linux", command_line="fixture", tier=tier)
        evidence["provenance"] = {"tested_sha": "a" * 40, "source_head": "a" * 40,
                                  "dirty": False, "architecture": "x86_64", "build_configuration": "Release",
                                  "compiler": {"id": "GNU", "version": "12.2.0", "path": "/usr/bin/c++"},
                                  "dependencies": {"JUCE": "b" * 40, "clap-juce-extensions": "c" * 40},
                                  "dependency_dirty": {"JUCE": False, "clap-juce-extensions": False},
                                  "binary_sha256": "d" * 64}
        if tier == "fast":
            evidence["steps"] = [r for r in rows if r["name"] not in ("unit-all", "perf", "pluginval", "clap-validator", "auval")]
            evidence["steps"].append({"name": "unit", "status": "unverified", "skipped": True,
                                      "executed": 0, "passed": 0, "failed": 0, "seconds": 0, "detail": "full suite already ran", "log": "", "command": ""})
        return evidence

    def test_strict_summary_accepts_automated_full_evidence_but_keeps_manual_open(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "full.json"
            path.write_text(json.dumps(self.evidence_fixture(tmp)))
            self.assertEqual(sm.main(["--strict", str(path)]), 0)

    def test_strict_summary_rejects_empty_or_incomplete_green_evidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "incomplete.json"
            evidence = self.evidence_fixture(tmp)
            evidence["steps"] = []
            path.write_text(json.dumps(evidence))
            self.assertEqual(sm.main(["--strict", str(path)]), 1)

    def test_strict_join_requires_same_candidate_and_preserves_the_skipped_row(self):
        with tempfile.TemporaryDirectory() as tmp:
            full, fast = Path(tmp) / "full.json", Path(tmp) / "fast.json"
            donor, recipient = self.evidence_fixture(tmp), self.evidence_fixture(tmp, "fast")
            full.write_text(json.dumps(donor))
            fast.write_text(json.dumps(recipient))
            summary = Path(tmp) / "summary.json"
            self.assertEqual(sm.main(["--strict", "--summary-out", str(summary), str(fast), str(full)]), 0)
            result = json.loads(summary.read_text())
            self.assertEqual(result["scope"], "automated acceptance only")
            self.assertEqual(result["coverage_links"][0]["source_step"], "unit-all")
            self.assertEqual(result["coverage_links"][0]["evidence_sha256"], hashlib.sha256(full.read_bytes()).hexdigest())
            self.assertEqual(json.loads(fast.read_text())["steps"][-1]["status"], "unverified")
            self.assertTrue(result["manual_rows_open"])
            for field, value in (("tested_sha", "e" * 40), ("build_configuration", "Debug"),
                                 ("architecture", "arm64"), ("binary_sha256", "e" * 64), ("dirty", True)):
                bad = copy.deepcopy(donor)
                bad["provenance"][field] = value
                full.write_text(json.dumps(bad))
                with self.subTest(field=field):
                    self.assertEqual(sm.main(["--strict", str(fast), str(full)]), 1)

    def test_strict_summary_rejects_tampered_logs_and_invalid_evidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            evidence = self.evidence_fixture(tmp)
            path = Path(tmp) / "full.json"
            path.write_text(json.dumps(evidence))
            Path(evidence["steps"][0]["log"]).write_text("different output")
            self.assertEqual(sm.main(["--strict", str(path)]), 1)
            path.write_text("[]")
            self.assertEqual(sm.main(["--strict", str(path)]), 1)

    def test_evidence_exposes_unverified_provenance_without_fabricating_build_metadata(self):
        evidence = rm.make_evidence([], sha="a" * 40, os_name="linux", command_line="fixture", tier="fast")
        self.assertEqual(evidence["schema"], 2)
        self.assertIn("provenance", evidence)
        self.assertEqual(evidence["provenance"].get("build_configuration"), "unverified")

    def test_au_bundle_metadata_controls_the_actual_validator_command(self):
        import plistlib
        with tempfile.TemporaryDirectory() as tmp:
            bundle = Path(tmp) / "Example.component"
            (bundle / "Contents").mkdir(parents=True)
            with (bundle / "Contents" / "Info.plist").open("wb") as stream:
                plistlib.dump({"AudioComponents": [{"type": "aufx", "subtype": "Test", "manufacturer": "Abcd"}]}, stream)
            self.assertEqual(rm.auval_command(bundle), ["auval", "-v", "aufx", "Test", "Abcd"])

    def test_cli_observes_build_compiler_binary_and_full_source_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp) / "build"
            compiler = build / "CMakeFiles" / "3.25.1" / "CMakeCXXCompiler.cmake"
            compiler.parent.mkdir(parents=True)
            compiler.write_text('set(CMAKE_CXX_COMPILER_ID "GNU")\nset(CMAKE_CXX_COMPILER_VERSION "12.2.0")\n')
            (build / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Release\nCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/c++\n")
            out = Path(tmp) / "evidence"
            command = [sys.executable, "-I", str(rm.REPO / "tools/qa/run_matrix.py"), "--binary", sys.executable,
                       "--out", str(out), "--build-dir", str(build), "--build-config", "Release"]
            for step in rm.default_steps([sys.executable], "fast"):
                command += ["--skip", step.name]
            result = subprocess.run(command, cwd=rm.REPO, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            evidence = json.loads(next(out.glob("*.json")).read_text())
            p = evidence["provenance"]
            self.assertRegex(p["tested_sha"], r"^[0-9a-f]{40}$")
            self.assertEqual(p["compiler"]["version"], "12.2.0")
            self.assertEqual(p["build_configuration"], "Release")
            self.assertEqual(p["binary_sha256"], hashlib.sha256(Path(sys.executable).read_bytes()).hexdigest())
            self.assertIsInstance(p["dirty"], bool)
            self.assertTrue(all(row["status"] == "unverified" for row in evidence["steps"]))

    def test_strict_summary_requires_log_counts_to_match_claimed_coverage(self):
        with tempfile.TemporaryDirectory() as tmp:
            evidence = self.evidence_fixture(tmp)
            log = Path(evidence["steps"][0]["log"])
            log.write_text("0 passed, 0 failed\n")
            digest = hashlib.sha256(log.read_bytes()).hexdigest()
            for row in evidence["steps"]:
                if row.get("log") == str(log):
                    row["log_sha256"] = digest
            path = Path(tmp) / "full.json"
            path.write_text(json.dumps(evidence))
            self.assertEqual(sm.main(["--strict", str(path)]), 1)

    def test_strict_summary_rejects_dirty_dependencies_even_with_pinned_revisions(self):
        with tempfile.TemporaryDirectory() as tmp:
            evidence = self.evidence_fixture(tmp)
            evidence["provenance"]["dependency_dirty"]["JUCE"] = True
            path = Path(tmp) / "full.json"
            path.write_text(json.dumps(evidence))
            self.assertEqual(sm.main(["--strict", str(path)]), 1)

    def test_malformed_step_is_rejected_without_traceback(self):
        with tempfile.TemporaryDirectory() as tmp:
            evidence = self.evidence_fixture(tmp)
            del evidence["steps"][0]["seconds"]
            path = Path(tmp) / "malformed.json"
            path.write_text(json.dumps(evidence))
            try:
                result = sm.main([str(path)])
            except (KeyError, TypeError) as exc:
                self.fail(f"malformed evidence must be rejected without a traceback: {exc}")
            self.assertEqual(result, 1)

    def test_fast_only_evidence_cannot_claim_complete_full_automation(self):
        with tempfile.TemporaryDirectory() as tmp:
            evidence = self.evidence_fixture(tmp, "fast")
            full = self.evidence_fixture(tmp)
            unit = next(row for row in full["steps"] if row["name"] == "unit-all")
            evidence["steps"][-1] = {**unit, "name": "unit"}
            path = Path(tmp) / "fast.json"
            path.write_text(json.dumps(evidence))
            self.assertEqual(sm.main(["--strict", str(path)]), 1)


if __name__ == "__main__":
    unittest.main()
