# GapFill Shared Baseline Compliance

GapFill uses `docs/standards/CDL_PLUGIN_BASELINE.md` as the required common Circuit Drift Labs plugin standard. The product specification in `instructions.md` remains the source of truth for GapFill-specific DSP, signal flow, parameter behavior, and musical rules.

## Profiles

| Profile | Status | Notes |
|---|---|---|
| Effect | Implemented | Gap-aware audio effect with main input/output and optional sidechain. |
| MIDI | Implemented | MIDI input is used for CC learn and mapped parameter control; GapFill is not a MIDI effect and emits no MIDI. |
| Standalone | Implemented | Standalone target exposes device configuration through JUCE's standalone host wrapper. |
| File loading | Implemented | Portable `.gapfill` parameter-state files can be saved and opened. |
| File export | Not applicable | GapFill does not promise offline audio rendering/export. |
| Instrument | Not applicable | GapFill generates fills from recent input audio and is not an instrument. |

## Compliance areas

| Area | State | GapFill behavior |
|---|---|---|
| Stable automation IDs | Implemented | Stable IDs are declared once in `GAPFILL_PARAMS`; the repository preflight rejects duplicates. |
| Parameter smoothing / RT safety | Implemented | Critical continuous mix/protection controls are smoothed; DSP buffers are allocated in preparation paths. |
| Variable block sizes / sample rates | Implemented | Covered by DSP/plugin and combinatorial tests. |
| Sidechain absence | Implemented | Falls back safely to main detection when no usable sidechain signal is present. |
| Latency | Implemented | Lookahead latency is reported to the host and the dry/bypass paths remain aligned. |
| Bypass | Implemented | Click-controlled internal bypass plus host bypass path coverage. |
| State migration | Implemented | Older/missing parameters are tolerated; unknown future parameters are ignored. Malformed known values are rejected atomically. |
| Preset files | Implemented | Save/Open uses `.gapfill` APVTS state. Loads are capped at 1 MB, XML/type/known values are validated atomically, and failures preserve the current sound with a visible reason. |
| Factory presets | Implemented | Descriptive factory bank; preflight checks every referenced parameter ID. |
| MIDI Learn | Implemented | Visible learn state, explicit cancel, mapping removal, deterministic conflict handling, and 10-second timeout. |
| Tooltips | Implemented | Global toggle persists in plugin state. |
| Help/About | Implemented | In-plugin Help covers operation, troubleshooting, version, MIDI Learn, and Circuit Drift Labs link. |
| Privacy/network disclosure | Implemented | No telemetry, analytics, background updater, licensing server, or audio/project-data transmission. Documented in README. |
| Linux VST3 build | Automated gate implemented | CI builds VST3 and runs pluginval at strictness 10. Current GitHub-hosted runner infrastructure must execute successfully before a release is marked validated. |
| Windows VST3 build | Automated gate implemented | CI builds VST3 and runs pluginval at strictness 10. Current GitHub-hosted runner infrastructure must execute successfully before a release is marked validated. |
| CLAP host validation | Automated gate implemented | Linux and Windows CI build the CLAP target and run free-audio clap-validator. |
| Standalone validation | Automated + hardware acceptance | CI builds and smoke-launches the real Standalone target; unit tests construct the JUCE device selector. Physical-device acceptance steps are defined in `docs/VALIDATION.md`. |
| Keyboard navigation | Implemented | Interactive controls are non-focusable by default. Users can opt in to Tab/Shift+Tab traversal and standard keyboard operation; the editor root remains non-focusable, focus has a visible 2 px accent ring, and parameter controls expose explicit accessibility titles/descriptions. |
| Reduced motion | Implemented | Options includes a persisted Reduced Motion preference that disables knob/value easing, hover fades and brief action flashes while retaining live signal meters and the gap visualizer. |
| Compact editor navigation | Implemented | Detection, Creative, Weights, Spectral and Protection use a five-tab focused-page layout. The visualizer and mix stay in the scrollable page; fixed-width side columns were removed. |
| Collapsible feature tiles | Implemented | Each focused feature page is hosted in a presentation-only disclosure tile. Collapse state is editor-session-only; it does not affect APVTS or presets. |
| Compact editor bounds and overflow actions | Implemented | Minimum editor size is 820 x 540 logical px. Secondary top-bar actions move into MORE below 1180 px; bypass, preset, A/B and file actions remain visible. |
| Responsive viewport layout | Implemented | Feature pages use a vertical JUCE viewport with horizontal scrolling disabled. Visualizer and feature panel remain reachable when the host window is short. Breakpoint screenshot and scale QA remain pending. |

## Validation

Fast repository validation:

```bash
python tools/validate_repo.py
python -m unittest tools/test_validate_repo.py
```

Full validation is defined in `docs/VALIDATION.md` and enforced by the JUCE test suite plus VST3, CLAP and Standalone gates in `.github/workflows/ci.yml`. The preflight stage is intentionally dependency-free so repository/spec wiring failures are reported before JUCE is cloned or a compiler is started.

## Known infrastructure issue

As of 2026-10-06, GitHub Actions runs for this private repository are creating Linux and Windows jobs but terminating before any workflow step is recorded. The same behavior occurs on documentation-only commits on `master`, so it is tracked as runner/account infrastructure rather than a product validation result. The workflow remains enabled and must be re-run when GitHub-hosted runner execution is available.
