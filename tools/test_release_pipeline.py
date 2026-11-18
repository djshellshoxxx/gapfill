"""Check release gating and archive selection without production credentials."""
import argparse
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

PACKAGING = Path(__file__).resolve().parents[1] / "packaging"
sys.path.insert(0, str(PACKAGING))
import release_pipeline
import sign_release


class ReleasePipelineTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def fake_sign(self, status):
        def run(arguments):
            report = Path(arguments[arguments.index("--out") + 1])
            report.parent.mkdir(parents=True, exist_ok=True)
            report.write_text(json.dumps({"status": status, "detail": "fixture"}))
            return {"pass": 0, "unverified": 2, "fail": 1}[status]
        return run

    def test_unsigned_beta_retains_unverified_without_failure(self):
        with patch.object(sign_release, "main", self.fake_sign("unverified")):
            report = release_pipeline.invoke_sign("windows", self.root / "report.json", False)
        self.assertEqual(report["status"], "unverified")

    def test_required_signing_cannot_ignore_missing_identity(self):
        with patch.object(sign_release, "main", self.fake_sign("unverified")):
            with self.assertRaises(RuntimeError):
                release_pipeline.invoke_sign("windows", self.root / "report.json", True)

    def test_real_signing_failure_always_blocks_unsigned_beta(self):
        with patch.object(sign_release, "main", self.fake_sign("fail")):
            with self.assertRaises(RuntimeError):
                release_pipeline.invoke_sign("windows", self.root / "report.json", False)

    def test_versioned_manifest_is_written_once_and_verifies_assets(self):
        assets = self.root / "assets"
        assets.mkdir()
        (assets / "example.zip").write_bytes(b"final packaged bytes")
        report = self.root / "signing.json"
        with patch.dict(os.environ, {"GAPFILL_RELEASE_GPG_KEY": ""}):
            code = sign_release.main(["checksums", "--directory", str(assets), "--out", str(report),
                                      "--manifest-name", "GapFill-0.0.2-beta1-SHA256SUMS.txt"])
        self.assertEqual(code, 2)
        self.assertFalse((assets / "SHA256SUMS.txt").exists())
        manifest = assets / "GapFill-0.0.2-beta1-SHA256SUMS.txt"
        self.assertEqual(list(release_pipeline.verify_release.checksums(manifest)), ["example.zip"])
        (assets / "example.zip").write_bytes(b"changed after signing")
        with self.assertRaises(ValueError):
            release_pipeline.verify_release.checksums(manifest)

    def test_manifest_name_cannot_escape_candidate_directory(self):
        self.assertEqual(sign_release.main(["checksums", "--directory", str(self.root),
                                           "--out", str(self.root / "report.json"),
                                           "--manifest-name", "../SHA256SUMS.txt"]), 1)

    def test_linux_portable_only_requires_its_actual_standalone_format(self):
        specs = release_pipeline.package_specs("linux", "0.0.2-beta1", "x86_64")
        self.assertEqual(specs[0][1], ["standalone"])
        self.assertEqual(specs[1][1], ["vst3"])
        self.assertEqual(specs[2][1], ["clap"])
        self.assertEqual(release_pipeline.package_specs("windows", "0.0.2-beta1", "x86_64")[0][1],
                         ["vst3", "clap", "standalone"])

    def test_tag_rejects_shell_and_path_text(self):
        for value in ("../0.0.2-beta1", "0.0.2-beta1;touch unsafe", "v0.0.2-beta1", "0.0.2-beta"):
            with self.assertRaises(argparse.ArgumentTypeError):
                release_pipeline.asset_tag(value)

    def test_windows_raw_signs_all_three_exact_binaries(self):
        for relative in ("GapFill.exe", "CLAP/GapFill.clap", "VST3/GapFill.vst3/Contents/x86_64-win/GapFill.vst3"):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"fixture")
        args = argparse.Namespace(action="windows-raw", directory=self.root, out=self.root / "evidence",
                                  require_signing=False)
        with patch.object(release_pipeline, "invoke_sign", return_value={"status": "unverified"}) as sign:
            release_pipeline.windows(args)
        argv = sign.call_args.args
        self.assertEqual(argv.count("--input"), 3)
        self.assertTrue(any(str(value).endswith("GapFill.exe") for value in argv))
        self.assertTrue(any(str(value).endswith("GapFill.clap") for value in argv))


if __name__ == "__main__":
    unittest.main()
