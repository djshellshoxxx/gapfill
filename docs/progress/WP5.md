# WP5 - Shaping and decisions progress

Branch: `worktree-agent-a0290edf613f38ecd` (not pushed). Integration branch merged (head 113f0f2 incl. WP3/WP8).
Neutral defaults keep the goldens and 0.0.1 fixtures identical. Full suite after merge: 430 passed, 0 failed.

## Status per ticket

| Ticket | Status | Tests |
|---|---|---|
| E-2 tempo sync | Engine, state, help done; Options/Sync-panel UI pending | TestSync |
| C-0 effective display | Knob API, LookAndFeel marker, processor publication done | TestEffectiveDisplay |
| C-4 envelope | Engine + help done; GUI pending | TestEnvelope |
| C-1 personalities | Engine + help done; GUI pending | TestPersonality |
| C-6 breathing, C-7 phrase (+offset) | Engine + telemetry + help done (one commit); GUI pending | TestBreathing, TestPhrase |
| F-D01 budget / quiet zones | Engine + telemetry + help done; GUI pending | TestBudget |
| C-9 band type | Engine + help done; Spectral-panel combos pending | TestBandType |
| C-10 capture v2 | CaptureLatch, Recapture/Release commands, captureLength done; button/status GUI pending | TestCapture2 |
| C-3 mistakes | Engine + voice stage + help done; GUI pending | TestMistakes |
| C-8 evolve | Engine + telemetry + help done; GUI pending | TestEvolve |

## Not done (all tickets): GUI
No WP5 control is on screen yet. Planned: one "SHAPE" popover in `Slot::CreativeExtra` (already reserved) holding personality, envelope, mistakes, breathing, phrase, budget, evolve (+label from `getFillEngine().getDecisionTelemetry().evolveProfile`), capture length + Recapture (`postCommand({CommandType::Recapture})`, status from `getShaping().capture`), plus band-type combos (Panels.cpp, WP11) and the E-2 Options row (`getShaping().tempo.setFallbackBpm`). Parameters are automatable/MIDI-learnable today.

## Conventions
* Help orders 5100-5199 (E-2 5100, C-4 5110, C-1 5120, C-6 5130, C-7 5140, F-D01 5150, C-9 5160, C-10 5170, C-3 5180, C-8 5190).
* Shared runtime in `ShapingRuntime` (`getShaping()`); decision figures in `FillEngine::getDecisionTelemetry()`.
* All WP5 parameter IDs are in `performanceParameterIDs()` (not randomised).

## Deviations
* Hot files touched: PluginProcessor.h/.cpp, FillEngine.cpp/.h, FillVoice.cpp/.h, Presets.cpp (`surpriseTargets`, allowed), EngineSettings.h, DspUtils.h (`SourceHistory` read floor), SchedulerTypes.h (GapContext history/anchor), FeatureHooks.cpp, StateSubtrees.cpp (`Perf` row = E-2 fallbackBpm), Tests/TestSignals.h (TestPlayHead time signature).
* C-1: spec absolute histogram floors (Percussive 35 %, Ambient 40 %) unreachable through chooseAlgorithm (length fit, chaos flatten); tests assert a >= 1.25x lift over Off and Dub's Delay as largest. "Normalised L2" = unit-length vectors.
* C-3: Octave direction derives from the recipe seed (no extra id); Granular only goes down; Missed Repeat never drops the first or last repeat and is time-segmented; mistakes without a source-stereo check on mono.
* C-10: v1 pause-the-writer semantics kept (frozen history + read floor = captureLength); Recapture copies a live tap into ping-pong 2 s snapshots; at the swap Granular voices fade over 10 ms (no per-sample crossfade).
* C-8: scene-overlay precedence test not possible until scenes exist; zero weights stay zero.
* F-D01: ring cleared on transport jump; "Fill Now counts toward budget" is automatic (every started fill is recorded) but F-P02 is not here.
* Unverified: real DAW (Reaper 60/174 BPM), audibility of all features, screenshots, Windows/macOS.
