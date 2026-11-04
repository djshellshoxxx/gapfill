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
