"""Native QA must wait for AU discovery without treating discovery as validation."""
import plistlib
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock

from tools.qa import nightly_native


class AudioUnitRegistrationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.bundle = self.root / "GapFill.component"
        (self.bundle / "Contents").mkdir(parents=True)
        with (self.bundle / "Contents/Info.plist").open("wb") as stream:
            plistlib.dump({"AudioComponents": [{"type": "aumf", "subtype": "Gpfl", "manufacturer": "Gfau"}]}, stream)

    @staticmethod
    def result(output="", code=0):
        return subprocess.CompletedProcess([], code, output, "")

    def test_waits_for_exact_identity_and_preserves_attempts(self):
        runner = Mock(side_effect=[self.result(code=1), self.result("aumf Other Gfau - another plugin"),
                                  self.result("  aumf Gpfl Gfau - GapFill\n")])
        sleep = Mock()
        out = self.root / "evidence"
        nightly_native.wait_for_audio_unit(self.bundle, out, runner=runner, sleep=sleep)
        sleep.assert_called_once_with(5)
        self.assertEqual(runner.call_args_list[0].args[0], ["killall", "AudioComponentRegistrar"])
        self.assertTrue((out / "discovery-1.log").is_file())
        self.assertIn("GapFill", (out / "discovery-2.log").read_text(encoding="utf-8"))
        self.assertTrue(all(call.args[0] == ["auval", "-a"] for call in runner.call_args_list[1:]))

    def test_missing_or_failed_discovery_never_passes(self):
        for result in (self.result("aumf Gpfl Else - different manufacturer"),
                       self.result("aumf Gpfl Gfau - stale output", code=1)):
            with self.subTest(result=result):
                runner = Mock(return_value=result)
                sleep = Mock()
                with self.assertRaisesRegex(RuntimeError, "not registered"):
                    nightly_native.wait_for_audio_unit(self.bundle, self.root / "evidence",
                                                       runner=runner, sleep=sleep, attempts=2)
                self.assertEqual(runner.call_count, 3)
                sleep.assert_called_once_with(5)

    def test_actual_au_validation_remains_required_after_discovery(self):
        from tools.qa import run_matrix
        steps = run_matrix.default_steps(["GapFillTests"], "full", au_bundle=self.bundle)
        au = next(step for step in steps if step.name == "auval")
        # On a non-Mac test host this row is intentionally unverified; the identity
        # resolver still defines the real validation command used on macOS.
        self.assertEqual(run_matrix.auval_command(self.bundle), ["auval", "-v", "aumf", "Gpfl", "Gfau"])
        self.assertFalse(au.expect_nonzero)


if __name__ == "__main__":
    unittest.main()
