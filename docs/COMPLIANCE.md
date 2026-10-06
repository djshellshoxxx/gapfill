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
| Preset files | Implemented | Save/Open uses `.gapfill` APVTS state. External files are validated before replacing the current sound. |
| Factory presets | Implemented | Descriptive factory bank; preflight checks every referenced parameter ID. |
| MIDI Learn | Implemented | Visible learn state, explicit cancel, mapping removal, deterministic conflict handling, and 10-second timeout. |
| Tooltips | Implemented | Global toggle persists in plugin state. |
| Help/About | Implemented | In-plugin Help covers operation, troubleshooting, version, MIDI Learn, and Circuit Drift Labs link. |
| Privacy/network disclosure | Implemented | No telemetry, analytics, background updater, licensing server, or audio/project-data transmission. Documented in README. |
| Linux VST3 build | Planned validation | Workflow is defined; GitHub-hosted runners are currently terminating before step 1. |
| Windows VST3 build | Planned validation | Workflow is defined; GitHub-hosted runners are currently terminating before step 1. |
| CLAP host smoke test | Planned | CLAP builds are configured; real-host opening remains a manual validation item. |
| Standalone device UI manual pass | Planned | Requires interactive validation on a desktop audio system. |
| Keyboard navigation | Exception / planned improvement | The current editor deliberately avoids stealing host keyboard focus for most controls. A focused accessibility pass is still required to reconcile host-keyboard safety with baseline navigation guidance. |
| Reduced motion | Exception / planned improvement | Motion is currently limited to meters, value easing, and brief action flashes. A global reduced-motion option is still to be evaluated. |

## Validation

Fast repository validation:

```bash
python tools/validate_repo.py
python -m unittest tools/test_validate_repo.py
```

Full validation remains the JUCE test suite plus VST3 validation in `.github/workflows/ci.yml`. The preflight stage is intentionally dependency-free so repository/spec wiring failures are reported before JUCE is cloned or a compiler is started.

## Known infrastructure issue

As of 2026-10-06, GitHub Actions runs for this private repository are creating Linux and Windows jobs but terminating before any workflow step is recorded. The same behavior occurs on documentation-only commits on `master`, so it is tracked as runner/account infrastructure rather than a product validation result. The workflow remains enabled and must be re-run when GitHub-hosted runner execution is available.
