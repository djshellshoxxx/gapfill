# 13 - Finalization prompt (0.0.2-beta1 -> 1.0 release candidate)

Paste everything below the line into a new agent session on this repository, or tell the agent
"follow docs/dev/13-FINALIZATION-PROMPT.md". It is written from a review of master `e89aecd`
(2026-10-30). Phase 0 re-checks every fact below before the agent relies on it.

---

## Role and goal

You are the release engineer for GapFill (JUCE/C++20 gap-aware creative effect; VST3, CLAP,
Standalone, AU on macOS). Take it from **0.0.2-beta1** to a **1.0 release candidate**: milestone
M7 in `docs/dev/01-DEVELOPMENT-PLAN.md` §6 plus the checklist in `docs/dev/07-QA-AND-RELEASE.md` §5.
Do all the work a headless agent can do. For work that needs a person, a DAW, hardware or secrets,
write the checklist, the evidence template or the gated workflow step, and list it as unverified.
Never claim it is done.

**Feature freeze.** No new features or parameters until 1.0. Allowed: bug fixes, tests, CI, docs,
release engineering and behaviour-preserving modular seams. If the owner lifts the freeze, record
that in `docs/dev/features/DECISIONS.md` first.

Talk tersely in chat and to subagents (use the caveman skill if it is installed). Commits, PR
bodies and docs keep full precision.

## Hard rules (these override anything else in this prompt)

Read before editing: `AGENTS.md`, `docs/dev/06-ENGINEERING-STANDARDS.md`,
`docs/dev/01-DEVELOPMENT-PLAN.md` §2-5, `.github/pull_request_template.md`.

1. One ticket per PR, short-lived branch, draft PR, at most 3 open PRs. Refactors go in their own
   PRs, never mixed with fixes. Never force-push and never rewrite history on someone else's branch.
2. Neutral defaults: goldens (`Tests/golden/`) and the state fixtures (`Tests/fixtures/`) stay
   bit-identical unless a PR is titled `[golden]` and explains why. Never regenerate goldens or
   fixtures to make a test pass.
3. Parameter IDs are append-only (`tools/param_ids/*.lock`, `GROUP_ORDER`). The first 66 host
   indices never move. Preset/state formats change only through a versioned migration with tests.
4. Audio thread (`processBlock` and everything it calls): no allocation, locks, IO, logging or GUI.
   Add AllocGuard tests where relevant.
5. Never lower an assertion, skip or quarantine a test, or add a waiver to get green. Every
   `KNOWN_EXEMPTIONS` entry needs a recorded reason.
6. Hot files (`PluginProcessor.*`, `PluginEditor.*`, `FillEngine.*`, `FillVoice.*`, `Panels.*`,
   see 01 §3): touch only with a written justification and the `hot-file-ok` label.
7. Closing or commenting on PRs and issues, deleting branches, publishing releases and using
   signing secrets are outward-facing. Propose them and wait for the owner's yes.
8. Every PR has a "Not verified" section. Real hosts, hardware MIDI, audio devices, Windows/macOS
   interactive use and audibility are unverified until a person records evidence.

## Baseline (re-check in Phase 0)

- Release `v0.0.2-beta1` (pre-release) published 2026-10-21 from master `e89aecd`. CI run
  39676870132 is green: Linux full suite (unit, golden, combinatorial), Windows and macOS smoke
  subset, pluginval at strictness 10 on Linux, Windows (`windows-2022`) and macOS, clap-validator,
  auval, standalone smoke, Windows packaging dry run.
- `python3 tools/validate_repo.py`, `python3 tools/audit/completeness_audit.py` (0 failing,
  8 exempt) and `python3 tools/check_layout_literals.py` all pass.
- Tickets (`docs/dev/features/IMPLEMENTATION-STATUS.md`): 65 yes, 8 partial (F-L20, F-M03, F-X05,
  F-X06, Q-01, T-01, T-02, W-03), 1 no (W-01), 1 out of scope (P-01).
- Open PRs: #27 "Build and validate compact GapFill Windows beta UI" (not a draft, mergeable_state
  `dirty`, based on pre-merge master `83447a7`) and #28 "Add wiki pages and README wiki section"
  (draft). Open issues: #11 (Actions runners; `docs/COMPLIANCE.md` says this is resolved) and #13
  (W-01, the Windows pluginval editor-automation crash; Windows pluginval at strictness 10 now
  passes in CI).
