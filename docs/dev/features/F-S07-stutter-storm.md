# F-S07 One-click stutter storm
Status: implementation spec, Gate C candidate. Category FUN/LIVE, value M, size S-M. **Built on `F-M10-momentary-effects-and-latch.md`**, which already specifies the hold engine (`HoldController`, `MomentaryEngine`, two hold voices, segment chaining, dry duck, timeout, Panic). F-M10 names F-S07 as "a curated preset of Stutter segments". This ticket therefore adds only: a curated **Storm profile** (rate, style), **quantized engage**, and a one-button control. It must not create a second voice, dry-duck, timeout or hold-state implementation.

## 1 Summary & user-visible behaviour
A **STORM** button plays a rapid stutter of "what just played" while held, then releases cleanly. It is the Stutter hold of F-M10 with a fixed, tuned profile and grid-aligned start.
- Hold the button (or a mapped MIDI note/CC, or automate `fs07_storm`): starts at the next grid line when the transport is playing (quantize Off / 1/16 / 1/8 / 1/4 / 1 bar), immediately otherwise. Release uses F-M10's release fade.
- **Style**: Steady (even repeats), Tight (finer slices), Triplet (triplet grid), Wild (varying rate/gain steps). **Rate**: 1/8, 1/16, 1/32, 1/16T, 1/32T, independent of the Stutter Division knob (F-M10's STUTTER follows that knob; STORM does not).
- Everything else is inherited from F-M10: Dry duck, "Respect protection", Timeout (stuck hold auto-release, "Stuck hold released"), Panic, bypass clears, newest-pressed-effect-wins stack (STORM is stack entry `Effect::Stutter` with `profile = Storm`).
- Keyboard: Space/Enter on the focused button toggles latch (F-M10 latch rules).

## 2 Success/quality metric
- With quantize on and transport playing, first storm repeat starts exactly on the grid line (0 samples error, block sizes 32-8192, tempos 20-300 BPM); release to silence within F-M10's bound (<= 50 ms decay after Panic, release fade otherwise).
- Output finite and below 1.5 peak for full-scale input; storm CPU = one hold voice (F-M10 measurement).
- `fs07_storm` off (default) => bit-identical to a build without this ticket.

## 3 Design
New small module `Source/features/storm/` (`cmake/sources/storm.cmake`, `Source/params/fs07.{h,cpp}`, `tools/param_ids/fs07.lock`):
```cpp
struct StormProfile { float intensity, complexity, chaos; std::uint64_t seed; int sliceDivision; };   // fixed table, 4 styles
const StormProfile& stormProfile (int style, int rateIndex) noexcept;
struct StormArm { bool armed; std::int64_t startPos; };                                                // pure
StormArm planStormStart (const EngineSettings&, int quantIndex, std::int64_t outputPos, int numSamples) noexcept;
```
- **Extension point required from F-M10** (agree with its owner before coding): `MomentaryEngine::setStutterProfile (const StormProfile*)` (null = normal STUTTER behaviour) and `HoldController::engageAt (Effect, std::int64_t samplePos)`. F-M10 already names `engageAt` as the seam for future quantised engage (F-M11), so Storm uses the same one. Profiles feed F-M10's shared `toVoiceSetup` (`stutterSeconds = rate x beatSeconds`, `intensity/complexity/chaos/seed` from the table). Style rules: Steady keeps `stutterRatio == 1` and no rate step (complexity <= 0.5, intensity <= 0.45, chaos 0 in `FillVoice::start`'s Stutter branch); Tight halves the slice; Triplet uses the T divisions; Wild raises intensity/chaos so rate/gain steps appear. A test asserts each style's characteristic.
- **Gate sources:** `fs07_storm` parameter (GUI button via `ParameterAttachment` with gesture, DAW automation), MIDI hold via F-S12 `CommandId::StutterStorm` (press >= 64 / NoteOn; release < 48 / NoteOff). Both feed `HoldController::setLevel (Effect::Stutter, Source::Param|Midi, down)`; quantize delays the engage only (`engageAt`), never the release.
- **Quantize:** `planStormStart` uses `EngineSettings::ppqAtBlockStart`, `samplesPerBeat`, `transportValid` exactly like the scheduler; invalid transport => immediate with status "No transport, starting now". Releasing before the grid cancels the pending engage.
- **Fallback if F-M10 is not scheduled:** owner may instead approve a reduced build of F-M10 (`HoldController` + Stutter effect only, about a third of it). Do not fork a second implementation.
- Existing-file hooks: none beyond F-M10's and F-S12's (`commands.processMidi`, `CancellableFx` registry). `FillEngine`/`trySchedule` untouched.

