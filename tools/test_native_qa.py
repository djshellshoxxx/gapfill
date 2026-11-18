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

    def fake(self, listings, probes):
        """Dispatch on the command: auval -a returns `listings`, auval -v returns `probes` in order."""
        listings, probes = iter(listings), iter(probes)
        calls = []

        def run(command, **_):
            calls.append(command)
            if command[0] == "killall":
                return self.result()
            return next(listings) if command[1] == "-a" else next(probes)
        return run, calls

    def test_waits_for_exact_identity_and_preserves_attempts(self):
        runner, calls = self.fake([self.result(code=1), self.result("aumf Other Gfau - another plugin"),
                                   self.result("  aumf Gpfl Gfau - GapFill\n")],
                                  [self.result(code=1), self.result(code=1)])
        sleep = Mock()
        out = self.root / "evidence"
        nightly_native.wait_for_audio_unit(self.bundle, out, runner=runner, sleep=sleep)
        self.assertEqual(sleep.call_count, 2)
        self.assertEqual(calls[0], ["killall", "-9", "AudioComponentRegistrar"])
        self.assertEqual(calls[1:], [["auval", "-a"], ["auval", "-v", "aumf", "Gpfl", "Gfau"]] * 2 + [["auval", "-a"]])
        self.assertTrue((out / "discovery-1.log").is_file())
        self.assertTrue((out / "probe-2.log").is_file())
        self.assertIn("GapFill", (out / "discovery-3.log").read_text(encoding="utf-8"))

    def test_successful_direct_probe_counts_as_registered(self):
        runner, calls = self.fake([self.result("nothing listed")], [self.result("PASSED")])
        sleep = Mock()
        nightly_native.wait_for_audio_unit(self.bundle, self.root / "evidence", runner=runner, sleep=sleep)
        sleep.assert_not_called()
        self.assertEqual(calls[-1], ["auval", "-v", "aumf", "Gpfl", "Gfau"])

    def test_missing_or_failed_discovery_never_passes(self):
        for listing in (self.result("aumf Gpfl Else - different manufacturer"),
                        self.result("aumf Gpfl Gfau - stale output", code=1)):
            with self.subTest(listing=listing):
                runner, calls = self.fake([listing] * 2, [self.result(code=1)] * 2)
                sleep = Mock()
                with self.assertRaisesRegex(RuntimeError, "not registered"):
                    nightly_native.wait_for_audio_unit(self.bundle, self.root / "evidence",
                                                       runner=runner, sleep=sleep, attempts=2)
                self.assertEqual(len(calls), 5)
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
