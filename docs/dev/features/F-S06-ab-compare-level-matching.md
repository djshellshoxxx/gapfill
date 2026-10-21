# F-S06 A/B comparison with level matching
Status: implementation spec, Gate C candidate. Category UX/VAL, value H, size M. Context: `10-SPEC-LIVE-CREATIVE-EFFECTS.md` section 4.2 (A/B audition and level-matched recipe compare) and U-1 (history). This ticket covers the **parameter-state** A/B (the existing header A/B) and adds level matching; recipe-level compare is `F-L23-recipe-ab-comparison.md` (different feature, shared loudness code).

Existing behaviour (characterize first, AGENTS.md rule 5): `PluginEditor` owns `slotA/slotB/usingSlotB`; `toggleAB()` copies `copyState()` into the current slot and `replaceState()`s the other; COPY duplicates to the other slot. Problems: slots live in the editor (lost whenever the host closes the editor, unusable with the editor closed), not saved with the project, swaps `bypass` and `lookahead` (changes PDC mid-comparison), no undo for COPY, no level compensation.

## 1 Summary & user-visible behaviour
- **A/B** header button (existing) toggles between two stored sounds. Slots now live in the processor and are saved with the project. Works with the editor closed (MIDI/shortcut).
- **COPY** (existing) copies the active side onto the other; one-step **Undo copy** appears in the A/B button's context menu.
- **Compared set** excludes `bypass`, `lookahead` (latency/PDC must not move while comparing), `capture`, `audition` and any future `performanceParameterIDs()`. Mix (`dry/wet/output`) stays in the set because it is part of the sound.
- **Level match** (off by default; A/B context menu or FILE menu "Compare > Level match"): the plug-in measures how much louder/quieter each side makes the output relative to the input and applies a smoothed attenuation (0 to -12 dB, never boost) to the louder side so both are heard at equal loudness. Status line: `Level match: B -2.3 dB`. Until both sides have been heard for >= 1.5 s of gated audio: `Play audio on both sides to level-match`, gain stays 0 dB. Bypass for matching = turn the toggle off (returns to exactly 0 dB).
- Switching is click-free: the wet bus dips for ~6 ms around the parameter swap.

## 2 Success/quality metric
- Level-matched sides differ by <= 1.0 dB (K-weighted, gated) on the test corpus (drum loop, pad, noise) after the 1.5 s settle time.
- Switch A->B->A restores every compared parameter bit-exactly (normalised values equal) and leaves excluded parameters untouched.
- Switch transition: max |sample delta| during the 20 ms around a switch <= 1.5x the same run's maximum sample delta without switching (dry sine, wet 100 %).
- With level match off, output is bit-identical to a build without this feature.

## 3 Design
New module `Source/features/compare/` (`cmake/sources/compare.cmake`):
- `AbSlots.{h,cpp}` (message thread, processor-owned): `enum class Side { A, B }`; `Side active() const`; `bool toggle (GapFillAudioProcessor&)`; `void copyActiveToOther()`; `bool undoCopy()`; `juce::ValueTree toValueTree (const juce::ValueTree& live) const`; `bool validate (const juce::ValueTree&) const`; `void restore (const juce::ValueTree&)`. Each slot stores only `PARAM id/value` children for `comparableParameterIDs()` (new function in the module, built from `allParameterIDs()` minus the exclusions; test asserts disjointness with the exclusion lists).
- Toggle algorithm: (1) snapshot live compared values into the active slot; (2) set `switchHold` (atomic) so the audio thread ramps wet to 0 over ~6 ms; (3) after 8 ms (`juce::Timer::callAfterDelay`, message thread) merge the other slot into a copy of the live tree and call the existing validated `applyParameterState` path (which also snaps normalised values for hosts and refreshes the host display); (4) clear `switchHold` after another 8 ms. No undo snapshot (the toggle is its own inverse); COPY pushes the one-step `copyBackup`.
- `LevelMatch.h` (header-only DSP, audio thread) built on the shared pure `LoudnessEstimate` (K-weighting plus gated mean square) that `F-L23-recipe-ab-comparison.md` also uses; whichever ticket lands first owns `Source/features/loudness/LoudnessEstimate.{h,cpp}` and the other consumes it, so there is one K-weighting implementation. `prepare (sr, channels)`; `process (const float* const* in, const float* const* out, int n) noexcept` accumulates 100 ms sub-block energies after ITU-R BS.1770 K-weighting (coefficients derived by bilinear transform for any sample rate, verified against the published 48 kHz table); gate: sub-blocks with input K-energy below -60 dBFS are ignored; forgetting factor `exp(-0.1/6)` per sub-block (about 6 s window); `Gdb[side] = 10 log10 (sumOut / sumIn)`, valid after >= 1.5 s accumulated gated time. `float targetMatchDb (Side active) const noexcept`: both valid => `clamp (min (GdbA, GdbB) - Gdb[active], -12, 0)`, else 0. Measurement taps the signal **before** the match gain, otherwise the loop would chase itself.
- Processor hooks (minimal): in the mix loop after `outGain` multiply by `matchGain.getNextValue()` (a `SmoothedValue`, 50 ms ramp, skipped entirely when matching is off), and by `transitionGain` for the wet term; `abSlots` member and `activeSide` atomic published for the matcher; `getStateInformation`/`setStateInformation` call `abSlots.toValueTree/restore` through the F0-6 `StateSubtrees` table. `PluginEditor` loses `slotA/slotB/toggleAB/copyToOtherSlot` bodies, which delegate to `proc.getAbSlots()` (previous entry points kept until callers migrate).

