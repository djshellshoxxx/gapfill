# 14 - Consolidation and completion prompt (cache-stable)

Snapshot taken 2026-11-04 against `origin/master` `cea17ff`. It replaces nothing: 13 (FIN-0..FIN-8),
`docs/progress/completion-plan-20261031.md` and `docs/progress/DETAILED-REMAINDER-AUDIT-20261031.md`
stay the sources of truth. This file pins the branch state and the order of work.

## How to use (prompt caching)

- **Part A** is a static prefix. Paste it verbatim, or say "Run docs/dev/14 Part A". Do not edit it
  between sessions: any byte change before the tail invalidates the cached prefix.
- **Part B** is the only text that changes. Fill in the two lines and append them after Part A.
- Every subagent brief starts with the same fixed line, `CTX: read docs/dev/14 Part A + AGENTS.md`,
  and puts its task-specific text last. A shared prefix is reused from cache; a reordered one is not.
- In a Workflow or API script, keep `system` + tool list + Part A fixed and put `cache_control`
  on the last block of Part A. Put changing ledger state after it, never before.

---

## Part A - static prefix

```text
ROLE: GapFill lead (JUCE/C++20 audio plugin). Finish consolidation, then complete the ledger to 1.0 RC.
Work autonomously; stop only for the OWNER items below. Report in caveman style (skills/caveman).
Subagents: Haiku/Sonnet for searches, docs and mechanical edits; strongest model for DSP, state,
concurrency and hot files. One owner per file set. Briefs use TASK|SCOPE|CONSTRAINTS|RESULT|EVIDENCE|BLOCKERS.

RULES (override everything else): AGENTS.md + docs/dev/13 "Hard rules" 1-8. Short form:
append-only param IDs (tools/param_ids/*.lock, first 66 host indices fixed); goldens/fixtures stay
bit-identical unless the PR is titled [golden]; audio thread does no alloc/lock/IO/log/GUI;
never weaken/skip a test or add an unexplained exemption; hot files need written justification +
hot-file-ok label; refactor PRs are separate; never force-push or rewrite others' branches; closing PRs/issues,
deleting branches, releasing and signing are OWNER actions (propose, wait for yes).
Every PR is a draft with a "Not verified" section. Hosts, hardware, Windows/macOS interactive
use and listening stay UNVERIFIED until a person records evidence.

READ ONCE, BY RANGE, DON'T PASTE: docs/dev/13 (FIN-*, owner decisions, DoD);
docs/progress/completion-plan-20261031.md (ledger rows, waves, evidence-invalidation rules);
docs/progress/DETAILED-REMAINDER-AUDIT-20261031.md (packages + acceptance numbers);
docs/dev/features/IMPLEMENTATION-STATUS.md; tools/audit/completeness_audit.py.
Use grep, then sed -n ranges. Write scratch notes to docs/progress/SESSION-NOTES.md (decisions, working
commands, pitfalls) and read that file before re-deriving anything.

STATE @2026-11-04 (re-check SHAs first, then trust them):
- master cea17ff. integration/gapfill-completion-20261031 2c36731: 226 commits ahead of master,
  5 behind; `git merge-tree` shows no conflict. Draft PR #32 carries it.
  #32 preflight FAILS: tools/test_completeness_audit.py lists 29 gaps:
  help_order 1230 collision (Macros and modulation / Detector MIDI output); no lock file for TAPEXTRA;
  fm08_detail/fm08_scan (CAPTUREEXT) missing from the GUI; fl21_{mode,mix,source,hold,length,preDelay,
  damp,decay,stereo} (L21EXT) with no ParamInfo and no GUI; fm18_{on,unit,first} (BATTLEV2) the same;
  battlev2 source fragment with no test fragment.
- PR #33 (consolidation/source-snapshot-20261102): adds only .github/workflows/consolidation-snapshot.yml.
  Disposable once #32 merges. PR #27 (codex/windows-release-gapfill-20261021, not a draft, Oct 21)
  conflicts with the merged baseline in 9 files, including PluginEditor.*.
- Fully absorbed (no unique content vs master+integration) -> propose deletion: audit-fixes-20261005,
  chatgpt/qa-zero-tests-guard-20261020, claude/{busy-brahmagupta-n1l92x,epic-wozniak-e1pvza,
  inspiring-fermi-pv9gv5,intelligent-wozniak-btrtc6}, codex/gapfill-creative-features-spec,
  codex/m0-baseline-acceptance-20261030, consolidation/wiki-reconcile-20261102, docs/readme-and-wiki,
  fix/windows-pluginval-diagnostics-20261019, full-audit-fixes-20261006, keyboard-host-validation-20261006,
  policy/modular-code-regression-safe-20261013, preset-loader-hardening-20261006, reduced-motion-20261006,
  spec-compliance-20261006. Keep backup/* until 1.0.
- Stale but unique (Oct 11-21, older than squash 4e6c945; conflict files in brackets):
  build/gated-alpha-release-matrix-20261019 [ci.yml]; build/macos-native-vst3-au-standalone-clap-20261019 [ci.yml];
  fix/reconcile-pr9-12-14-20261019 [ci.yml]; fix/release-packaging-20261011 [ci.yml];
  qa/release-artifact-selection-20261013 [ci.yml]; fix/windows-release-packaging-path-20261019 [ci.yml,
  tools/test_package_release.sh]; claude/serene-hawking-q9gclq [ci.yml, docs/dev/01-07, README, tools/dev/check.sh];
  chatgpt/incremental-build-targets-20261020 [09-FAST-INCREMENTAL-BUILDS.md, tools/dev/build-target.sh];
  codex/fast-modular-build-slices-20261020 [ci.yml, CMakeLists, PROGRESS, TestFramework.cpp, docs/dev 05/08/README];
  docs/developer-guides-ui-plan-20261021 [COMPLIANCE, TEST-TRACEABILITY, adr, MODULES, docs/dev 09/10/11/README,
  build-target.sh]; docs/feature-specs-20261021 [features/DECISIONS.md]; docs/live-creative-effects-spec-20261020
  [docs/dev/README]; plan/stabilize-modularize-deferred-backlog-20261019 [08, README];
  feature/gui-collapsible-layout-20261019 [PluginEditor.cpp, gui/Panels.cpp];
  fix/transport-state-audit-20261012 [PluginProcessor.*] (has a non-Apple from_chars fix + transport audit).
- Issues: #11 (runners restored? CI runs again -> propose close with run-id evidence); #13 = W-01.

PHASES (stop at a gate only when it fails; otherwise continue):
P0 Verify (no edits): fetch; re-run the merge-tree and absorbed checks above; build target GapFillTests
   (see docs/dev/09; cmake --build --parallel N, N = RAM/2GB) on integration; record pass counts.
P1 Land the baseline: on integration, close the 29 audit gaps in the smallest correct way (add ParamInfo
   entries, GUI controls or documented table reach, the TAPEXTRA lock via tools/param_ids, a unique Help order,
   a battlev2 test fragment). Do not add waivers. Merge master into integration (no conflict expected),
   run preflight + full suite + the fast QA tier, and get #32 green. OWNER: merge #32 (merge commit, no squash).
P2 Salvage the stale branches: for each one, `git diff <merge-base>..<branch>` and keep only behaviour or docs
   that master lacks. One small PR per theme: (a) CI/release workflow deltas -> one PR against the current
   ci.yml, following the user's CI speed rules (concurrency, paths-ignore, --target, explicit --parallel,
   heavy macOS jobs only on main/nightly); (b) build-target docs/scripts; (c) transport/from_chars ->
   check against TestTransport, add a regression test, hot-file label; (d) GUI collapsible + #27 compact UI
   -> only after the T-02 screenshots, otherwise record a deferral. A PR goes up only if its diff is
   non-empty. List every branch with its disposition (merged-into-PR / superseded-by-<sha> / dropped:
   reason). OWNER: approve branch deletions and closing #27 #33 #11.
P3 Correctness first (DETAILED-REMAINDER-AUDIT "Immediate"): TAIL, EVENT, STATE-GRAPH (TSan),
   CORE-CALLBACK. One PR each with the acceptance tests the audit names.
P4 Capability packages, in the audit's table order, gated by dependencies: CV, BANDS(+BAND-PHASE),
   DELAYS, ENERGY, QUALITY, CRUSH, RESONATOR, FREEZER, DUEL(+AB-MODEL/MATCH shared loudness), SAFETY,
   LIKE, PHRASE, SIDECHAIN, EVOLVE, SCALE, MISTAKES, SCRUB, STORM, ROULETTE, PRESET-QUANT, EXPORT,
   PERSONALITY, BREATH, RATCHET, INFO, MIDI-AUDITION, REASONS, FX-CONTRACT, F-L17 pin menu, plus the
   completion-plan rows that are still open (FIXTURE, STATE, SCENE, FORMANT, COOP, TIMELINE, TASTE,
   SKIP, PC, MIDI-OUT, ROUTING, W03, ARRANGE, CLOUD, HARMONIC, KEY, I18N, REMAINDER).
   Each package is opt-in or versioned, the legacy path is exact (test it), new IDs go after FM10 in
   GROUP_ORDER, and its pure module/tests come first with the hot-file hook last and serialized.
   Parallelize only packages that share no file. At most 3 open PRs.
P5 Assurance: FIN-2 fixtures, FIN-3 nightly (ASan+UBSan, TSan, full QA tier, perf with
   GAPFILL_PERF_ENFORCE=1), FIN-5 soak, PERF/ANALYSIS. Follow the completion-plan rules on evidence
   invalidation.
P6 Release: FIN-7 packaging/provenance (signing waits for OWNER secrets), FIN-1/DOCS-RC truth pass,
   FIN-8 human acceptance pack. Update IMPLEMENTATION-STATUS and the audit rows from production wiring only.

OWNER (ask once, batch, never decide): 13 "Owner decisions" 1-6; the scope conflict: 13 says F-L20
per-voice formant, F-X06 cross-process co-op, F-M03 growth and Program Change are post-1.0, while the
completion-plan says the 2026-10-31 assignment includes them (completion-plan governs until the owner
answers); merge #32; deletions/closures from P2.

DONE = 13 "Definition of done (1.0 RC)" + every audit/ledger row closed from evidence or owner-deferred.

PER-PR REPORT (caveman, <=10 lines): ticket | PR link | CI run id + result | tests added |
hot files | not verified | next. After each PR, update the PROGRESS.md checklist (P0-P6, owner items).
```

## Part B - variable tail (edit only this)

```text
RESUME AT: P0
LAST STATE: none (first session)
```
