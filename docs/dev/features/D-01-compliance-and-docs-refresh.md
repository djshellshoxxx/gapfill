# D-01 Documentation drift refresh (COMPLIANCE.md and related)

Status: specified (this pass). Size S, docs only, separate PR (audit risk 4). No code.

## 1. Summary and user-visible behaviour
`docs/COMPLIANCE.md` still says GitHub runners cannot start (as of 2026-10-06) although CI completed on 2026-10-19; other docs repeat historical test counts ("91 tests", "50 tests") and a Windows cross-compile story that predates native CI. Bring them in line with evidence, without claiming anything not shown by fresh CI logs.

## 2. Success / quality metric
Every status sentence in the touched docs cites a dated source (run id, commit, or "unverified"); `tools/validate_repo.py` gains a check that `COMPLIANCE.md` contains no row marked "runners cannot start". Zero unsourced pass claims.

## 3. Design
* Rewrite the CI-status paragraph in `docs/COMPLIANCE.md`: baseline commit, last green master run id (take from the 08 A0-01 snapshot `docs/progress/release-baseline-YYYYMMDD.md`), remaining manual/unverified rows.
* `PROGRESS.md` "Next up" items 1-5 are referenced from the new `docs/qa/` plan: link rather than duplicate (maintainer owns PROGRESS.md; propose the edit in the PR description).
* README/docs: replace historical test counts by "see latest CI run".
* `docs/TEST-TRACEABILITY.md`: add the QA step names from Q-01 when that lands.
* `validate_repo.py`: add a check for stale phrases (`runners cannot`, `cannot start`) in COMPLIANCE.md plus a unit test.

## 4. Parameters and state
None.

## 5. Real-time safety
None.

## 6. UI
None. Help text unchanged.

## 7. MIDI / automation / CLAP / host
None.

## 8. Tests
`tools/test_validate_repo.py`: `compliance_has_no_stale_runner_claim` | `compliance_cites_a_run_or_unverified`.

## 9. Dependencies and build order
After A0-01 baseline snapshot exists. Independent of code PRs; must not be bundled with a feature PR.

## 10. Effort and risks
~0.5 day. Risk: restating a pass that CI does not show; every sentence cites its source or says unverified.

## 11. Acceptance checklist
- [ ] Stale sentences removed, replaced with sourced statements
- [ ] validate_repo check and test added and green
- [ ] No feature or code files in the PR
