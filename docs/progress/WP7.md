# WP7 - Musical context progress

Branch: `worktree-agent-a2c162820170573c0` (not pushed). Integration branch `claude/inspiring-fermi-pv9gv5` merged last with WP3, WP4, WP5 and WP8 landed (WP6 was not on it yet).
Scope: N-MUS, F-L22, F-L09, F-X03, F-L10, F-X05, F-M21, F-X04.
Neutral settings (every new feature off, Scale Off) leave the six golden renders and the 0.0.1 state fixtures unchanged; `Tests/golden` was not touched.

## Status per ticket

| Ticket | Status | Where |
|---|---|---|
| N-MUS Scale Lock | Done | `Source/features/scale/*`, `Source/gui/slots/ScaleStrip.cpp`, `TestScale.cpp` |
| F-L22 Musical energy tracking | Done, see deviations | `features/context/EnergyTracker.*`, `effective/StageEnergy.cpp`, `EnergyStrip.cpp`, `TestEnergy.cpp` |
| F-L09 Groove inheritance | Done, see deviations | `features/groove/*`, `GrooveStrip.cpp`, `TestGroove.cpp` |
| F-X03 Automatic key detection | Done | `features/keydetect/*`, `KeyStrip.cpp`, `TestKey.cpp` |
| F-L10 Call and response | Done | `features/callresponse/*`, `CallStrip.cpp`, `TestCallResponse.cpp` |
| F-X05 Local preference learning | Done, M1/M4 deviate | `features/taste/*`, `TasteStrip.cpp`, `TestTaste.cpp` |
| F-M21 Tension / release curves | Done without the curve editor popover | `features/tension/*`, `effective/StageTension.cpp`, `TensionStrip.cpp`, `TestTension.cpp` |
| F-X04 Arrangement awareness | Done | `features/arrangement/*`, `effective/StageArrangement.cpp`, `ArrangementStrip.cpp`, `TestArrangement.cpp` |

## Test evidence

After merging the integration branch (WP5): `GapFillTests` 649 passed / 0 failed (WP7 added about 150: Scale 20, Energy 12, Groove 19, Key 22 (incl. helpers), CallResponse 11, Taste 17, Tension 15, Arrangement 15). `tools/validate_repo.py` OK (152 IDs), `python -m unittest discover -s tools -p 'test_*.py'` 25 OK, `tools/check_hot_files.py` OK. VST3 / CLAP / Standalone builds: see the hand-off.

## New files, parameters and state

* Sources are in `cmake/sources/{scale,groove,callresponse,taste,tension}.cmake`, tests in `cmake/tests/*.cmake`. One `ContextState` (`features/context/`) owns every tracker; the processor calls it in six places.
* 32 new parameters, all append-only with locks updated: `scaleRoot scaleType scaleAmount` | `scaleFollow fx03_window fx03_minConf fx03_hold` | `energyAdapt energyTarget` | `fl09_amount fl09_grid fl09_hold` | `fl10_mode fl10_strength fl10_calls fl10_replies` | `fx05_on fx05_strength` | `fm21_on fm21_bars fm21_start fm21_shape fm21_end fm21_depthDensity fm21_depthIntensity fm21_depthWidth fm21_depthFx` | `fx04_on fx04_calm fx04_lift fx04_settle fx04_sensitivity`. None is in Random / Surprise; the `fl09_*`, `fl10_*`, `fm21_*`, `fx04_on`, `fx05_on` ids are also performance ids.
* State subtrees: `Groove`, `Tension` (existing reserved rows, now filled) and two new rows `Key` and `Taste` (13 rows; `TestState` count assertion changed 11 -> 13). Nothing else is serialised.
* Commands appended: `SkipLatest = 31`, `SkipRecipe = 32` (message-thread handling). Recipe flags appended: `flagSnapped` (bit 5, Scale Lock), `flagResponse` (bit 6, Call and response).

## Hot-file touches (hook lines)

`FillEngine.cpp` (adjustRecipe, toVoiceSetup, chooseAlgorithm/chooseBand/buildRecipe responder factors, noteFillStarted, trySchedule configure, reset), `FillEngine.h` (responder member, jump reset), `FillVoice.cpp` (7 lines: scale quantiser, cloud quantiser, comb frequency, granular ratio, stutter/delay groove shifts), `PluginProcessor.cpp/.h` (the `ContextState` member, accessors, about ten lines: prepare/reset, command, jump, onChunk, timer, fill events, width target), `StateSubtrees.cpp` rows. Everything else is new files.