## 4 Parameters & state
- No APVTS parameters (comparison is a workflow, not an automatable value; automating A/B would fight the host's own automation). Reserved prefix `fs06_` unused.
- New versioned subtree `<AB version="1" active="A" levelMatch="0">` with `<Slot side="A">` and `<Slot side="B">`; budget <= 16 KiB (about 70 params x 2 slots x 70 bytes = 10 KiB); validation: known ids, finite values within range (same rules as `isParameterStateValid`), no duplicates; invalid subtree dropped, slots re-seeded from live state (B = copy of A, A active). `matchDb`, measurements and `switchHold` are never saved.
- Old sessions: no `<AB>` => A active, B = copy of A, level match off => sound and parameters identical. The only saved-state change is the optional subtree.
- Host-state policy: the APVTS state a host saves is always the **active** side (what the user hears); the inactive side exists only in `<AB>`. Switching sides performs ordinary parameter writes (same host notification path as preset loads), so a host with write-automation armed records them; this cannot be avoided without an effective-settings overlay (10-SPEC says switching must not mutate host automation values). **Owner decision:** accept parameter writes (proposed) or require an overlay layer on top of F0-5 (more invasive, knobs would no longer show what is heard).

## 5 Real-time safety
Audio thread: when matching is on, 2 K-weight biquad chains per channel for in and out (about 8 biquads/sample) plus a handful of adds; fixed memory allocated in `prepare`; no locks, allocation or IO. Results to the message thread via relaxed atomics (`GdbA`, `GdbB`, `valid`). `switchHold` is a single atomic bool. When matching is off the DSP is not run and the multiply is skipped (bit-exact path).

## 6 UI
- Placement: no new header control (docs/dev/11). A/B and COPY stay; the A/B button gets a right-click menu: Level match, Undo copy, Learn MIDI for A/B toggle. FILE menu adds a "Compare" group. Status line shows match state (F-S02 `StatusSource`).
- Tooltips: A/B "Switch between two versions of your sound. Bypass and Lookahead are not switched." Level match "Makes both versions equally loud so you judge the sound, not the volume. Only lowers the louder one."
- Help: "Comparing sounds (A/B)" (order 1070). Accessible: button title "A/B compare, side A" updating to B; toggle state announced; menu items named. Keyboard: `Ctrl/Cmd+Shift+B` toggles when the editor has focus. MIDI: `CommandId::AbToggle` (F-S12).
- Reduced motion: no flash on switch. Narrow: no layout change.

## 7 MIDI/automation/CLAP/host impacts
Parameter writes on switch are visible to hosts (see 4). `lookahead` excluded so PDC never changes. CLAP/VST3/AU: the program name shows "Custom" after a switch because `applyParameterState` sets `currentProgram = -1` (existing behaviour; acceptable, documented). Offline render uses whichever side is active at render time.

## 8 Tests (`Tests/features/TestCompare.cpp`)
Characterization first: `ab_toggle_roundtrip_current_behaviour`. Then: `ab_roundtrip_bit_exact` | `ab_excludes_bypass_lookahead_capture` | `compared_set_disjoint_from_performance_ids` | `ab_works_without_editor` | `copy_then_undo_copy` | `ab_state_roundtrip_active_side_saved` | `ab_old_state_loads_unchanged` | `ab_corrupt_subtree_dropped_rest_loads` | `ab_subtree_size_cap` | `ab_switch_click_free` | `ab_switch_rapid_toggle_100x_stable` | `ab_during_panic_held` | `level_match_off_bit_exact` | `level_match_within_1db_drum_pad_noise` | `level_match_never_boosts_max_12db` | `level_match_silent_input_stays_zero` | `level_match_invalid_until_both_sides_heard` | `level_match_nan_inf_input_safe` | `level_match_sample_rates_44k1_to_192k` | `k_weighting_matches_reference_48k` | `level_match_no_audio_thread_allocation`. Fixtures: drum loop scenario A seed 1, pad (no events), pink noise, silence, two presets with known loudness gap (Init vs Breakcore Destroyer).

## 9 Dependencies & build order
Needs F0-6 (subtrees), B4 (atomics/queues pattern), F-S12 (`CommandBindings` for the MIDI toggle; button works without it). Soft: F-S02 (status), F-S08 (Roulette uses its own restore, not A/B), F-L23 (recipe-level duel; shares `LoudnessEstimate`), U-1 later reuses `LevelMatch`. Order: F-S12 -> F-S06.

## 10 Effort & risks
M. Risks: ratio-based loudness is an approximation when fills fire only in gaps (documented; cap and "never boost" keep it safe); stale measurement of the hidden side (show age in tooltip); partial-state audible during the swap (mitigated by wet dip); behaviour change from excluding `bypass/lookahead` (called out in release notes); host automation recording (decision above).

## 11 Acceptance checklist
- [ ] Characterization test of the old A/B written before the refactor and still green or consciously updated
- [ ] Slots in processor, saved in `<AB>`, old sessions unchanged
- [ ] Excluded parameters never switch; latency unchanged by A/B
- [ ] Level match within 1 dB on corpus; off path bit-exact; max 12 dB attenuation, never boost
- [ ] Click-free switch verified; rapid toggling stable
- [ ] No audio-thread allocation; golden output unchanged with feature idle
- [ ] Help, tooltips, accessible names; screenshot of menu at minimum size
- [ ] Owner decision on host parameter writes recorded in `docs/progress/F-S06.md`
