"""Synthetic trace tests; none are evidence of a real DAW measurement."""
import csv
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.qa import transport_jitter as jitter


class TransportJitterTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def trace(self, rows, *, callbacks=None, dropped=0, complete=True):
        path = self.root / "synthetic.csv"
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=jitter.FIELDS)
            writer.writeheader()
            for row in rows:
                writer.writerow(dict(zip(jitter.FIELDS, row)))
        metadata = {"schema": 1, "producer": "synthetic-test", "host_id": "synthetic",
                    "host_version": "fixture", "source_sha": "a" * 40, "source_dirty": False,
                    "complete": complete, "rows_written": len(rows), "dropped_rows": dropped,
                    "callbacks_seen": callbacks if callbacks is not None else len(rows), "io_error": False}
        meta = self.root / "synthetic.json"
        meta.write_text(json.dumps(metadata))
        return path, meta

    @staticmethod
    def row(sequence, ppq, bpm=120, frames=512, rate=48000, sample=None, offline=0):
        return [sequence, 1, 1, 1, ppq, bpm, frames, rate, int(sample is not None), sample or 0, offline]

    def test_steady_callback_trace_retains_input_hash_and_never_claims_host_acceptance(self):
        path, meta = self.trace([self.row(i, i * 512 / 24000, sample=i * 512) for i in range(4)])
        report = jitter.analyze(path, meta)
        self.assertEqual(report["models"]["explicit_hypotheses"]["count"], 3)
        self.assertLess(report["models"]["explicit_hypotheses"]["max_abs_beats"], 1e-12)
        self.assertEqual(report["input"]["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
        self.assertEqual(report["data_integrity"], "complete")
        self.assertEqual(report["block_sizes"], {"512": 4})
        self.assertEqual(report["valid_bpm_range"], [120.0, 120.0])
        self.assertIn("unverified", report["real_host_acceptance"])

    def test_linear_ramp_integral_and_boundary_step_are_distinct_models(self):
        path, meta = self.trace([self.row(0, 0, 60, 48000, sample=0),
                                 self.row(1, 1.5, 120, 48000, sample=48000),
                                 self.row(2, 4, 180, 48000, sample=96000)])
        report = jitter.analyze(path, meta)
        self.assertAlmostEqual(report["models"]["previous_bpm"]["max_abs_beats"], .5)
        self.assertEqual(report["models"]["endpoint_average"]["max_abs_beats"], 0)
        self.assertEqual(report["models"]["explicit_hypotheses"]["over_tolerance"], 0)
        path, meta = self.trace([self.row(0, 0, 60, 48000), self.row(1, 1, 600)])
        report = jitter.analyze(path, meta)
        self.assertEqual(report["models"]["explicit_hypotheses"]["max_abs_beats"], 0)
        self.assertGreater(report["models"]["endpoint_average"]["max_abs_beats"], .25)

    def test_gap_between_hypotheses_is_an_outlier_not_accepted_as_ramp(self):
        path, meta = self.trace([self.row(0, 0, 60, 48000), self.row(1, 3, 600)])
        report = jitter.analyze(path, meta)
        self.assertEqual(report["models"]["explicit_hypotheses"]["over_tolerance"], 1)
        self.assertEqual(report["models"]["explicit_hypotheses"]["max_abs_beats"], 2)

    def test_dropped_capture_rows_are_not_transport_jitter(self):
        path, meta = self.trace([self.row(0, 0), self.row(2, 1024 / 24000), self.row(3, 1536 / 24000)],
                                callbacks=4, dropped=1)
        report = jitter.analyze(path, meta)
        self.assertEqual(report["events"]["missing_rows"], 1)
        self.assertEqual(report["models"]["explicit_hypotheses"]["count"], 1)
        self.assertEqual(report["data_integrity"], "incomplete")

    def test_sample_position_jump_and_offline_render_are_separate_events(self):
        path, meta = self.trace([self.row(0, 0, sample=0), self.row(1, 16, sample=48000),
                                 self.row(2, 16 + 512 / 24000, sample=48512),
                                 self.row(3, 17, sample=49024, offline=1)])
        report = jitter.analyze(path, meta)
        self.assertEqual(report["events"]["sample_position_discontinuity"], 1)
        self.assertEqual(report["events"]["offline_interval"], 1)
        self.assertEqual(report["models"]["explicit_hypotheses"]["count"], 1)

    def test_declared_drop_counters_must_match_csv(self):
        path, meta = self.trace([self.row(0, 0), self.row(2, .1)], callbacks=3, dropped=0)
        with self.assertRaises(ValueError):
            jitter.analyze(path, meta)

    def test_nonfinite_timing_duplicate_sequence_and_bad_schema_are_rejected(self):
        for rows in ([self.row(0, float("nan"))], [self.row(0, 0), self.row(0, .1)]):
            path, meta = self.trace(rows)
            with self.assertRaises(ValueError):
                jitter.analyze(path, meta)
        path.write_text("not,the,trace,header\n")
        with self.assertRaises(ValueError):
            jitter.analyze(path, meta)

    def test_incomplete_capture_remains_unverified_and_annotations_retain_provenance(self):
        path, meta = self.trace([self.row(0, 0), self.row(1, 16), self.row(2, 16 + 512 / 24000)], complete=False)
        markers = self.root / "markers.json"
        markers.write_text(json.dumps({"schema": 1, "events": [{"sequence": 1, "kind": "seek"}]}))
        report = jitter.analyze(path, meta, markers)
        self.assertEqual(report["data_integrity"], "incomplete")
        self.assertEqual(report["events"]["annotated_seek"], 1)
        self.assertEqual(report["models"]["explicit_hypotheses"]["count"], 1)
        self.assertIn("markers_sha256", report["input"])

    def test_input_hash_cannot_describe_a_file_changed_during_analysis(self):
        path, _ = self.trace([self.row(0, 0), self.row(1, 512 / 24000)])
        parse = jitter.parse_row
        changed = False

        def mutate(raw):
            nonlocal changed
            result = parse(raw)
            if not changed:
                path.write_text(path.read_text().replace("120", "121", 1))
                changed = True
            return result

        with patch.object(jitter, "parse_row", mutate):
            with self.assertRaisesRegex(ValueError, "changed while"):
                jitter.analyze(path)


if __name__ == "__main__":
    unittest.main()
