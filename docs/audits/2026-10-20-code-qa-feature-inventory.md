# GapFill code, specification and QA audit — 2026-10-20

## Audit baseline and scope
- Baseline: `master` at `20e5ffcaada9768f2f037d03be4dfeb0c99c6288`.
- Reviewed: repository tracked-file tree, core audio processor transport path, factory presets, CLI test runner, repository preflight, CMake, CI workflow, current development specs and open issues/PRs.
- Last observed successful master CI: run 39331947502. Published beta: `v0.0.1-beta1`; assets listed for Windows x64, Linux x64, macOS arm64 and x86_64.
- **Limit:** This is a targeted code audit and checklist, not an exhaustive line-by-line review of the entire DSP/UI implementation. No new native build, real host/plugin validator run, GUI screenshot inspection, hardware MIDI test, or audible comparative render was performed during this audit. This environment cannot clone from github.com.

## Definite QA defect identified and corrected on this branch
- `Tests/TestFramework.cpp` accepted an unmatched test filter as zero failures. The local gate requests `-Combinatorial`, but the runner treated it as an inclusion substring and therefore skipped every test while returning success. The correction supports `-filter` exclusion and returns nonzero when no test executes.
- Verify the fix on native CI by running `GapFillTests -Combinatorial`, `GapFillTests Plugin`, and `GapFillTests nonexistent_filter_should_fail` with required counts and expected nonzero final exit code for the final invocation. Do not consider this proven until the CI jobs execute.

## Known unresolved bug and engineering risks
1. **Transport prediction**: `updateTempoInfo` predicts the next host PPQ using the current subchunk's `numSamples`, rather than the previous whole callback length. Variable host callback sizes or internal chunking can induce false jump detection; see issues #10 and PR #12. Coordinate with the overlapping PR; do not duplicate/refactor it blindly. Require a failing alternating-buffer/seek test then green native DSP and host validation.
2. **Windows GUI automation**: issue #13 describes a pluginval editor automation crash. Current successful workflows do not establish that this exact reported crash is reproduced and resolved across real host configurations. Collect stack and editor lifetime stress evidence.
3. **Stale PRs**: PR #9 and #12 remain open despite new release work on master. Compare patches against master and explicitly close or reconcile to avoid accidental regression or duplicate changes.
4. **Documentation drift**: `docs/COMPLIANCE.md` still claims GitHub runners cannot start (as of 2026-10-06), although the October 19 CI completed successfully. Refresh historical notes in a separate docs-only change.
5. **QA visibility**: existing CI runs tests and validators, but the inspected source does not establish verified real DAW projects, audible golden references, user-device MIDI handling, crash dumps, or memory-allocation tracing. Log these as acceptance gaps, not as defects proven in code.
6. **Build-performance validation**: PR #21 adds targeted incremental build tooling, but that developer convenience is independent of release readiness and must pass CI before merging.

## Feature inventory against specifications
The following are **specified but not verified as implemented**. No implementation claim should be made solely because they are listed in developer docs:
- Essentials: E-1 Fill Audition, E-2 full tempo-sync acceptance, E-3 fill suppression explanations/legend.
- Creative: effective-value display, fill personalities, macro performance mappings, controlled mistakes, fill envelopes, fill lock, density breathing, musical phrase logic, evolution, per-band type, Capture v2, and More Like That.
- Usability: fill history/recipes, stem export, audition MIDI trigger, advanced preset browser, simple/advanced UI, auto-calibration, first-run tutorial, UI scaling, information/status line, and phase-based preset quality review.
- Already present in production code/release notes: factory presets (17 including Init), Capture v1, Surprise, Undo, Tape Stop, JUCE VST3/Standalone and conditional CLAP. Do not reimplement existing features without a narrow change request.

## Required QA matrix before any successor beta release
1. Run repository preflight and its Python unit tests, shell lint and packaging fixture.
2. Execute full JUCE suite (include Combinatorial); record number of executed, passed and failed cases.
3. Vary actual host block lengths 32, 64, 512, 4096, 8192; include internal subchunking and transport seek/loop/stop/start.
4. Verify deterministic same-seed/PPQ output and golden audio identity against `v0.0.1-beta1`; verify legacy state/preset round trips.
5. Stress 30+ create/open/close/destroy GUI cycles during automation and state/preset changes; capture crash traces.
6. Check denormal, NaN/Inf, silence, nonstandard sample rates, sidechain loss/recovery, latency/bypass and out-of-bounds output.
7. Native Windows/Linux/macOS (arm64 and Intel) build and validation: VST3 pluginval level 10, CLAP validator, macOS AU auval, standalone smoke; run real-DAW sessions on each available OS. Preserve unsupported cases as unverified.
8. Inspect installer/archive contents, code signatures where applicable, source SHA and SHA256 asset checksums; publish only successful, validated native builds.

## Release decision
**NO-GO for a new release as of this report.** A test-runner correctness fix has been committed for CI evaluation, but the full release QA matrix and host evidence have not been established. Never retag the published beta or create a new public release from an unverified PR. 
