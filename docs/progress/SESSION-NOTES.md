# Session notes (scratch: decisions, working commands, pitfalls)

Read this before re-deriving anything. Newest first inside each section.

## Decisions
- 2026-11-04 P1: audit gaps closed on branch claude/stoic-brown-92wmta (based on integration 2c36731, master cea17ff merged).
  - TAPEXTRA lock = rename fm20.lock -> tapextra.lock (audit keys locks by macro suffix; validate_repo keys by
    header stem and only for headers whose stem == macro, so fm20.h is NOT checked by validate_repo; follow-up:
    rename fm20.h/.cpp -> tapextra.* in a separate refactor PR).
  - L21EXT (fl21_*) and BATTLEV2 (fm18_*) are registered host params with NO engine wiring (CV / BATTLEV2 P4).
    GUI rows drawn disabled (`ControlDef::inactive`, BattleCard reserved row) + ParamInfo "not active yet".
    Flip them on in the package that wires the engine.
  - Help order: Detector MIDI output 1230 -> 1235 (F-M23 doc pins Macros and modulation at 1230).
- i18n: pseudo catalog 270 KB > 256 KiB cap on integration (en 192 KB; +40% pseudo makes the cap unreachable).
  Spec value (doc 12 F-P04, i18n README, Catalog.h, tests) -> OWNER decision. Hidden behind the audit failure
  in #32 preflight because CI step order runs test_completeness_audit first.

- P0 baseline (integration 2c36731 + GCC13 test fix, full suite under Xvfb): 1666 passed, 6 failed:
  Audit.feature_parameters_are_never_randomised, Foundations.help_registry_returns_sections_sorted_by_order,
  ModulationPipeline.opt_in_decision_targets..., ParameterCompatibility.detailed_feature_tail_order...,
  StackBudget.big_objects_stay_small_by_value (engine 105304 > 96 KiB, processor 248368 > 192 KiB),
  Info.every_parameter_has_info. All six addressed on PR #36 (help order, ParamInfo, 3 stale tests, heap-owned
  HoldVoices + LiveControl). #32 CI never reached unit-tests (preflight failed first), so none were visible.
- Doc 12 C9 (no post-66 param randomised) vs F-X01 contract (Random All draws fx01_motion/scatter/tone while
  fx01_mode on): the contract governs; the Audit test encodes both. Flagged to the owner.
- P3 triage: TAIL partial (legacy wired; parallel max unused because no parallel return exists yet; processor
  tests missing: 16 s Dub, off ramp, mono, rates, concurrent queries). EVENT open. STATE-GRAPH partial (Lock only;
  scenes/macros serialisers read live models). CORE-CALLBACK open (diagnostic note, reproducer outside the repo).
- Session may push only claude/stoic-brown-92wmta; P2 (a)/(b) live on local branches p2/ci-speed, p2/build-target-tests.

## Working commands
- deps: apt-get install ccache xvfb libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev
  libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev
- configure: cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
- build+test: JOBS=6 bash tools/dev/build-target.sh build-dev tests -Combinatorial   (16 GB box -> 6 jobs)
- preflight: python3 tools/validate_repo.py; python3 -m unittest tools/test_validate_repo.py tools/test_check_hot_files.py
  tools/test_completeness_audit.py tools/test_qa_matrix.py tools/test_check_layout_literals.py tools/test_x11_test_session.py;
  python3 -m unittest tools.qa.nightly_test tools.test_release_pipeline tools.test_i18n_catalog;
  python3 tools/audit/completeness_audit.py; python3 tools/check_layout_literals.py
- i18n regen: python3 Source/features/i18n/build_catalog.py  (--check to verify)
- absorbed-branch check: merge-tree --write-tree <int-or-master> <branch> == that ref's tree.

## Pitfalls
- GuiQa needs a display: run the suite as `xvfb-run -a -s "-screen 0 6000x4000x24" python3 tools/qa/x11_test_session.py <bin>`.
- `pkill -f <pattern>` inside a bash -c whose command line contains the pattern kills itself (exit 144).
- sizeof probe without linking: `template<size_t N,int T> struct Sz; Sz<sizeof(X),1> a;` + -fsyntax-only with the ninja command.
- JUCE/clap-juce-extensions are gitignored clones; a second worktree can symlink them.
- PROGRESS.md is a hot file; hot rule only bites ticket-titled (E-1:, F-L07:) or `feature`-labelled PRs.
