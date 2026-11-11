# Handoff (2026-11-07, updated 2026-11-11)

## 2026-11-11 wind-down (GapFill paused by the owner, relayed by the coordinator session)

- Fixed on this branch since 2026-11-07 (commits 8970449, a181043, 745fa96, 1b78831):
  - `preset_files_round_trip_through_disk` / `fg01_filtMax`: juce's skew snaps only in convertTo0to1, so the
    fresh raw value (7999.9995) and a file load (8000) differed; glitch ranges now snap in both directions.
  - `IntegratedStress::soak_many_seeds` seed 5003 (22.05 kHz, random <= 8192, stereo+sc+wet) and seed 1016
    (192 kHz, 8192, mono+sc+wet): Wet-bus output > +6 dBFS with the Glitch rack OFF and `wetBusLevel` fuzzed to
    +9.9 / +10.6 dB (the F-L12 trim sits after the -1 dBFS Wet Safety limiter). The driver now caps
    wetBusLevel at 0 dB like Output; the 2.0 bound is unchanged. 5/5 consecutive group runs pass.
  - Rack gain staging (resonance/feedback compensation, soft ceiling; whole-output contract test).
- Still open: the soak is not run-to-run deterministic (same binary: peak 2.43 then 1.00 for seed 5003);
  likely wall-clock driven code (Quality Auto CPU governor or co-op timing) - unverified, not investigated.
- Real hosts, listening, screenshots, release artefacts: not done.


Read `docs/progress/SESSION-NOTES.md` first (decisions, working commands, pitfalls); this file adds the 2026-11-07 pass.

- Latest pushed commit: see `git log -1 origin/claude/zealous-gauss-7b4omr` (PR #39 -> `claude/stoic-brown-92wmta`).
- Open PRs: #32 (integration -> master), #35 (docs), #36 (P1 baseline), #37/#38 (stacked on #36), #39 (this work).
- Done this pass: root cause + fix of the 4 hosted-CI failures (FMA contraction, commit 74bb8ac, cherry-pickable onto
  #36); F-G01 Glitch rack (spec `docs/dev/features/F-G01-glitch-rack.md`, DECISIONS 19-21).
- Known failures: none locally after 64d2786. CI on #39: check the latest run.
- Next actions, in order:
  1. Owner: cherry-pick 74bb8ac onto #36 (or merge #39 first), add `hot-file-ok` to #39, then the merge order in
     `CONSOLIDATION_PLAN.md`.
  2. T-02-style screenshots of the GLITCH tab at 100/150/200 % (`GF_SCREENSHOT_DIR`, Xvfb).
  3. P3 correctness packages from SESSION-NOTES (TAIL branch ready locally in the old session; EVENT, STATE-GRAPH,
     CORE-CALLBACK).
  4. Check the hosted toolchain's default `-march` in the release job (portability of Linux binaries).
- Resume commands:

      git fetch origin && git checkout claude/zealous-gauss-7b4omr
      bash tools/ci_fetch_deps.sh
      cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
      cmake --build build-dev --parallel 4 --target GapFillTests
      xvfb-run -a -s "-screen 0 6000x4000x24" build-dev/artefacts/GapFillTests Glitch
      xvfb-run -a -s "-screen 0 6000x4000x24" python3 tools/qa/x11_test_session.py build-dev/artefacts/GapFillTests

- Pitfalls found this pass: editing a header while ninja builds produces ODR-mixed objects (segfault in the
  processor constructor) - use a separate worktree for parallel builds; skewed continuous ranges need an interval to
  resave byte-identically; `grep -o` in an `&&` chain succeeds even when it prints a stale list.
