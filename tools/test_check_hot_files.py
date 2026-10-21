import unittest

from tools import check_hot_files as chf


class CheckHotFilesTests(unittest.TestCase):
    def test_feature_pr_touching_hot_file_fails(self):
        errors = chf.check("E-1: Fill Audition", set(), ["Source/Parameters.h", "Source/features/x/a.cpp"])
        self.assertEqual(len(errors), 1)
        self.assertIn("Source/Parameters.h", errors[0])

    def test_label_overrides_hot_file_rule(self):
        self.assertEqual(chf.check("E-1: Fill Audition", {"hot-file-ok"}, ["CMakeLists.txt"]), [])

    def test_feature_label_without_ticket_title_counts(self):
        self.assertTrue(chf.check("Add thing", {"feature"}, ["Source/PluginProcessor.cpp"]))

    def test_feature_pr_in_own_files_passes(self):
        files = ["Source/features/lock/Lock.cpp", "cmake/sources/lock.cmake", "tools/param_ids/lock.lock",
                 "Tests/features/TestLock.cpp"]
        self.assertEqual(chf.check("C-5: Fill Lock", {"feature"}, files), [])

    def test_phase_zero_and_maintenance_prs_are_exempt(self):
        self.assertEqual(chf.check("F0-2: scheduler stages", set(), ["Source/dsp/FillEngine.cpp"]), [])
        self.assertEqual(chf.check("docs: fix", set(), ["PROGRESS.md"]), [])

    def test_golden_requires_title_tag(self):
        files = ["Tests/golden/linux/A_drum_loop_default.f32"]
        self.assertTrue(chf.check("C-2: Macros", {"hot-file-ok"}, files))
        self.assertEqual(chf.check("C-2: Macros [golden] macros change the default sound", {"hot-file-ok"}, files), [])

    def test_fixtures_are_protected_like_goldens(self):
        self.assertTrue(chf.check("fix: tidy", set(), ["Tests/fixtures/state_0.0.1_preset_init.bin"]))

    def test_perf_baseline_requires_perf_tag(self):
        self.assertTrue(chf.check("fix: tidy", set(), ["Tests/perf_baseline.json"]))
        self.assertEqual(chf.check("chore: [perf] new baseline", set(), ["Tests/perf_baseline.json"]), [])

    def test_ticket_title_detection(self):
        for title in ["E-1: x", "C-5: x", "F-L07: x", "N-FX1: x", "UI-0: x", "U-10a: x"]:
            self.assertTrue(chf.is_ticket_title(title), title)
        for title in ["F0-7: x", "Merge branch", "release: x"]:
            self.assertFalse(chf.is_ticket_title(title), title)


if __name__ == "__main__":
    unittest.main()
