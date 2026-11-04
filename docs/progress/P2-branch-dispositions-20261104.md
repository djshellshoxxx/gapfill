# P2 stale-branch salvage and branch dispositions — 2026-11-04

Baseline: `3bb7948` (integration `2c36731` + master `cea17ff`). Method: per-file blob comparison against
the merge base, intent comparison for `ci.yml`, `git log -S` for superseding commits. Read-only; no
branch was changed. Deleting branches and closing PRs/issues are OWNER actions (proposed below).

## Fully absorbed (no unique content vs master or integration) → propose deletion

Verified with `git merge-tree --write-tree <ref> <branch>` == `<ref>^{tree}`:
audit-fixes-20261005, chatgpt/qa-zero-tests-guard-20261020, claude/busy-brahmagupta-n1l92x,
claude/epic-wozniak-e1pvza, claude/inspiring-fermi-pv9gv5, claude/intelligent-wozniak-btrtc6,
codex/gapfill-creative-features-spec, codex/m0-baseline-acceptance-20261030 (in integration),
consolidation/wiki-reconcile-20261102 (in master), docs/readme-and-wiki (in master),
fix/windows-pluginval-diagnostics-20261019, full-audit-fixes-20261006, keyboard-host-validation-20261006,
policy/modular-code-regression-safe-20261013, preset-loader-hardening-20261006, reduced-motion-20261006,
spec-compliance-20261006. Keep `backup/*` until 1.0.

## Stale but unique (older than squash 4e6c945)

