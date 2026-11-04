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
- JUCE/clap-juce-extensions are gitignored clones; a second worktree can symlink them.
- PROGRESS.md is a hot file; hot rule only bites ticket-titled (E-1:, F-L07:) or `feature`-labelled PRs.
