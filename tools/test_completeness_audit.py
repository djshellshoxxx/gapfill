#!/usr/bin/env python3
"""Runs the completeness audit as a unit test: the repository must have no un-exempted wiring gap."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "audit"))

import completeness_audit as audit  # noqa: E402


class CompletenessAudit(unittest.TestCase):
    def test_repository_has_no_unexempted_gap(self):
        report = audit.Report()
        for check in audit.CHECKS:
            if check is audit.check_params:
                check(audit.ROOT, report, False)
            else:
                check(audit.ROOT, report)
        failures = ["{} {}: {}".format(c, k, t) for level, c, k, t in report.findings if level == "FAIL"]
        self.assertEqual(failures, [], "\n".join(failures))

    def test_every_exemption_has_a_reason(self):
        for key, reason in audit.KNOWN_EXEMPTIONS.items():
            self.assertGreater(len(reason), 20, key)

    def test_parsers(self):
        self.assertEqual(audit.parse_lock("# c\na\n\nb\n"), ["a", "b"])
        self.assertEqual(audit.parse_status_rows("| F-L01-x | yes | a |"), {"F-L01-x": "yes"})
        groups = audit.parse_param_groups(audit.ROOT)
        self.assertGreater(sum(len(v) for v in groups.values()), 200)
        enum = audit.parse_command_enum(audit.ROOT)
        self.assertEqual(enum["Preview"], 1)
        self.assertEqual(enum["SkipRecipe"], 32)


if __name__ == "__main__":
    unittest.main()
