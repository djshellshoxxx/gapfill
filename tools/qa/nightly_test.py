"""Release-assurance behavior checks; no JUCE or signing credentials required."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile
import shutil

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

    def test_soak_requires_real_processor_summary(self):
        self.program("print('1 passed, 0 failed')\n")
        result = subprocess.run([sys.executable, str(REPO / "tools/qa/soak_run.py"),
                                 "--binary", str(self.binary), "--seconds", "0.1", "--warmup", "0.01",
                                 "--out", str(self.root / "soak")], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue((self.root / "soak/soak.json").is_file(), result.stderr)
        evidence = json.loads((self.root / "soak/soak.json").read_text())
        self.assertEqual(evidence["status"], "fail")
        self.assertIn("summary", evidence["detail"])

    def test_rss_criterion_uses_measured_samples_and_one_percent_limit(self):
        from tools.qa import soak_run
        good = soak_run.rss_result([10000, 10000, 10000, 10050, 10050, 10050])
        self.assertEqual(good["status"], "pass")
        self.assertAlmostEqual(good["growth_percent"], 0.5)
        self.assertEqual(soak_run.rss_result([10000, 10000, 10000, 10200, 10200, 10200])["status"], "fail")
        self.assertEqual(soak_run.rss_result([])["status"], "unverified")

    def test_soak_keeps_one_process_and_measures_requested_duration(self):
        self.program("import json, os, time\nprint('GF_SOAK_BEGIN', flush=True)\n"
                     "seconds=float(os.environ['GF_SOAK_SECONDS'])\ntime.sleep(seconds)\n"
                     "print('GF_SOAK_SUMMARY ' + json.dumps({'measured_seconds': seconds, 'blocks': 1, "
                     "'processed_samples': 512, 'peak': 0.5, 'allocations': 0, 'engine_idle': True}))\n"
                     "print('1 passed, 0 failed')\n")
        from tools.qa import soak_run
        evidence = soak_run.run(self.binary, seconds=0.15, warmup=0, seed=42, out=self.root / "soak")
        self.assertEqual(evidence["process_starts"], 1)
        self.assertEqual(evidence["processor_status"], "pass")
        self.assertGreaterEqual(evidence["seconds_from_launch"], 0.15)
        self.assertGreater(len(evidence["rss_samples"]), 1)

    def test_malformed_soak_summary_retains_failure_evidence(self):
        self.program("print('GF_SOAK_SUMMARY {\"measured_seconds\":\"invalid\",\"blocks\":1,"
                     "\"processed_samples\":512,\"peak\":0,\"allocations\":0,\"engine_idle\":true}')\n"
                     "print('1 passed, 0 failed')\n")
        from tools.qa import soak_run
        evidence = soak_run.run(self.binary, seconds=0.01, warmup=0, seed=42, out=self.root / "soak")
        self.assertEqual(evidence["status"], "fail")

    def test_downloaded_checksum_tampering_fails(self):
        asset = self.root / "candidate.zip"
        asset.write_bytes(b"original")
        manifest = self.root / "SHA256SUMS.txt"
        manifest.write_text(hashlib.sha256(asset.read_bytes()).hexdigest() + "  candidate.zip\n")
        command = [sys.executable, str(REPO / "packaging/verify_release.py"), "checksums", "--manifest", str(manifest)]
        self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
        asset.write_bytes(b"tampered")
        self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)

    def test_checksum_manifest_cannot_read_outside_download_directory(self):
        manifest = self.root / "SHA256SUMS.txt"
        manifest.write_text("0" * 64 + "  ../outside\n")
        result = subprocess.run([sys.executable, str(REPO / "packaging/verify_release.py"), "checksums",
                                 "--manifest", str(manifest)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)

    def test_package_extraction_rejects_traversal(self):
        archive = self.root / "bad.zip"
        with zipfile.ZipFile(archive, "w") as zipped:
            zipped.writestr("../escaped", "unsafe")
        result = subprocess.run([sys.executable, str(REPO / "packaging/verify_release.py"), "extract",
                                 "--archive", str(archive), "--out", str(self.root / "unpacked")], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / "escaped").exists())

    def test_package_extracts_regular_asset(self):
        archive = self.root / "good.zip"
        with zipfile.ZipFile(archive, "w") as zipped:
            zipped.writestr("GapFill/LICENSE.txt", "AGPL-3.0-or-later")
        result = subprocess.run([sys.executable, str(REPO / "packaging/verify_release.py"), "extract",
                                 "--archive", str(archive), "--out", str(self.root / "unpacked")], capture_output=True)
        self.assertEqual(result.returncode, 0)
        self.assertEqual((self.root / "unpacked/GapFill/LICENSE.txt").read_text(), "AGPL-3.0-or-later")

    def test_package_symlink_cannot_escape_extraction(self):
        archive = self.root / "symlink.zip"
        with zipfile.ZipFile(archive, "w") as zipped:
            entry = zipfile.ZipInfo("GapFill.clap")
            entry.create_system = 3
            entry.external_attr = 0o120777 << 16
            zipped.writestr(entry, "../escaped")
        result = subprocess.run([sys.executable, str(REPO / "packaging/verify_release.py"), "extract",
                                 "--archive", str(archive), "--out", str(self.root / "unpacked")], capture_output=True)
        self.assertNotEqual(result.returncode, 0)

    @unittest.skipUnless(sys.platform.startswith("linux"), "fixture models a Linux ELF package")
    def test_missing_packaged_validator_stays_unverified(self):
        archive = self.root / "candidate.zip"
        header = bytearray(64)
        header[:6] = b"\x7fELF\x02\x01"
        header[18:20] = b"\x3e\0"
        with zipfile.ZipFile(archive, "w") as zipped:
            zipped.writestr("GapFill.clap", header)
            zipped.writestr("LICENSE.txt", "SPDX-License-Identifier: AGPL-3.0-or-later")
        manifest = self.root / "SHA256SUMS.txt"
        manifest.write_text(hashlib.sha256(archive.read_bytes()).hexdigest() + "  candidate.zip\n")
        out = self.root / "validation"
        result = subprocess.run([sys.executable, str(REPO / "packaging/verify_release.py"), "validate",
                                 "--archive", str(archive), "--manifest", str(manifest), "--platform", "linux",
                                 "--formats", "clap", "--out", str(out)], capture_output=True)
        self.assertEqual(result.returncode, 2)
        evidence = json.loads((out / "package-validation.json").read_text())
        self.assertEqual(evidence["checks"]["downloaded-checksums"]["status"], "pass")
        self.assertEqual(evidence["checks"]["clap"]["status"], "unverified")

    def test_unsigned_checksum_preparation_remains_unverified(self):
        assets = self.root / "assets"
        assets.mkdir()
        (assets / "candidate.zip").write_bytes(b"candidate")
        env = dict(os.environ)
        env.pop("GAPFILL_RELEASE_GPG_KEY", None)
        result = subprocess.run([sys.executable, str(REPO / "packaging/sign_release.py"), "checksums",
                                 "--directory", str(assets), "--out", str(self.root / "unsigned.json")],
                                capture_output=True, env=env)
        self.assertEqual(result.returncode, 2)
        self.assertTrue((assets / "SHA256SUMS.txt").is_file())
        self.assertFalse((assets / "SHA256SUMS.txt.asc").exists())

    def test_missing_signing_identity_is_unverified_and_never_success(self):
        env = dict(os.environ)
        env.pop("GAPFILL_WINDOWS_CERT_SHA1", None)
        env.pop("GAPFILL_APPLE_SIGN_IDENTITY", None)
        env.pop("GAPFILL_NOTARY_PROFILE", None)
        result = subprocess.run([sys.executable, str(REPO / "packaging/sign_release.py"), "windows",
                                 "--input", str(self.root / "candidate.exe"), "--out", str(self.root / "signing.json")],
                                capture_output=True, env=env)
        self.assertEqual(result.returncode, 2)
        evidence = json.loads((self.root / "signing.json").read_text())
        self.assertEqual(evidence["status"], "unverified")

    @unittest.skipUnless(shutil.which("gpg"), "GnuPG integration requires the actual gpg executable")
    def test_detached_manifest_signature_with_ephemeral_fixture_key(self):
        key_dir = self.root / "fixture-keyring"
        key_dir.mkdir(mode=0o700)
        env = dict(os.environ, GNUPGHOME=str(key_dir))
        created = subprocess.run(["gpg", "--batch", "--passphrase", "", "--quick-generate-key",
                                  "GapFill fixture <assurance@example.invalid>", "ed25519", "sign", "0"],
                                 capture_output=True, env=env)
        self.assertEqual(created.returncode, 0, created.stderr.decode(errors="replace"))
        if shutil.which("gpgconf"):
            self.addCleanup(subprocess.run, ["gpgconf", "--kill", "gpg-agent"], env=env, capture_output=True)
        keys = subprocess.run(["gpg", "--batch", "--with-colons", "--list-keys"],
                              capture_output=True, text=True, env=env, check=True)
        fingerprint = next(line.split(":")[9] for line in keys.stdout.splitlines() if line.startswith("fpr:"))
        env["GAPFILL_RELEASE_GPG_KEY"] = fingerprint
        assets = self.root / "assets"
        assets.mkdir()
        (assets / "candidate.zip").write_bytes(b"candidate")
        result = subprocess.run([sys.executable, str(REPO / "packaging/sign_release.py"), "checksums",
                                 "--directory", str(assets), "--out", str(self.root / "signed.json")],
                                capture_output=True, env=env)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        verified = subprocess.run(["gpg", "--batch", "--verify", str(assets / "SHA256SUMS.txt.asc"),
                                   str(assets / "SHA256SUMS.txt")], capture_output=True, env=env)
        self.assertEqual(verified.returncode, 0)
        (assets / "SHA256SUMS.txt").write_text("tampered")
        self.assertNotEqual(subprocess.run(["gpg", "--batch", "--verify", str(assets / "SHA256SUMS.txt.asc"),
                                            str(assets / "SHA256SUMS.txt")], capture_output=True, env=env).returncode, 0)


if __name__ == "__main__":
    unittest.main()
