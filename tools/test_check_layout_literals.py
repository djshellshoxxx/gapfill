import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_layout_literals as cl  # noqa: E402


class LayoutLiteralTests(unittest.TestCase):
    def test_finds_non_multiples_of_four(self):
        text = "a.reduced (2, 0);\nb.removeFromLeft (10);\nc.removeFromTop (8);\nd.reduced (12, 16);\n"
        found = cl.offending_literals(text)
        self.assertEqual([(1, "reduced", 2), (2, "removeFromLeft", 10)], found)

    def test_ignores_comments_zero_floats_and_nested_calls(self):
        text = "// x.reduced (3);\nx.reduced (0, 4);\n/* y.removeFromTop (5) */\nz.reduced (2.5f);\nw.reduced (foo (3));\n"
        self.assertEqual([], cl.offending_literals(text))

    def test_identifiers_with_digits_are_not_literals(self):
        self.assertEqual([], cl.offending_literals("x.reduced (kPad2, 8);\n"))

    def test_ratchet_logic(self):
        self.assertEqual([], cl.check({"a": 5}, {"a": 5}))
        self.assertTrue(cl.check({"a": 6}, {"a": 5}))          # grew
        self.assertTrue(cl.check({"a": 4}, {"a": 5}))          # baseline stale: lower it
        self.assertTrue(cl.check({"a": 1}, {}))                # no baseline entry

    def test_repository_is_within_its_baseline(self):
        counts = {name: cl.count_for(cl.ROOT / name) for name in cl.FILES}
        self.assertEqual([], cl.check(counts, cl.load_baseline()))


if __name__ == "__main__":
    unittest.main()