- Missing: no `Tests/fixtures/state_0.0.2*` (only 0.0.1 fixtures exist), no `docs/qa/` evidence
  directory (no manual host script has ever been recorded), no scheduled or nightly workflow (07 §1
  requires sanitizers and perf nightly), no soak harness, no code signing.
- Stale docs: the `PROGRESS.md` header still says "Version: 0.0.1 beta", and its "Next up" section
  (2026-09-16) says 50 tests and an unfinished Linux build. In `README.md` the 0.0.1 "What's new"
  section comes before the 0.0.2 one.
- Biggest files (modularity debt): `PluginProcessor.cpp` (1600 lines), `PluginEditor.cpp` (1394),
  `FillVoice.cpp` (1338), `FillEngine.cpp` (1229).
- Known residuals (`docs/audits/2026-10-21-qa-report.md`): a small `RecordTap::allocate`-after-
  `cancel()` window; a TSan report inside `juce::dsp::Convolution` (JUCE internals);
  clap-validator's `state-invalid-random` warning (fail-soft by design).

## Phase 0 - verify the baseline (no edits)

1. `git fetch origin`. List open PRs, issues and branches, and read the latest master CI run (job
   list and conclusions). Note any drift from the baseline above.
2. Build locally (`docs/DEV-SETUP.md`, `tools/dev/bootstrap.sh`, then `tools/dev/check.sh`), or
   rely on CI if the machine cannot build JUCE. Record the test count and the result.
3. Run the three Python checks above and `python3 -m unittest discover -s tools -p "test_*.py"`.
4. Write a short Phase 0 summary in your first report. If something is red on master, fix that
   first, as its own PR, root cause only.

## Work packages (in this order; one PR each unless noted)

### FIN-0 Repository triage (proposals only)
- #27: diff it against current master and list what master already has (the compact UI landed
  via #25). Salvage any unique tests or fixes into a fresh branch from master, as a separate ticket.
  Propose closing #27 with that reasoning.
- #28: check the wiki text against current behaviour, then propose finishing it or closing it.
- #11: propose closing it, citing green CI runs.
- #13 / W-01: download the `windows-pluginval-diagnostics` artifacts from the last 3 or more master
  runs. If strictness 10 passed in all of them, update `docs/progress/W-01.md` and
  IMPLEMENTATION-STATUS to "not reproducible on master <sha> (evidence: run ids)" and propose closing
  the issue. If it fails anywhere, follow the W-01 protocol (procdump, symbols, a minimised stress
  test that is red first).

### FIN-1 Documentation truth pass (docs-only PR)
- `PROGRESS.md`: update the header version, replace the stale "Next up" with the current
  finalization plan, and add a session log entry. `README.md`: put the newest release first.
- Every "partial", "deviation" and "unverified" statement must match the code. Do not upgrade any
  status without evidence.

### FIN-2 0.0.2-beta1 state fixtures (compatibility for 1.0)
- Build the `v0.0.2-beta1` tag and write fixtures into `Tests/fixtures/state_0.0.2-beta1_*.bin`:
  every factory preset, a full session (all 216 parameters moved, scenes, the MIDI map, command map,
  history, recipes, Taste, Groove), a wet-only/routing session, and corrupt (truncated and
  bit-flipped) variants.
- Extend `TestStateFixtures`/`TestStateQa` so each one loads, renders finite audio and re-saves
  byte-identically. Do not touch the 0.0.1 fixtures.

### FIN-3 Nightly and release-evidence CI (`.github/workflows/nightly.yml`, triggers: `schedule` and `workflow_dispatch`)
- ASan+UBSan full suite. TSan on the threading, co-op, editor-cycle and worker tests (exclude the
  known JUCE Convolution report only by a documented suppression). Perf scenario E against
  `Tests/perf_baseline.json` with `GAPFILL_PERF_ENFORCE`, advisory at first.
- `tools/qa/run_matrix.py --binary <GapFillTests> --tier full`: upload the evidence JSON per SHA and OS (this closes the
  CI-job part of Q-01).
- Keep this out of the PR gate. Document it in 07 §5b and `docs/TEST-TRACEABILITY.md`.

### FIN-4 Close the repo-closable partials and residuals (one PR per ticket)
- T-01: the narrow-readout test, plus a Linux-only pixel golden of `LevelMeter` (per-platform
  images, with documented tolerance).
- T-02: an 8 px grid layout pass, driven by `TestGuiQa` screenshots (xvfb) at minimum, default and
  large sizes and at 100/150/200 % scale. Put before/after PNGs in the PR. The owner reviews them
  visually before merge. The ratchet baseline may only go down.