## Deviations from the specs

1. **Pitch Rise / Fall under Scale Lock** draw their semitones from a salted sub-RNG (`kSalt_Scale`) with the voice's distribution, because the voice draws internally; the legacy draw order is untouched and output with Scale Off is bit-identical.
2. **Parameter IDs**: doc 12 names kept where doc 12 defines them (`energyAdapt/Target`, `scaleFollow`); the feature specs' `fl09_/fl10_/fm21_/fx03_/fx04_/fx05_` names elsewhere. `scaleFollow` is F-X03's "Auto key".
3. **F-L22**: doc 12 offsets (density +-0.15, intensity -0.10/+0.15) with the feature spec's frame design; no style-follow. Silence is Low. M1: a ladder reads Mid then High (the range is relative), not Low/Mid/High once each.
4. **F-L09**: hit time is measured in the INPUT domain from the analysis events, so no lookahead correction is needed (tested at 0 and 40 ms). Scramble slices are not shifted (not a musical subdivision in the voice). Tempo-ramp / W-03 variable-block transport fixes are not part of this work; jump detection reuses the processor's.
5. **F-X03**: peak harmonic suppression added (a peak on the 2nd..7th harmonic of a louder lower peak counts a quarter), because partials made minor keys read as major. A strong lead (>= 0.10) switches the published key after two evaluations instead of three. Measured on the fixtures: 72/72 exact (fixtures use stratified melody draws, not independent random draws, so sampling noise does not dominate), invalid fraction on drums/noise/silence/clicks < 5 %, lock <= 8 s, one key change at most under noise bursts, modulation reported within window + 3 s. Worst `analyseFrame` 105-180 us on the loaded shared VM (target 60 us on a quiet machine, unverified there).
6. **F-L10**: a motif needs a dominant family (share >= 0.5) in addition to confidence >= 0.5, otherwise regular spacing alone opened a response for three different families (76 % -> 95 % unclear). Recipe flag bit 6 (bit 3 is taken by Swell). History shows "Response".
7. **F-X05**: M1 (+15 points at 30 ratings) is not reachable with the specified model and 80 %-consistent ratings (multipliers capped at 0.5..2, 2 ratings per fill type): measured +7.6 points at 30 and +10 at 60 ratings; the tests assert >= +5 / +9. M4: the raw formula moves a sparse feature by up to ~0.2 per rating, so the service slew-limits every published multiplier to 0.06 per rating. Skip is a dislike signal; unrated fills are neutral (DECISIONS 12). The History drawer has no Skip button (WP4 file); Like / Skip act on the latest fill in the Taste strip, and LikeRecipe from the drawer is observed.
8. **F-M21**: curve editor popover is not built; Custom reads curves from the saved `<Tension>` subtree (validated, <= 2 KiB). `fxSend01` is produced (`perf.fxSend01`) but not yet consumed by the effect cards.
9. **F-X04**: boundary accuracy M1 precision 0.95 / recall 1.00 on 10 synthetic songs; stationary 5 minute loop: 0 boundaries; prediction within +-4 frames (2 s) in >= 80 % of cases (spec: +-1 frame, not met because detection lags 3.5 s and jitters); section energy is the position between the quietest and busiest section (not a percentile rank) so the quietest section gets exactly 0; M5: Calm removes 34 % of the fills in quiet sections at density 50 % (the spec's density -> probability mapping bounds it near 39 %). No bar snapping of predictions (frames only).
10. **UI**: every strip registers in `Slot::PerformanceStrip`, which has no reserved rectangle until UI-0; the components are hidden in the shipping editor and reachable through host parameters / MIDI learn.

## Measured numbers (Linux, gcc Release, shared 4-core VM)

`SectionTracker` < 100 KB, `pushFrame` 7-50 us, `ChromaTracker::analyseFrame` 105-180 us under load, no audio-thread allocation with any WP7 feature on (`AllocGuard` tests for each).

## Unverified

Real DAW / host behaviour, pluginval / clap-validator for the new parameters, audibility of every musical effect, Windows and macOS builds, key detection and arrangement accuracy on real recorded music, multi-instance ratings library in two real hosts, F-M21 / F-X04 / F-X05 UI at minimum size (no reserved rectangle).
