# Q-01 Release QA matrix harness and evidence capture

Status: specified (this pass). Size M. Closes the audit gaps in `docs/audits/2026-10-20-code-qa-feature-inventory.md` ("QA visibility", required QA matrix items 2-6) with repeatable tooling. Complements F0-7 (goldens, AllocGuard) and `07-QA-AND-RELEASE.md` (manual scripts).

## 1. Summary and user-visible behaviour
One command runs the scripted part of the release matrix and writes a machine-readable evidence file per platform: test counts (executed/passed/failed, nonzero required), block-size sweep, sample-rate sweep, determinism, denormal/NaN/silence checks, state fixtures, GUI lifecycle, and validator exit codes. No end-user behaviour.

## 2. Success / quality metric
| # | Metric | Target |
|---|---|---|
| M1 | A run fails when any step executes zero tests or a validator is skipped | yes (self-test with a deliberately empty filter) |
| M2 | Evidence file completeness | every row of the audit matrix items 2-6 has a result or an explicit `unverified` reason |
| M3 | Runtime | fast tier <= 10 min, full tier <= 45 min on CI |
| M4 | No fabricated results | evidence records the git SHA, OS, compiler, command line, and raw log path |

## 3. Design
* `tools/qa/run_matrix.py` (Python 3, stdlib only, `-I` safe) and `tools/qa/run_matrix.sh|.ps1` one-paste wrappers (follows the F0-8 convention). Output `qa-evidence/<sha>-<os>.json` plus raw logs.
* Steps (each returns `{name, status: pass|fail|unverified, executed, passed, failed, seconds, log}`):
  1. `preflight` (`validate_repo.py` + its Python tests, packager fixture, shell lint).
  2. `unit` (`GapFillTests -Combinatorial`, require `executed > 0`), `combinatorial` (full tier only), `bad-filter-must-fail` (`GapFillTests nonexistent_filter_should_fail` must return nonzero).
  3. `blocksweep`: `GapFillTests --scenario blocksweep` renders scenario A at host block sizes 8192/4096/512/64/32 and with alternating patterns, requires identical fill start positions and bit-equal output where the design promises it (new test group `Matrix`, scenario names from `07` section 3).
  4. `ratesweep`: 44.1/48/88.2/96/176.4/192 kHz, finite output, bounds (<= +6 dBFS peak at default), latency equals Lookahead.
  5. `determinism`: same seed + PPQ twice and realtime-chunked vs offline.
  6. `robust`: silence, DC, NaN/Inf injection, denormals, sidechain loss/recovery, bypass toggle, `reset()`, `releaseResources`/re-`prepare`.
  7. `fixtures`: legacy `.gapfill` and host-state blobs round trip (F0-7 fixtures).
  8. `gui-lifecycle`: 30+ editor create/destroy cycles under automation (W-01 test).
  9. `validators`: pluginval 10 (exact bundle path, `--output-dir`), clap-validator, `auval` on macOS; exit codes recorded; skipped = `unverified`, never `pass`.
  10. `perf`: `Perf` group numbers vs `Tests/perf_baseline.json`.
  11. `crash-capture`: on any nonzero exit collect a dump (`procdump`/`coredumpctl`/crash reporter path) and attach.
* `tools/qa/summarise.py` prints a table and exits nonzero on any `fail`; CI uploads `qa-evidence` as an artifact even on failure (08 A5).
* `docs/TEST-TRACEABILITY.md` gains a column linking each requirement to the step name.

## 4. Parameters and state
None.

## 5. Real-time safety
Harness is offline tooling. The `Matrix` tests run under AllocGuard where the audio path is exercised.

## 6. UI
None.

## 7. MIDI / automation / CLAP / host
Real DAW and hardware MIDI rows are always `unverified` in the automated file and are filled by the manual scripts (`docs/qa/<release>/<host>.md`); the summariser lists them as open.

## 8. Tests
`tools/test_qa_matrix.py`: `zero_tests_is_failure` | `skipped_validator_is_unverified_not_pass` | `evidence_schema_valid` | `nonzero_exit_collects_dump_path` | `sha_and_os_recorded` | `bad_filter_expected_to_fail_passes_only_when_nonzero` | `summary_exit_code`. GapFill side: `Matrix_blocksweep_*`, `Matrix_ratesweep_*`, `Matrix_robust_*` registered through `GF_TEST`.

## 9. Dependencies and build order
Needs the test-runner fix already on master (zero-test failure), F0-7 TestRig for the best sweeps (an interim version can use `TestSignals` directly). Land after E-4. Feeds W-05, L-02, M-02 and release job A5.

## 10. Effort and risks
~4 ideal days. Risk: runtime growth; keep a fast tier for PRs and a full tier for nightly/release. Risk: evidence files mistaken for host QA; mitigated by the explicit `unverified` rows.

## 11. Acceptance checklist
- [ ] `run_matrix` green on Linux and Windows CI; evidence artifacts uploaded
- [ ] Self-test proves a zero-test step fails the run
- [ ] Audit matrix items 2-6 each map to a step name in `TEST-TRACEABILITY.md`
- [ ] Unverified rows listed in the release notes template
