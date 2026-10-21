# GapFill Shared Baseline Compliance

GapFill uses docs/standards/CDL_PLUGIN_BASELINE.md as the required common Circuit Drift Labs plugin standard. The product specification in instructions.md remains the source of truth for GapFill-specific DSP, signal flow, parameter behavior, and musical rules.

## Profiles

| Profile | Status | Notes |
|---|---|---|
| Effect | Implemented | Gap-aware audio effect with main input/output and optional sidechain. |
| MIDI | Implemented | MIDI input is used for CC learn and mapped parameter control; GapFill is not a MIDI effect and emits no MIDI. |
| Standalone | Implemented | Standalone target exposes device configuration through JUCE's standalone host wrapper. |
| File loading | Implemented | Portable .gapfill parameter-state files can be saved and opened. |
| File export | Planned, not implemented | U-2 Fill Export is specified in docs/dev/05-SPEC-USABILITY-VALUE.md. Do not describe offline fill rendering/export as shipped until implemented and validated. |
| Instrument | Not applicable | GapFill generates fills from recent input audio and is not an instrument. |

## Compliance areas

| Area | State | GapFill behavior |
|---|---|---|
| Stable automation IDs | Implemented | Stable IDs are declared once in GAPFILL_PARAMS; the repository preflight rejects duplicates. |
| Parameter smoothing / RT safety | Implemented | Critical continuous mix/protection controls are smoothed; DSP buffers are allocated in preparation paths. |
| Variable block sizes / sample rates | Implemented | Covered by DSP/plugin and combinatorial tests. |
| Sidechain absence | Implemented | Falls back safely to main detection when no usable sidechain signal is present. |
| Latency | Implemented | Lookahead latency is reported to the host and the dry/bypass paths remain aligned. |
| Bypass | Implemented | Click-controlled internal bypass plus host bypass path coverage. |
| State migration | Implemented | Older/missing parameters are tolerated; unknown future parameters are ignored. Malformed known values are rejected atomically. |
| Preset files | Implemented | Save/Open uses APVTS state. Loads are capped at 1 MB, XML/type/known values are validated atomically, and failures preserve the current sound with a visible reason. |
| Factory presets | Implemented | Descriptive factory bank; preflight checks every referenced parameter ID. |
| MIDI Learn | Implemented | Visible learn state, explicit cancel, mapping removal, deterministic conflict handling, and 10-second timeout. |
| Tooltips | Implemented | Global toggle persists in plugin state. |
| Help/About | Implemented | In-plugin Help covers operation, troubleshooting, version, MIDI Learn, and Circuit Drift Labs link. |
| Privacy/network disclosure | Implemented | No telemetry, analytics, background updater, licensing server, or audio/project-data transmission. Documented in README. |
| Linux VST3 build | Automated gate implemented | CI builds VST3 and runs pluginval at strictness 10. A release still requires successful current CI and applicable host validation. |
| Windows VST3 build | Automated gate implemented | CI builds VST3 and runs pluginval at strictness 10. A release still requires successful current CI and applicable host validation. |
| CLAP host validation | Automated gate implemented | Linux and Windows CI build the CLAP target and run free-audio clap-validator. |
| Standalone validation | Automated + hardware acceptance | CI builds and smoke-launches the real Standalone target; unit tests construct the JUCE device selector. Physical-device acceptance steps are defined in docs/VALIDATION.md. |
| Keyboard navigation | Implemented | Interactive controls are non-focusable by default. Users can opt in to Tab/Shift+Tab traversal and standard keyboard operation; the editor root remains non-focusable, focus has a visible 2 px accent ring, and parameter controls expose explicit accessibility titles/descriptions. |
| Reduced motion | Implemented | Options includes a persisted Reduced Motion preference that disables knob/value easing, hover fades and brief action flashes while retaining live signal meters and the gap visualizer. |

## Validation

Fast repository validation:

    python tools/validate_repo.py
    python -m unittest tools/test_validate_repo.py

Full validation is defined in docs/VALIDATION.md and enforced by the JUCE test suite plus VST3, CLAP and Standalone gates in .github/workflows/ci.yml. The preflight stage is intentionally dependency-free so repository/spec wiring failures are reported before JUCE is cloned or a compiler is started.

## Known infrastructure history

**Resolved (updated 2026-10-21).** A runner-startup problem was reported on 2026-10-06, when GitHub Actions jobs terminated before recording a workflow step. It is no longer current: CI runs complete on the hosted runners (for example master CI run 39331947502 and PR CI runs 39431380019 and 39432078610 on 2026-10-19/20), and beta1 packages for Windows, Linux and macOS were published from validated jobs (owner statement; verify the release assets and their checksums rather than this sentence). Reopen the infrastructure investigation only if a current job again terminates before its first step; older notes in `PROGRESS.md` that say runners cannot start are historical. Successful CI only confirms the checks in those jobs; it does not replace real-DAW, hardware, Logic/AU, macOS Intel-native or packaged-install acceptance, which stay manual and unverified until recorded.

## Planned compliance changes (docs/dev/12)

Rows below change when the named ticket ships; update this file in the same PR. File export: U-2 (and F-L12 wet bus, F-M17 wet-only mode). MIDI profile: X-1 adds MIDI *command* bindings and Program Change input; F-L11 would make GapFill emit MIDI and is a build option that stays OFF by default (the "emits no MIDI" statement holds until then, never for AU). Keyboard navigation and Reduced Motion: every new control follows the existing rows; F-D04 adds contrast and large-control settings. Privacy: F-D02 event files and F-X05 preference tables are local files only, opt-in, never transmitted; no row changes to "no telemetry".