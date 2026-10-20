# 09 - Fast, target-selective developer builds

Status: approved implementation slice for the local developer workflow. This
spec changes build selection and verification commands; it does not change DSP,
plugin formats, release gates, or compiler settings.

## Problem

`tools/dev/check.sh` currently builds `GapFillTests`, VST3, Standalone, and CLAP
before running any focused test filter. A developer changing one area therefore
waits for unrelated targets to link. The release workflow still needs the full
matrix, while normal iteration needs a reliable way to request only the target
that exercises the current change.

The current build topology has these separately named JUCE targets:

| Deliverable | CMake target | Availability |
|---|---|---|
| Unit / integration test runner | `GapFillTests` | All supported build hosts |
| VST3 | `GapFill_VST3` | Windows, Linux, macOS |
| CLAP | `GapFill_CLAP` | When the pinned CLAP extension is present |
| Standalone | `GapFill_Standalone` | Windows, Linux, macOS |
| Audio Unit | `GapFill_AU` | macOS only |

## User-facing commands

`tools/dev/build.sh <build-dir> <component>` builds one target slice. Components
are `tests`, `vst3`, `clap`, `standalone`, `au`, and `all`. `all` builds every
configured deliverable and remains the release-oriented choice. The script
must fail with a direct explanation when the build directory is unconfigured,
the requested target is unavailable, or the parallel-job count is invalid.

`JOBS` bounds parallel compilation and defaults to 2. It is passed through
`cmake --build --parallel` so the same command works with Ninja, Make, Visual
Studio, and Xcode generators. CMake documents `--target` as target selection
and `--parallel` as a maximum job count; both are supported by this project’s
minimum CMake version (3.22):
https://cmake.org/cmake/help/v3.22/manual/cmake.1.html#build-a-project

`tools/dev/check.sh <build-dir> <test-filter> <component>` runs repository
preflight first, then builds the selected slice. Existing two-argument usage is
preserved: omitting `<component>` continues to build and run the default quick
suite. `tests` and `all` run `GapFillTests`; other components skip it. The
`preflight` component runs only repository checks and does not require CMake or
a configured build directory. Pluginval remains optional through `PLUGINVAL`
and runs only when VST3 is part of the requested slice.

Examples:

```bash
# Fast source/docs gate, no JUCE toolchain needed
tools/dev/check.sh build-dev '' preflight

# Iterate on the DSP/plugin regression suite without linking plugin formats
JOBS=2 tools/dev/check.sh build-dev Dsp tests

# Build only the plugin format under development
JOBS=2 tools/dev/build.sh build-dev vst3

# Build every target configured on this host
JOBS=2 tools/dev/build.sh build-dev all
```

## Correctness and compatibility requirements

1. The build helper must use CMake target names, not guessed artifact paths, to
   choose work. It must not alter generators, build type, compiler flags,
   plugin formats, or output directories.
2. An absent CLAP target is an error when `clap` is explicitly requested. In
   `all`, CLAP is included only when configuration exposes the target, matching
   the repository’s optional CLAP configuration.
3. An AU target is an error off macOS when explicitly requested. In `all`, AU
   is included only when configuration exposes it.
4. `check.sh` keeps the existing default test filter and optional VST3
   validation behavior. A target-only check must not falsely claim the full
   test suite passed.
5. CI and release workflows continue to build and validate the full set of
   configured deliverables. Target-selective local commands are an iteration
   aid, not a release gate or a way to skip a failing check before merge.
6. Builds remain incremental. The helper must not run `--clean-first`, delete
   build outputs, or reconfigure automatically.

## Verification

`tools/test_dev_build.sh` uses a temporary fake CMake executable and a synthetic
configured build tree to verify target selection, `all` expansion, missing
target errors, invalid component errors, and job-count validation without
requiring JUCE or a compiler. `Tests/TestFrameworkFilters.cpp` verifies include
and exclude filter semantics and that a zero-match filter fails the test
process. Every shell script also passes `bash -n`.

On a real configured build, compare elapsed time for the old full target list
with `tests` and a single plugin-format slice under the same warm build tree.
Record compiler, generator, job count, changed source files, and elapsed time
in the work session note. Do not promise a fixed percentage improvement: the
benefit depends on which targets are already built and which files changed.

## Follow-on modularization

This slice removes avoidable work selection; it does not yet split C++ source
ownership into shared object libraries. That change is separately gated by
Gate B in `08-STABILIZATION-MODULARITY-FEATURE-BACKLOG.md`: first characterize
plugin/test macro differences and JUCE module linkage, then extract only a
source group proven safe to compile once for both targets. Require clean
multi-generator builds and behavior tests before claiming a compile-time win.
