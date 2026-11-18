# Release continuation (2026-11-18, full-assurance repairs for 5667f8a)

- Candidate stays `integration/gapfill-release-20261116`, draft PR #40. It now also
  contains #35 (docs/dev/14). With that, #32/#35/#36/#37/#38/#39 are all ancestors of #40.
- Evidence came from the run 43886723865 artifacts (per-test logs in `qa-matrix/*-logs/`
  and `sanitizer-evidence/*.log`), not from the job logs. The job logs only print the matrix.

## Failures on 5667f8a and their causes

| Job | Failure | Cause | Fix |
|---|---|---|---|
| Linux MIDI-ON | `MidiProcessor.declared_output_never_echoes_input...` | 5667f8a stopped clearing input when output is compiled in but both modes are off. F-L11 spec line 33 requires the clear. | Restored `kDeclared` in `beginMidiOutput`. The older WP10 test `plugin_off_..._keeps_the_incoming_cc` now asserts `ccSurvived == !kDeclared`. |
| Windows | `Effective.feature_hooks_leave_default_parameters_neutral` | `gatherContext` does `g.snap = {}`. MSVC copies the temporary's indeterminate tail padding (offset 360, after `GrooveSnapshot::valid`). | `GrooveSnapshot` gets a named zeroed `reserved[3]`. The size and layout are unchanged. |
| Windows | `Taste.corrupt_oversized_or_partial_library...` | `/proc/...` can be created on a Windows drive. | The unwritable target is now a path below a regular file. |
| Windows | `test_i18n_catalog` inventory | `sorted(WindowsPath)` ignores case. | `build_catalog.source_order` sorts by case-sensitive path parts. Has a regression test. |
| macOS Intel | AU not registered after 8 `auval -a` | Listing alone never triggered the rescan. | Discovery mirrors ci.yml's passing step: `killall -9`, then an `auval -v` probe per attempt. Every attempt's evidence is kept. |
| macOS arm64 | unit-all 1800 s timeout in `CoopRenderMatrix` | Stalled inside the 64/4-thread config, the only `std::barrier` user. The progress line prints *before* each config. | `Tests/support/TestSync.h` provides a mutex/condvar barrier and semaphore. `tools/test_test_sync.cpp` runs in preflight. |
| macOS, TSan, ASan | `EditorLifecycle` 600 s deadline | Release on 2 CPUs took 470 s: two unpaced spin loops starve the message thread. ASan progressed steadily (125 cycles in 574 s). | One yield per block and per parameter sweep. The child has a 120 s per-phase stall detector (mutation-checked: it fails with the phase name). The total deadline is x4 only under sanitizers (`TestTimeScale.h`). Rounds, deadlines in Release and assertions are unchanged. |

## Follow-up on deff8c4 (push run 44229039992)

- Windows full QA: every row passes (auval is macOS-only). macOS arm64 now finishes the whole suite: the CoopRenderMatrix stall is gone.
- Three macOS failures remained, and each is fixed in the next commit:
  - EditorLifecycle child printed nothing after its header. It never serviced its first callSync because `runDispatchLoop()` is `[NSApp run]` on macOS. `MessageLoopTestRunner` now runs `CFRunLoopRunInMode` until `hasStopMessageBeenSent()` (macOS only). The stall detector now also covers the first callSync.
  - `Golden.alloc_guard_counts...`: Apple clang elides a matching new/delete expression pair. The probe now calls `::operator new[]` directly.
  - `Evolve.random_access_seek...`: memcmp across stack padding. The helper zeroes the storage and constructs in place; snapshots are copied with memcpy.
- None of these is reproducible on Linux. All three need the hosted macOS run.

## Local evidence (Linux, GCC 13, Release, MIDI_OUT=ON)

- Focused groups pass: MidiProcessor 3, MidiOut 15, Effective 14, Taste 24, CoopRenderMatrix 1 (19 s).
  EditorLifecycle passes on 2 CPUs in 241 s (was 470 s).
- Preflight (validate_repo, tooling unittests, completeness audit, layout, i18n --check) passes.
  TestSync passes normally and under GCC TSan.
- Full suite (x11_test_session, Xvfb): 1709 passed, 0 failed.
- NOT verified here: Windows, macOS, and the TSan/ASan full builds. They need the hosted push run
  on the new head. The macOS fixes are reasoned from artifacts and are not reproduced.

---

