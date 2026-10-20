# Repository baseline - 2026-10-20

This snapshot records the repository state used to select the fast-build work.
It distinguishes published build assets from manual host acceptance.

## Source and release state

- Default branch: `master`
- Source commit: `20e5ffcaada9768f2f037d03be4dfeb0c99c6288`
- Beta tag: `v0.0.1-beta1` at `8bf4078`
- The current history contains commits adding Windows/Linux release assets and
  native Apple Silicon/Intel macOS release assets. This confirms release
  artifacts were produced; it does not by itself prove every manual DAW,
  controller, or audio-device acceptance step.
- CI defines Windows, Linux, and macOS build jobs and release formats. The
  existing repository validates VST3, CLAP, and Standalone; macOS additionally
  builds AU.

## Build inputs

- Minimum CMake: 3.22
- Local verification tools: CMake 4.4.4, Ninja 1.13.2, GCC 13.3.0
- JUCE: 8.0.15
- `clap-juce-extensions`: `55525c9858d4b25687be7759a5e0f70eccef218e`
- Available memory: approximately 9.7 GiB; local compilation capped at two jobs.

The repository bootstrap configured successfully with these pinned dependencies.
The container did not provide CMake or Ninja on `PATH` initially; installing
the system packages through apt was blocked by its restricted setgroups helper.
The existing Python-installed CMake and Ninja binaries were used instead.

## Baseline checks

| Check | Result |
|---|---|
| `python3 tools/validate_repo.py` | Pass; 66 stable parameter IDs and preset references valid |
| `python3 -m unittest tools/test_validate_repo.py` | Pass; 4 tests |
| `bash tools/test_package_release.sh` | Pass; synthetic packaging fixture |
| Local configure via `tools/dev/bootstrap.sh build-dev` | Pass |
| JUCE quick suite | Pass; 84 selected tests, 0 failures, with Combinatorial excluded |
| Linux `GapFillTests`, VST3, CLAP, Standalone | Build pass |
| Developer build-slice regression | Pass |
| Native Windows/macOS hosts and physical MIDI/audio devices | Not available in this container |

The local JUCE build emitted existing non-fatal GCC warnings, including
overloaded virtuals, enum switch coverage, sign conversions, float equality,
and an unused test variable. They did not stop the four Linux targets from
building; this implementation did not suppress or weaken those warnings.

## Current caveats carried forward

The historical `PROGRESS.md` section titled “Next up” predates the 2026-10-19
and 2026-10-20 release commits. Do not use that section as the current release
asset inventory. Manual Windows/macOS/Linux DAW acceptance, CLAP host acceptance,
and outstanding issue states must still be checked against fresh evidence.
