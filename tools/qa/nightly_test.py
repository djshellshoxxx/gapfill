"""Release-assurance behavior checks; no JUCE or signing credentials required."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]


class NightlyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.binary = self.root / "test-binary"

    def program(self, body):
        self.binary.write_text("#!" + sys.executable + "\n" + body, encoding="utf-8")
        self.binary.chmod(0o755)

    def run_nightly(self):
        return subprocess.run([sys.executable, str(REPO / "tools/qa/nightly_run.py"),
                               "--binary", str(self.binary), "--filter", "Example", "--label", "example",
                               "--out", str(self.root / "evidence")], capture_output=True, text=True)

    def test_records_executed_counts_and_exact_binary(self):
        self.program("print('2 passed, 0 failed')\n")
        result = self.run_nightly()
        self.assertEqual(result.returncode, 0, result.stderr)
        evidence = json.loads((self.root / "evidence/example.json").read_text())
        self.assertEqual(evidence["executed"], 2)
        self.assertEqual(evidence["status"], "pass")
        self.assertEqual(evidence["binary_sha256"], hashlib.sha256(self.binary.read_bytes()).hexdigest())
        self.assertEqual(evidence["command"], [str(self.binary), "Example"])
        self.assertEqual(len(evidence["source_sha"]), 40)
        self.assertTrue(Path(evidence["log"]).is_file())

    def test_zero_tests_cannot_pass(self):
        self.program("print('0 passed, 0 failed')\n")
        result = self.run_nightly()
        self.assertNotEqual(result.returncode, 0)

    def test_reported_failures_cannot_pass_with_zero_exit(self):
        self.program("print('1 passed, 1 failed')\n")
        self.assertNotEqual(self.run_nightly().returncode, 0)

    def test_nonzero_exit_and_diagnostics_survive(self):
        self.program("print('diagnostic before failure')\nraise SystemExit(7)\n")
        self.assertNotEqual(self.run_nightly().returncode, 0)
        evidence = json.loads((self.root / "evidence/example.json").read_text())
        self.assertEqual(evidence["exit_code"], 7)
        self.assertIn("diagnostic before failure", Path(evidence["log"]).read_text())

    def test_label_cannot_escape_output_directory(self):
        self.program("print('1 passed, 0 failed')\n")
        result = subprocess.run([sys.executable, str(REPO / "tools/qa/nightly_run.py"),
                                 "--binary", str(self.binary), "--label", "../escape",
                                 "--out", str(self.root / "evidence")], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / "escape.json").exists())

    def test_au_helper_builds_explicit_au_target(self):
        build = self.root / "build"
        build.mkdir()
        (build / "CMakeCache.txt").write_text("")
        cmake = self.root / "cmake"
        cmake.write_text("#!/usr/bin/env python3\nimport sys\nprint('GapFill_AU: phony')\n"
                         "if 'help' not in sys.argv:\n    print('BUILD:' + ' '.join(sys.argv[1:]))\n")
        cmake.chmod(0o755)
        env = dict(os.environ, PATH=str(self.root) + os.pathsep + os.environ.get("PATH", ""))
        result = subprocess.run(["bash", str(REPO / "tools/dev/build-target.sh"), str(build), "au"],
                                capture_output=True, text=True, env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--target GapFill_AU", result.stdout)


if __name__ == "__main__":
    unittest.main()