## 4 Parameters & state
Appended group `GAPFILL_PARAMS_FS07` after the last group (permanent order), IDs prefixed `fs07_`. The validator's ID regex `^[a-z][A-Za-z0-9]*$` rejects `_`; it must be relaxed (owner decision; same change the `fm10_`, `fl06_` tickets need).
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fs07_storm` | bool (momentary level) | off | - |
| `fs07_stormRate` | choice: 1/8, 1/16, 1/32, 1/16T, 1/32T | 1/16 | - |
| `fs07_stormStyle` | choice: Steady, Tight, Triplet, Wild | Steady | - |
| `fs07_stormQuant` | choice: Off, 1/16, 1/8, 1/4, 1 bar | 1/16 | - |
All four are added to `performanceParameterIDs()` (not randomised, not in Surprise/Roulette, Undo snapshots or A/B). `fs07_storm` is forced off by `forceHoldsOff()` after any state/preset/Undo restore (F-M10 pattern) and written as 0 to `.gapfill` files. Dry duck, timeout and protection reuse `fm10_dryDuck`, `fm10_timeout`, `fm10_protect` (no duplicates). Old sessions lack the IDs and get defaults: storm off, bit-identical audio.

## 5 Real-time safety
Per chunk: a few atomic loads and integer/double grid arithmetic; the voice work is F-M10's (bounded, preallocated hold voices). No allocation, locks, IO or logging. All new loops are constant-size.

## 6 UI
- Slot `PerformanceStrip` (F0-9), beside F-M10's four buttons and F-S12's Panic: STORM hold-button (min 72x28). Right-click menu: Rate, Style, Quantize, Learn MIDI. No header control (docs/dev/11).
- Tooltip: "Hold to repeat what just played in rapid stutters. Release to stop. Starts on the beat when your DAW is playing." Help "Stutter Storm vs Stutter" (order 1080). Status: `Storm: Steady 1/16 (starts on next beat)`.
- Accessible: title "Stutter storm", state announced (armed, running, released, timed out). Reduced motion: steady highlight, no pulse. Narrow: overflow follows F-M10/F-S12 ordering; Panic never hides.

## 7 MIDI/automation/CLAP/host impacts
`fs07_storm` is a normal automatable bool in VST3/AU/CLAP ("Stutter Storm"); four parameters are added at the end of the list (existing indices untouched). MIDI hold follows F-S12 rules; MIDI delivery caveats (Live audio tracks, Logic `aumf`, FL port) as in F-S04. Lost NoteOff or controller disconnect => F-M10 timeout. Transport jump does not clear holds (F-M10 decision); a pending quantized engage is re-planned from the new position.

## 8 Tests (`Tests/features/TestStorm.cpp`)
`storm_off_bit_identical_golden` | `plan_start_on_grid_all_block_sizes` | `plan_start_immediate_without_transport` | `plan_start_cancelled_by_early_release` | `plan_start_after_tempo_change` | `storm_steady_constant_slice_length` | `storm_tight_half_slice` | `storm_triplet_grid` | `storm_wild_varies_but_finite` | `storm_rate_independent_of_stutter_div` | `storm_uses_fm10_timeout_and_duck` | `storm_cancelled_by_panic` | `storm_stack_newest_wins_with_stutter` | `storm_param_forced_off_after_restore` | `storm_ids_locked_valid_and_excluded_from_random_surprise_ab` | `storm_midi_hold_release_levels` | `storm_no_audio_thread_allocation`. Edge: 44.1-192 kHz, tempo 20 and 300 BPM, silent input ("Nothing to sample").

## 9 Dependencies & build order
Hard: F-M10 (and through it E-1/F-M01 for the dedicated-voice pattern and command-learn), F-S12 (`CommandBindings`, `CancellableFx`, Panic). Soft: F-S02, F-S05. Order: E-1 -> F-M10 -> F-S12 -> F-S07. Because F-M10 is a larger (M) ticket, F-S07 cannot honour the "S tier first" ordering of the backlog; this is the dependency, not a choice.

## 10 Effort & risks
S-M (L if F-M10 is not available and a reduced build is needed). Risks: F-M10 and F-S07 owners must agree the two extension points; style tuning by ear; four more host-visible parameters. Owner decisions: validator ID regex, whether Storm may exist as a button-only feature on top of a reduced F-M10, final style names.

## 11 Acceptance checklist
- [ ] F-M10 extension points agreed and implemented; no duplicate hold/duck/timeout code
- [ ] Grid-exact engage; early release cancels; transport-less fallback with status
- [ ] Four styles reviewed by ear against names; profile tests green
- [ ] IDs appended, locked, excluded from Random/Surprise/Roulette/Undo/A-B; gate forced off after restore
- [ ] Golden output unchanged with storm off; no audio-thread allocation
- [ ] Button hold/latch/keyboard, MIDI and DAW automation demonstrated in one host per OS
- [ ] Help, tooltips, accessible names, minimum-size screenshot