| branch | disposition |
|---|---|
| build/gated-alpha-release-matrix-20261019 | superseded by ci.yml `build-macos` (macos-15 + macos-15-intel, `lipo -verify_arch`, `auval` retries) and CMakeLists AU-on-Apple. Its two docs are 1–3 line stubs. Delete. |
| build/macos-native-vst3-au-standalone-clap-20261019 | superseded (a subset of the branch above). Delete. |
| fix/reconcile-pr9-12-14-20261019 | superseded by ci.yml "Upload verified Linux/Windows release binaries". Delete. |
| fix/release-packaging-20261011 | superseded by release-beta.yml, package-validation.yml, packaging/release_pipeline.py and ci `package-windows`. Its pre-validation binary upload is dropped on purpose: the baseline uploads only validated binaries. Delete. |
| qa/release-artifact-selection-20261013 | superseded by ci `build-windows-x64` (exact artefact paths, pluginval Start-Process -Wait). Delete. |
| fix/windows-release-packaging-path-20261019 | superseded by packaging/package_release.sh:45 + tools/test_package_release.sh (run in preflight). Delete. |
| claude/serene-hawking-q9gclq | superseded by docs/dev/01-07, tools/dev/{bootstrap,check}.sh; PR template identical. Delete. |
| chatgpt/incremental-build-targets-20261020 | superseded by tools/dev/build-target.sh (adds `au`) and docs/dev/09. Delete. |
| codex/fast-modular-build-slices-20261020 | merged into P2(b): `tools/test_dev_build.sh`, retargeted at build-target.sh (local commit d40c6a0, pending branch permission). The rest is superseded or dropped: docs 10/11 → docs/dev/features + 12; build.sh duplicates build-target.sh; a historic release snapshot; the `matchesFilter` change would alter a lone "-" filter (it selects all tests today), so it is dropped as a behaviour change. Delete after P2(b) merges. |
| docs/developer-guides-ui-plan-20261021 | superseded (DEV-SETUP, TROUBLESHOOTING, bug-report.yml identical; COMPLIANCE, TEST-TRACEABILITY, adr, MODULES, docs/dev/10/11 evolved). Delete. |
| docs/feature-specs-20261021 | superseded by docs/dev/features/* (75/77 identical; DECISIONS.md and F-M20 newer in the baseline). Delete. |
| docs/live-creative-effects-spec-20261020 | superseded by docs/dev/10-SPEC-LIVE-CREATIVE-EFFECTS.md (a rename, 3 lines differ). Delete. |
| plan/stabilize-modularize-deferred-backlog-20261019 | superseded by docs/dev/08. Delete. |
| fix/transport-state-audit-20261012 | superseded by e2eebde, later refactored into features/transport/{ProcessorTransport.cpp,TransportTracker.h}. The baseline uses no floating-point `std::from_chars` anywhere (only a comment at PluginProcessor.cpp:1392 remains), and the hand-written numeric grammar plus the regression tests `Plugin.invalid_numeric_state_strings_are_rejected_atomically` and `Plugin.variable_host_buffers_do_not_trigger_false_transport_jumps` are present. Nothing to port. Delete. |
| feature/gui-collapsible-layout-20261019 | DEFERRED to T-02. Its design (editor-only Detection/Protection collapse, collapsible Mix header, `SideCollapseButton`, scrolling Weights viewport, `GapKnob` maxDial cap, docs/dev/gui-layout-iteration-20261019.md) predates the rewritten `resized()` and its hunks don't apply. Reimplementing hot GUI files without the T-02 screenshots at min/default/large sizes is forbidden. Keep the branch until T-02. |
| codex/windows-release-gapfill-20261021 (PR #27) | DEFERRED to T-02 / UI-0. It conflicts with the architecture: it retires SlotHost and deletes the Performance tab, Effects drawer, SIMPLE/ADVANCED, tour and footer status. Worth reimplementing later, after UI-0 and screenshots: `DisclosureTile`, the header MORE overflow below 1180 px, and a smaller 820×540 minimum size with migrated persisted-size clamps. Propose closing #27 unmerged; keep the branch until the reimplementation lands. |

## P2 PRs (prepared locally; pushing waits for permission to use extra branches)

- (a) `p2/ci-speed` 1a9ba56: macOS on push/dispatch only, explicit `--parallel` (measured run 41530807124).
- (b) `p2/build-target-tests` d40c6a0: build-target.sh regression harness in preflight.
- (c) none. (d) deferred, as above.

## OWNER proposals

Delete the 17 absorbed branches and the 13 superseded stale branches above (keep
feature/gui-collapsible-layout, codex/windows-release-gapfill and backup/*). Close #27 (unmerged, deferred
design), #33 (disposable once #32 merges), #11 (CI runs again: runs 41782301184 and 41530807124 succeeded).

## Approved for deletion 2026-11-04 (owner runs it; the session proxy may delete only session branches). Restore with: git push origin <sha>:refs/heads/<branch>

- `audit-fixes-20261005` a0b3d6a320e873d079e67c89d60b47945b9c58d5
- `chatgpt/qa-zero-tests-guard-20261020` 526a123fe1a0f78907692959ded2fc3ade2aea4f
- `claude/busy-brahmagupta-n1l92x` a384a7b15bd34192e121dc561238fce6f915c406
- `claude/epic-wozniak-e1pvza` 34e7c35d5ec6fcfc413697cb4cf35a145586dc93
- `claude/inspiring-fermi-pv9gv5` 560c2fe87637048c42da108b5e2a8c7085727663
- `claude/intelligent-wozniak-btrtc6` 09bba4afa6afa33e3145d0ccef09a20792beea53
- `codex/gapfill-creative-features-spec` 750cf4b50790d20e0311afc71a772974c2ada05c
- `codex/m0-baseline-acceptance-20261030` 7ddec085fa6846c19ae02730204383cd2dfe782d
- `consolidation/wiki-reconcile-20261102` d1f70478db4b528605c24e01639ba7bf36a134e8
- `docs/readme-and-wiki` 7aba7233798f6cf8b8e670db4629246070859303
- `fix/windows-pluginval-diagnostics-20261019` 346dc95b4d8c5aab2a9c0f0a6fa4cec70a2539db
- `full-audit-fixes-20261006` ae92d25e839e32aeb3ca81bef281c28af319100f
- `keyboard-host-validation-20261006` 99f1e6402bd2261d444d6d90be92e5f3fcd1d2d4
- `policy/modular-code-regression-safe-20261013` 62088d62d2e65a356fba525f8f9eb103fa4dc1b2
- `preset-loader-hardening-20261006` 85f05cc24e095992e1b7ce3b96ce9c7be976b2f0
- `reduced-motion-20261006` 7a192a8172d09a789553f4747d348b699bc83906
- `spec-compliance-20261006` 9eb426b003fe3199874f649e440b1ee539fa989a
- `build/gated-alpha-release-matrix-20261019` c4a8f13dedd35a40bdc0b148ebb93ca9274e2315
- `build/macos-native-vst3-au-standalone-clap-20261019` bd8bb06fe6c8f93fc080ab5f0a49d7cbeba9325e
- `fix/reconcile-pr9-12-14-20261019` 0db4beaa6555d04e7549cfe240f10da006aa2e99
- `fix/release-packaging-20261011` 566a211cf99774c551c489d1452c49a09765385b
- `qa/release-artifact-selection-20261013` 8bdeb186fdf27b21fc0cf7b8cc4824d1253ca60c
- `fix/windows-release-packaging-path-20261019` 71309db160746a4c913fc80d9958d3e1d1684344
- `claude/serene-hawking-q9gclq` 8ff570da1e66f11dbd3799a04fcf58e92a0d038d
- `chatgpt/incremental-build-targets-20261020` 504594ead30366116622d95cccdc14d8e0577fc2
- `docs/developer-guides-ui-plan-20261021` 0742553a2440a3ec1efad56dc4570ca51867b3f9
- `docs/feature-specs-20261021` 82f16d00094ce8ce333de9108cef14fd03553a72
- `docs/live-creative-effects-spec-20261020` 9fa05f4ea05cee0ccdfce29202053014d80da6b5
- `plan/stabilize-modularize-deferred-backlog-20261019` 3b8386220e4f0d1ab5f2afc8bc0cd1820a381b29
- `fix/transport-state-audit-20261012` e0300e89cf99b0b5813d642b8e02b52f2b83308a