- `RecordTap::allocate` after `cancel()`: write a deterministic test that shows the window, then
  fix it (for example, the audio side acknowledges before reallocating).
- Leave F-L20, F-M03, F-X05 M1, F-X06 and W-03 (tolerance tightening) as recorded deviations or
  post-1.0 items. Only the owner can reopen them.

### FIN-5 Soak harness
- Add a headless soak tool: the processor with random automation, transport loops and preset
  changes. It checks RSS growth (at most 1 % over the run), stuck voices, non-finite output and
  level bounds. Run lengths are configurable.
- Hosted runners stop at 6 h, so either run 8 h in chained segments or document a local or
  self-hosted 8 h run. A 7-day soak (M7) is manual: write its procedure.

### FIN-6 Modular seams (separate refactor PRs, after FIN-0 to FIN-3, never blocking the release)
- Follow AGENTS.md rule 5: characterize first, extract behind a facade, keep the old entry point,
  goldens bit-identical, update `docs/architecture/MODULES.md`.
- Pick the smallest safe seam in `PluginProcessor.cpp` (state/undo or transport) and in
  `PluginEditor.cpp` (key handling or preset stepping). One seam per PR. Stop if coverage is thin
  and write characterization tests first.

### FIN-7 Release engineering for 1.0
- Signing: add Windows Authenticode and macOS Developer ID signing plus notarization steps to
  `release-beta.yml` (or a new `release.yml`). Gate every step on the secrets being present and skip
  with a loud message otherwise. Never commit secrets. Document the secret names for the owner.
- Use `shared/scripts/bump_version.py` and `generate_release_notes.py`. Check that the release
  notes list every user-visible change since 0.0.1 plus known issues.
- Verify release assets after download: SHA-256 list, bundle layout, then pluginval, clap-validator
  and auval on the packaged artefacts, not the build tree (07 §5).
- Do not tag or publish. Prepare a draft release checklist for the owner.

### FIN-8 Human acceptance pack
- Create `docs/qa/<version>/` with one evidence template per host and OS from the 07 §2 matrix
  (Reaper on all OSes, Ableton Live, FL Studio, Bitwig CLAP, Logic AU). Each holds the 07 §4 manual
  scripts, the `docs/VALIDATION.md` hardware steps, and every item from audit gap #14 (host keyboard
  shortcuts, screen reader, scale sizes, export drag, MIDI LED feedback, standalone devices).
- Add an "S1/S2 open bugs" triage table. The release is blocked while any row is open.

## Owner decisions to request (ask, never decide)

1. Version naming: `1.0.0-rc1`, or the 0.x betas from the 01 §6 milestones?
2. Accept ADR 0001 (no Free/Pro split) and 0002 (no AAX/AUv3/iOS), both currently "Proposed".
3. `GAPFILL_MIDI_OUT` in 1.0: OFF, as now, or ON after the host matrix?
4. Confirm that the F-L20 per-voice formant, F-X06 cross-process co-op, F-M03 editor growth and
   MIDI Program Change are post-1.0.
5. Signing certificates and the secret names. The 1.0 host list and who runs FIN-8.
6. Closing #27, #28, #11 and #13 (FIN-0 proposals).

## Definition of done (1.0 RC)

- [ ] Master CI is green at the RC commit, and the nightly has been green for 7 consecutive days.
- [ ] Every ticket is `yes`, or is a recorded deviation, out of scope or post-1.0 with the owner's
      approval. W-01 is closed with evidence.
- [ ] The 0.0.1 and 0.0.2-beta1 fixtures all load unchanged. The goldens are unchanged, or each
      `[golden]` change has been approved.
- [ ] Validators pass on the packaged, signed artefacts on Windows, Linux and macOS.
- [ ] `docs/qa/<version>/` is filled in by a person for at least Reaper plus one other DAW on
      Windows, with zero open S1/S2 bugs. The 8 h soak passes. The 7-day soak is recorded.
- [ ] README, PROGRESS, COMPLIANCE, VALIDATION, TEST-TRACEABILITY, MODULES and
      IMPLEMENTATION-STATUS are true at the RC commit.

## Reporting

After each PR, report: the ticket, PR link, CI result with run id, tests added, hot files touched,
what was not verified, and the next package. Keep a running checklist of FIN-0 to FIN-8 and of the
owner decisions in `PROGRESS.md`.
