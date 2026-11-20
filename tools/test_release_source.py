"""Release-source eligibility checks with recorded job shapes and an offline CLI."""

import copy
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[1]
SHA = "ae87910a32ebe24c2c9f8e1758b4ce7d5e866642"
RUN_ID = 123
PREFIX = "Candidate native and sanitizer assurance / "
# These are the eight required matrix rows, independently frozen from the
# workflow's API job names. An ordinary successful CI run does not contain them.
ASSURANCE_NAMES = (
    "Linux ASan-UBSan-full",
    "Linux TSan-threading",
    "Linux Release performance and continuous processor soak",
    "Full QA ubuntu-22.04 MIDI_OUT=OFF",
    "Full QA windows-2022 MIDI_OUT=OFF",
    "Full QA macos-15 MIDI_OUT=OFF",
    "Full QA macos-15-intel MIDI_OUT=OFF",
    "Full QA ubuntu-22.04 MIDI_OUT=ON",
)


def jobs_fixture():
    names = [PREFIX + name for name in ASSURANCE_NAMES] + ["CI gate"]
    return [{"id": index + 1, "run_id": RUN_ID, "head_sha": SHA,
             "name": name, "status": "completed", "conclusion": "success"}
            for index, name in enumerate(names)]


def pages_fixture(jobs=None):
    jobs = jobs_fixture() if jobs is None else jobs
    return [{"total_count": len(jobs), "jobs": jobs}]


class ReleaseSourceTests(unittest.TestCase):
    def validate(self, pages):
        from tools.qa.release_source import validate_assurance
        validate_assurance(pages, RUN_ID, SHA)

    def test_all_matrix_rows_must_be_present(self):
        for missing in ASSURANCE_NAMES:
            with self.subTest(missing=missing):
                jobs = [job for job in jobs_fixture() if job["name"] != PREFIX + missing]
                with self.assertRaisesRegex(ValueError, "missing full assurance"):
                    self.validate(pages_fixture(jobs))

    def test_nonpassing_or_incomplete_job_never_qualifies(self):
        for status, conclusion in (("completed", "failure"), ("completed", "skipped"),
                                   ("completed", "cancelled"), ("completed", "timed_out"),
                                   ("completed", "neutral"), ("in_progress", None),
                                   ("queued", None), ("in_progress", "success")):
            with self.subTest(status=status, conclusion=conclusion):
                jobs = jobs_fixture()
                jobs[0].update(status=status, conclusion=conclusion)
                with self.assertRaisesRegex(ValueError, "did not pass"):
                    self.validate(pages_fixture(jobs))

    def test_job_results_cannot_be_borrowed_from_another_run_or_commit(self):
        for field, value in (("run_id", RUN_ID + 1), ("head_sha", "b" * 40)):
            with self.subTest(field=field):
                jobs = jobs_fixture()
                jobs[0][field] = value
                with self.assertRaisesRegex(ValueError, "different run or commit"):
                    self.validate(pages_fixture(jobs))

    def test_all_pages_are_combined(self):
        jobs = jobs_fixture()
        self.validate([{"total_count": len(jobs), "jobs": jobs[:3]},
                       {"total_count": len(jobs), "jobs": jobs[3:]}])

    def test_github_cli_consecutive_json_pages_are_read(self):
        from tools.qa.release_source import parse_job_pages
        jobs = jobs_fixture()
        pages = [{"total_count": len(jobs), "jobs": jobs[:3]},
                 {"total_count": len(jobs), "jobs": jobs[3:]}]
        parsed = parse_job_pages("\n ".join(json.dumps(page, indent=2) for page in pages) + "\n")
        self.validate(parsed)

    def test_truncated_trailing_page_cannot_be_ignored(self):
        from tools.qa.release_source import parse_job_pages
        with self.assertRaises(ValueError):
            parse_job_pages(json.dumps(pages_fixture()[0]) + "\n{\"jobs\":")

    def test_partial_or_changed_pagination_does_not_qualify(self):
        pages = pages_fixture()
        pages[0]["total_count"] += 1
        with self.assertRaisesRegex(ValueError, "pagination"):
            self.validate(pages)
        pages.append({"total_count": 500, "jobs": []})
        with self.assertRaisesRegex(ValueError, "pagination"):
            self.validate(pages)

    def test_duplicate_job_ids_and_matrix_names_are_rejected(self):
        for duplicate_id in (True, False):
            with self.subTest(duplicate_id=duplicate_id):
                jobs = jobs_fixture()
                duplicate = copy.deepcopy(jobs[0])
                if not duplicate_id:
                    duplicate["id"] = 100
                jobs.append(duplicate)
                with self.assertRaisesRegex(ValueError, "duplicate"):
                    self.validate(pages_fixture(jobs))

    def test_additional_assurance_rows_must_also_pass(self):
        jobs = jobs_fixture()
        jobs.append(dict(jobs[0], id=100, name=PREFIX + "Future platform", conclusion="skipped"))
        with self.assertRaisesRegex(ValueError, "Future platform"):
            self.validate(pages_fixture(jobs))

    def test_malformed_response_is_not_empty_success(self):
        for pages in (None, {}, [], [{"message": "API error"}], [{"total_count": 1, "jobs": [None]}],
                      [{"total_count": True, "jobs": []}]):
            with self.subTest(pages=pages), self.assertRaises(ValueError):
                self.validate(pages)