# Release continuation (2026-11-16, native QA repairs)

- Candidate: `integration/gapfill-release-20261116`, draft PR #40; next tag
  candidate `v0.0.2-beta2`. No new release or merge has been performed.
- Consolidates #32/#36, latest Glitch `34aada5`, CI speed `01a0671`, and build
  target tests `a87b1b0`. The portable worker fix at `43c1c6f` now builds on both
  Apple architectures. Existing fixtures/goldens are preserved.

## Hosted evidence before these repairs

Push run [43843843236](https://github.com/djshellshoxxx/gapfill/actions/runs/43843843236)
on `43c1c6f` failed full assurance even though ordinary Linux/Windows/macOS
builds, smoke tests, plugin validators and package checks passed. Linux full
unit tests, MIDI-OFF native QA, performance and 300-second soak also passed.

- Windows full QA exited `3221225725` (stack overflow) in the 2,000-level state
  nesting regression. Preflight also had UTF-8 and Windows binary-path failures.
  EVENT drawing and two settings-identity assertions failed before the crash.
- Linux MIDI-ON: 1,705 passed, three failed (two stale MIDI declaration assertions
  and real loss of incoming CC while both MIDI output modes were disabled).
- macOS arm64/x86_64: full suite timed out at `CoopRenderMatrix` (1,800 seconds);
  editor lifecycle child exceeded 600 seconds. Intel AU discovery was absent.
- TSan stopped inside JUCE's timed condition-variable wait with GCC 11.4's missing
  `pthread_cond_clockwait` interceptor (GCC PR 101978); no project threading test
  completed. ASan+UBSan executed the suite but failed the editor lifecycle deadline;
  its report contains no address/undefined-behavior diagnostic. Neither is a pass.

## Repairs in this continuation

- State and preset loading reject more than 64 nested XML elements and DTD/entity
  declarations before JUCE's recursive parser. Binary states validate canonical
  UTF-8 first. Rejection leaves the session unchanged. See
  `Source/features/state/README.md` and actual-processor regressions in `StateQa`.
- Disabled MIDI output preserves incoming MIDI/CC, while active modes and old-note
  flushes retain ownership. Declaration tests now cover the selected build option.
- Native drawing is flushed before pixel inspection; no-op settings tests snapshot
  the same object's representation instead of comparing unrelated padding bytes.
- Tooling reads catalogs as UTF-8 and quotes the test-binary path correctly on
  Windows. The generated i18n inventory is refreshed; catalogs are unchanged.
- Sanitizer jobs select GCC 12 and run a timed-wait runtime smoke test before their
  full build. All sanitizer diagnostics, leak checks and workload selections stay
  enabled. AU QA requires bounded discovery of the built bundle's exact identity
  before running the existing real `auval -v` check.
- Co-op matrix phases and every 25th editor lifecycle cycle report progress for
  timeout diagnosis. Bar counts, stress rate assertions and deadlines are unchanged.
- GUI snapshot matrix now includes Glitch at minimum/large sizes and 100/150/200%.

## Local evidence and remaining work

- 109 tooling tests pass. XML resource-limit tests pass normally and with
  ASan+UBSan; the timed-wait smoke passes normally and under local GCC 14 TSan.
  Local ASan leak detection is disabled because this workspace uses ptrace;
  hosted leak detection stays enabled.
- Repository validation (362 stable IDs), completeness audit (zero findings),
  layout checks, i18n freshness, packaging fixtures, CI/build helpers and workflow
  syntax pass. The standalone portable-worker regression remains in preflight.
- `tools.qa.nightly_test`: 19 pass, one environment failure creating an ephemeral
  GPG key because this workspace cannot launch gpg-agent's Unix socket. Not skipped
  and not reported as a pass; hosted preflight must verify it.
- Native Linux MIDI-ON Release compilation is in progress. The workspace exceeded
  memory limits with four and then two compiler jobs; resumed with one. No complete
  native result for these repairs is claimed here.
- Require a successful push CI/full-assurance run on the exact new head. In
  particular, the macOS co-op and editor stress timeouts are still unresolved.
  Do not merge or publish based solely on ordinary build/validator success.
- Real DAW/listening, signing/notarization and interactive accessibility remain
  unverified. Authenticated release workflow dispatch is still needed after green
  gates; the GitHub connector has no dispatch action. Keep release-beta.yml's
  exact-SHA, artifact and package-validation requirements.

---

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