class ReleaseSourceWorkflowTests(unittest.TestCase):
    """Execute the real release verify step, replacing only GitHub and tag reads."""

    def run_verify(self, jobs, workflow_path=".github/workflows/ci.yml"):
        workflow = (REPO / ".github/workflows/release-beta.yml").read_text(encoding="utf-8")
        step = workflow.split("      - id: check\n", 1)[1]
        script_lines = step.split("        run: |\n", 1)[1].splitlines()
        body = []
        for line in script_lines:
            if line and not line.startswith("          "):
                break
            body.append(line[10:] if line else "")

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = root / "jobs.json"
            fixture.write_text("\n".join(json.dumps(page) for page in pages_fixture(jobs)), encoding="utf-8")
            commands = {
                "git": "#!/usr/bin/env bash\n[[ $1 == ls-remote ]]\n",
                "gh": """#!/usr/bin/env bash
set -eu
if [[ $1 == release && $2 == view ]]; then exit 1; fi
case "$*" in
  *'/jobs?'*) cat "$JOB_FIXTURE" ;;
  *'/artifacts'*)
    printf '%s\\n' gapfill-linux-x64 gapfill-windows-x64 gapfill-macos-arm64 gapfill-macos-x86_64 unit-test-results ;;
  *'actions/runs/'*) printf 'success\\t%s\\t%s\\n' "$GITHUB_SHA" "$SOURCE_WORKFLOW_PATH" ;;
  *) echo 'unexpected gh command' >&2; exit 2 ;;
esac
""",
            }
            for name, script in commands.items():
                path = root / name
                path.write_text(script, encoding="utf-8", newline="\n")
                path.chmod(0o755)
            output = root / "output"
            cmake = (REPO / "CMakeLists.txt").read_text(encoding="utf-8")
            version = re.search(r"project\(GapFill VERSION ([0-9.]+)", cmake).group(1)
            env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ.get("PATH", ""),
                       INPUT_TAG=f"v{version}-beta2", INPUT_RUN_ID=str(RUN_ID), GITHUB_SHA=SHA,
                       GITHUB_REPOSITORY="djshellshoxxx/gapfill", GITHUB_OUTPUT=output.as_posix(),
                       RUNNER_TEMP=root.as_posix(), JOB_FIXTURE=fixture.as_posix(),
                       SOURCE_WORKFLOW_PATH=workflow_path)
            result = subprocess.run(["bash", "-c", "\n".join(body)], cwd=REPO, env=env,
                                    text=True, capture_output=True)
            published_outputs = output.read_text() if output.exists() else ""
            return result, published_outputs

    def test_successful_ordinary_ci_cannot_qualify_a_release(self):
        jobs = [job for job in jobs_fixture() if not job["name"].startswith(PREFIX)]
        jobs.append({"id": 50, "run_id": RUN_ID, "head_sha": SHA,
                     "name": PREFIX.rstrip(" /"), "status": "completed", "conclusion": "skipped"})
        result, outputs = self.run_verify(jobs)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("run_id=", outputs)

    def test_complete_assurance_can_proceed_to_packaging(self):
        result, outputs = self.run_verify(jobs_fixture())
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"run_id={RUN_ID}\n", outputs)

    def test_cancelled_assurance_cannot_publish_workflow_outputs(self):
        jobs = jobs_fixture()
        jobs[5]["conclusion"] = "cancelled"
        result, outputs = self.run_verify(jobs)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("cancelled", result.stderr)
        self.assertEqual(outputs, "")

    def test_assurance_from_another_commit_cannot_publish_workflow_outputs(self):
        jobs = jobs_fixture()
        jobs[0]["head_sha"] = "b" * 40
        result, outputs = self.run_verify(jobs)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("different run or commit", result.stderr)
        self.assertEqual(outputs, "")

    def test_a_workflow_with_only_a_matching_filename_prefix_does_not_qualify(self):
        result, outputs = self.run_verify(jobs_fixture(), ".github/workflows/ci.yml-other")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not a CI workflow run", result.stdout)
        self.assertEqual(outputs, "")


if __name__ == "__main__":
    unittest.main()
