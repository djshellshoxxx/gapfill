# GapFill test and validation traceability

This guide connects each requirement to executable tests and the evidence required for acceptance. The full release procedure remains in `docs/VALIDATION.md` and `docs/dev/07-QA-AND-RELEASE.md`; this document is the feature-level index. Update the status after each implementation PR. Never equate a build passing with user-visible behavior verified.

## Status vocabulary

- **Automated**: a deterministic test or validator runs in CI and the passing run is linked.
- **Manual**: tested in a named DAW/device/OS with steps and evidence recorded.
- **Unverified**: no current evidence; do not imply completion.
- **Blocked**: a known issue prevents acceptance.

Each row must name test case(s), required CI/host jobs, status, and evidence link or commit. A blank evidence link means unverified.

## Validation layers

| Layer | Purpose | Authoritative evidence |
|---|---|---|
| Repository preflight | Required files/specs, stable IDs, preset references, workflow gates, packaging fixtures | `python3 tools/validate_repo.py`, its unit tests, shell/package checks in CI |
| DSP unit/regression | Determinism, finite output, buffer boundaries, protection, state and signal behavior | Named `GapFillTests` cases and test-run output including executed/passed/failed count |
| Combinatorial | Parameter combinations and interaction regressions | `GapFillTests Combinatorial`; must report a nonzero executed count |
| Plugin validators | Format/host contract smoke and editor automation | VST3 pluginval strictness 10, CLAP validator, macOS AU validation/auval where configured |
| Standalone | Device selector construction and process startup | Real Standalone target build and CI launch smoke; physical-device acceptance remains separate |
| Real DAW/MIDI/audio device | Actual host behavior, MIDI hardware, audible results, and editor lifecycle | Manual checklist with host/version/OS/format/sample rate/block size, result and evidence |
| Release artifact | Packaged file contents, architecture, checksums and signatures | Validation of downloaded assets, not only build-tree outputs |

## Feature traceability

Keep this table current. The file names below are expected test locations; adjust them to the repository's actual test layout when the feature is split into its own module.

| Requirement | Unit/regression evidence | CI/host evidence | Current status |
|---|---|---|---|
| Gap detection, fill scheduling and deterministic selection | DSP/signal tests; seeded same-input/same-PPQ comparison; variable block sizes | Linux/Windows/macOS builds and validators | Automated coverage exists; full host/audible comparison must be recorded per release |
| Sidechain fallback, transient protection, latency and bypass alignment | Signal fixtures; absent/zero sidechain; impulse alignment; finite bounded output | VST3/CLAP validators; DAW bypass/PDC check | Automated coverage is defined in `VALIDATION.md`; host evidence must be linked |
| Preset and session-state validation | Round-trip, malformed/truncated/oversized input, atomic failure preservation, old-state migration | Open/reopen in supported formats and hosts | Automated coverage described in compliance/validation docs |
| MIDI Learn | CC-to-parameter, conflict, timeout, cancel, flood, persistence | Physical MIDI controller in a real DAW and Standalone | Unit coverage exists; hardware checks remain manual |
| Undo, Capture v1, Surprise, Tape Stop, factory presets | Action/state round trip; capture freeze/live separation; deterministic profile choice; algorithm behavior; preset reference tests | Platform builds and plugin validators | Listed as shipped; verify the associated test names and evidence when changing them |
| Variable/chunked transport and seeks/loops | Alternating callback sizes 32/64/512/4096/8192, internal chunking, genuine seek/loop, tempo/meter changes | Real hosts with varying callback sizes | **Blocked/needs confirmation:** see issue #10 and PR #12; do not close without regression evidence |
| Windows editor automation lifecycle | Repeated editor create/open/close/destroy while playing/automating and state changes | Windows pluginval strictness 10 plus DAW session | **Unverified investigation:** issue #13; gather exit code/stack evidence |
| Fill Audition (E-1) and MIDI trigger (U-3) | `TestAudition.cpp` (Audition, NoteTrigger): coalescing, stale command discard, gap counters untouched, ducked, deterministic sequence, no allocation, CC edge/re-arm, note/channel filter, learn timeout | Linux automated; DAW preview in Reaper/Ableton/standalone and a physical MIDI controller are **unverified** | Implemented (WP4); host/hardware checks pending. Skip explanations are E-3 (WP3) |
| History, recipe lock/compare, export (U-1, C-5, C-11, U-2, F-S06) | `TestHistory.cpp`, `TestLock.cpp`, `TestExport.cpp`, `TestAbCompare.cpp` (see the WP4 table below) | Linux automated; DAW export/drag-out/import, audibility of lock and level matching are **unverified** | Implemented (WP4); host checks pending |
| Scenes, macros, quantized changes, panic/recovery | Queue overflow, note-off timeout, transport/no-transport fallback, panic during every FX, last-good restore | Real DAW automation and physical controller | Candidate specs; not implemented/verified |
| Musical context | Fixed seed/PPQ phrase reproducibility, groove offset boundaries, confidence fallback, loop/seek | Host tempo/meter/transport session | Candidate specs; not implemented/verified |
| Each new effect | Neutral golden; impulse/sine/silence/NaN; bounds; state migration; CPU and memory budget | Per-format validators; audible bypass/automation test | Add one row per effect before implementation; no effect is accepted from UI presence alone |

Rows below are for the tickets specified in `docs/dev/12-SPEC-BACKLOG-AND-NEW.md` (test names are listed in each ticket; all are **Specified, not implemented/verified** until a PR links evidence). Test files live in `Tests/features/Test<Feature>.cpp`.

| Ticket | Unit/regression evidence (file) | CI/host evidence | Current status |
|---|---|---|---|
| F0-10 pre-registration | golden identical; `validate_repo.py` with 30 empty locks | all CI jobs | Specified |
| X-1 MIDI command map | edge/momentary/toggle, duplicate refusal, flood bound, panic direct path (`TestCommands.cpp`) | Physical controller in a real DAW and Standalone | Specified; hardware unverified |
| FX-0 effect-card framework | empty-chain identity, card-off never called, fixed order, per-voice state, click-free, panic/tail, duck + fill starts to bus cards, CPU meter, no-alloc, all-off golden, `<Fx>` state (`TestFx.cpp`, `TestFxSection.cpp`) | per-format validators | Implemented (WP8); validators and Effects-section placement (UI-0) unverified; `enable_disable_ramp` is per-fill, see `docs/progress/WP8.md` |
| SP-0 spectral core | identity reconstruction (-90 dB, 44.1-192 kHz), latency exact (N-1), block-size bit-exact, sr re-prepare, NaN guard, throughput, no-alloc, phase helpers (`TestSpectral.cpp`) | CPU budget on the reference machines | Implemented (WP9); latency is N-1, see `docs/progress/WP9.md` |
| N-FX1 Reverse Swell | landing time +-1 sample, never overlaps protNext, curves, neutral (`TestSwell.cpp`) | audible check on drum loop in a DAW | Implemented (WP8); audible check unverified |
| N-FX2 Granular Cloud | <= 48 grains, density limit, jitter/pitch bounds, ends before protNext, neutral (`TestCloud.cpp`) | audible check; CPU per voice | Implemented (WP8); smoothness criterion met only for dense settings, see `docs/progress/WP8.md`; audible check unverified |
| N-USE-a Panic & Recover | wet silent <= 5 ms, dry bit-identical, every algo/card, not quantised, not saved (`TestPanic.cpp`) | Hardware button + real DAW | Specified; hardware unverified |
| N-USE-b Health footer | `TestHealth.cpp`: healthy_shows_nothing, each_code_set_and_cleared_with_hysteresis, flicker_inside_the_hold_time_never_shows, pulses_latch_for_ten_seconds, sidechain_absent_vs_silent, no_transport_only_when_feature_needs_it, priority_order, message_text_matches_code, muted_code_hidden_but_listed, footer_rate_limited, mask_publish_no_alloc, processor_publishes_codes_from_real_blocks, muted_codes_round_trip_through_the_state_subtree; `TestExplainGui.cpp`: footer_shows_only_the_highest_priority_message, details_panel_lists_everything_in_text | Standalone + DAW without sidechain/transport (E-7 script) | **Automated (Linux)**; real-DAW reproduction of each code **unverified** |
| F-S01 CPU and latency display | `TestTelemetry.cpp`: cpu_estimate_monotonic_with_load, estimate_is_independent_of_the_block_size, overruns_are_counted_and_garbage_input_is_ignored, labels_say_estimated, latency_matches_reported, disabled_when_editor_closed_zero_cost, measuring_does_not_allocate_or_change_the_sound | latency equals the host-reported value in a real DAW (manual) | **Automated (Linux)**; host comparison **unverified**; per-card `fxCpu` waits for FX-0 |
| E-3 Reasons and legend | `TestReasons.cpp` (15 tests: wording, exact numbers, reasonCount == events, viz flags, hover tooltip), `TestExplainGui.cpp`: gui_legend_has_text_and_shape_for_every_state, legend_collapses_and_the_choice_is_remembered_in_state | M-3 manual script (provoke each reason) | **Automated (Linux)**; manual M-3 script **unverified** |
| F-M03 Gap timeline | `TestTimeline.cpp` (10 tests incl. variable_block_sizes, ring_wrap_512, overflow_sets_incomplete, pause_does_not_drop_events), `TestExplainGui.cpp`: a11y_list_matches_events, screenshot_min_default_large, reduced_motion_static, editor_hosts_footer_and_overlay_and_keeps_min_size_layout | DAW session with scripted E-3 reasons: timeline text equals engine record | **Automated (Linux)**; real-host visual check **unverified**; height x2 expansion deferred to UI-0 |
| F-D02 Event capture and replay | `TestEvents.cpp` (14 tests: record_off_zero_cost, ring_full_stops_with_flag, file_roundtrip, replay_reproduces_outcomes, rejects_oversize_malformed, contains_no_audio_or_paths, records_ordered_across_threads, command_29_toggles_recording_on_the_message_thread) | Standalone Open replay (manual) | **Automated (Linux)**; Standalone UI path **unverified** |
| N-MUS Scale Lock | 12 scales x 12 roots, tie rule, RNG untouched, comb in-scale (`TestScale.cpp`) | audible check in a key | Specified |
| F-L07 / N-VAL Scenes and Bank | quantised boundary exact PPQ, block invariance, loop/seek re-arm, file round trip, hostile import (`TestScenes.cpp`, `TestSceneBank.cpp`) | Looping DAW project on the bar line; cross-OS bank import | Specified; host unverified |
| N-EASY Fix-it | `TestFixit.cpp` (20 tests: one_synthetic_log_per_rule_gives_the_exact_proposal, apply_pushes_undo_and_sets_values, undo_restores_bit_identical, never_touches_forbidden_ids, r10_requires_confirm, cooldown_and_dismiss, evidence_hold_time, priority_vs_health_and_other_status, apvts_adapter_applies_and_undoes_on_the_real_parameters) | DAW with automation lane present | **Automated (Linux)**; automation interplay **unverified**; R8/R11 stay inactive until `fl01_lock` / `qualityMode` exist |
| F-M10, F-S07, F-S08, F-P02 | hold/latch/timeout, storm pattern grid, roulette no-repeat, forced fill safety (`TestLive.cpp`) | hardware pads/MIDI | Specified; hardware unverified |
| F-M03 timeline, F-D02 events | reason text equals engine record, ring wrap, replay reproduces outcomes (`TestTimeline.cpp`, `TestEvents.cpp`) | screenshots min/default/large | Specified |
| F-D01 / F-D03 / F-D04 | budget and quiet zone, quality caps and Auto hysteresis, contrast ratios (`TestBudget.cpp`, `TestQuality.cpp`, `TestAccess.cpp`) | CPU-limited machine; contrast screenshot | Specified |
| Musical context (F-L09, F-L10, F-L22, F-M21, F-X03, F-X04, F-X05) | groove estimate within 2 ms, motif table, hysteresis, curve shapes, 24-key detection, boundary detection (`TestGroove.cpp` etc.) | host tempo/meter session | Specified; heuristics need listening tests |
| F-M16 ratchet, F-M14 rhythmic crush, F-M20 tape destruction | exact repeat positions, quantiser levels, hold pattern, determinism per seed, clicks, DC, finite extremes, CPU (`TestVoiceFxCards.cpp`) | per-format validators; audible bypass/automation | Implemented (WP8); audible check and cross-platform crush hash unverified |
| F-L14 multiband effect routing | all-on identity, band bypasses voice cards, bus sees only selected bands, second bus only when needed (`TestFx.cpp`, group `BandFx`) | real DAW, spectral mode | Implemented (WP8); BusFx cards (WP9) not yet present, exercised with a stub card |
| F-M15 Comb resonator | off, tuning < 5 cents (24-96), stable extremes, scale-mode notes, click-free retune, denormal-clean, spread, tail cap, duck (`TestBusFx.cpp` `Resonator.*`) | audible check | Implemented (WP9); Scale Lock seam only (N-MUS not landed) |
| F-L19 Frequency-shifted delay | zero shift == plain delay reference, shift accuracy < 0.5 Hz, direction, image rejection >= 30 dB, stable at max feedback, tempo slew, 44.1-192 kHz limits, tail cap, panic, duck (`FsDelay.*`) | audible check | Implemented (WP9) |
| F-L15 Dynamic dub feedback | monotonic decay, bounded at max feedback, ping-pong routing, no-transport LFO fallback, smooth automation, forced tail decay, panic, CPU, duck (`Dub.*`) | audible check | Implemented (WP9) |
| F-L21 Transient-derived convolution | no-IR identity, IR extraction and -24 dB normalisation, NaN / oversize rejected, zero latency, swap without click, no audio in state, 500 ms CPU, tail cap, thread stops (`Convo.*`) | Save / Load IR dialogs in a host | Implemented (WP9); file dialogs unverified |
| F-L20 Formant shifting | pitch preserved < 5 cents, formant peak moves by ratio, stereo coherence, transient timing, silence / extremes finite, latency + duck, CPU / memory, no-alloc, Lite (`Formant.*`) | audible check | Implemented (WP9); latency 21.3 ms (N-1) |
| F-X02 Spectral freeze and morph | capture / release stress (10k), thread race, phase continuity, memory ceiling, latency + duck, panic, morph endpoints, pitch / width, auto-capture, sr re-prepare, determinism, no-alloc (`SpFreeze.*`) | TSAN run; cross-platform tolerance | Implemented (WP9); TSAN and other platforms unverified |
| F-X01 Harmonic granular resynthesis | per-card list in doc 12 section 7 | audible check | **Not started** (needs N-MUS, WP7) |
| Routing (F-M17, F-L11, F-L12, F-L13, F-X06) | wet-solo identity, MIDI-out bounds, bus layout matrix, SC gate/snap, co-op lifecycle | pluginval 10, clap-validator, auval, real DAW | Specified; AU/Logic unverified |

### WP4 tickets (audition, recipes, export, A/B)

| Ticket | Unit/regression evidence (file) | CI/host evidence | Current status |
|---|---|---|---|
| E-1 Fill Audition | `audition_starts_without_gap`, `audition_leaves_gap_counters_untouched`, `audition_not_in_recipe_events`, `audition_respects_wet_zero_and_output_trim`, `audition_ducked_by_protection`, `audition_rejects_no_source`, `audition_rejects_no_band`, `audition_restart_fades_old_voice`, `audition_coalesces_pending`, `audition_stale_command_discarded`, `audition_ignored_when_bypassed`, `audition_fades_on_panic_reset_samplerate_change`, `audition_deterministic_sequence_for_same_seed`, `audition_does_not_change_next_natural_fill`, `audition_no_audio_thread_allocation`, `audition_midi_cc_edge_and_rearm`, `audition_finite_output_all_algorithms` (`TestAudition.cpp`) | Linux; pluginval 10 + DAW playing/stopped unverified | Automated; host unverified |
| U-3 MIDI note trigger | `note_trigger_fires_once_per_noteon`, `wrong_note_or_channel_ignored`, `velocity_scales_amplitude`, `learn_times_out`, `trigger_roundtrip_state`, `note_ignored_when_bypassed`, `coalesces_with_ui_request` (`TestAudition.cpp`) | hardware controller unverified | Automated; hardware unverified |
| C-5 Fill Lock | `lock_pins_character_across_gaps`, `lock_does_not_force_fill_each_gap`, `lock_uses_fresh_seed`, `lock_adapts_length_to_short_gap`, `lock_skips_unfit_gap_with_reason`, `lock_band_fallback_and_skip`, `lock_armed_captures_next_fill`, `lock_ignores_preview_recipes`, `unlock_returns_to_selection`, `lock_state_roundtrip`, `lock_corrupt_recipe_does_not_block_load`, `lock_restore_plays_nothing`, `lock_with_protection_never_overruns_next_event`, `fill_type_forced_overrides_lock` (`TestLock.cpp`) | audible check unverified | Automated; audibility unverified |
| U-1 History and recipes | `history_rolls_after_eight`, `history_excludes_previews`, `history_order_newest_first`, `history_survives_editor_recreate`, `history_roundtrip_state`, `history_bad_entry_dropped`, `audition_recipe_plays_selected`, `lock_this_sets_selected_recipe`, `recipe_file_roundtrip`, `recipe_file_rejects_oversize_malformed_out_of_range`, `describe_recipe_all_algorithms`, `history_queue_overflow_counts_drops`, drawer hosting test (`TestHistory.cpp`) | screenshots at 1020x620 and 1280x760 reviewed locally; DAW unverified | Automated; host unverified |
| C-11 More Like That | `like_biases_but_not_forces`, `like_expires_after_four_started`, `rejected_gaps_dont_count`, `preview_doesnt_count`, `transport_jump_resets`, `zero_weight_stays_zero`, `like_ignored_when_locked`, `family_table_complete`, `draw_count_unchanged` (`TestLock.cpp`, group Like) | audible check unverified | Automated; audibility unverified |
| U-2 Fill Export | `export_segment_matches_wet_bus`, `export_disarmed_zero_cost`, `export_too_old_rejected`, `export_bounded_copy_per_block`, `export_busy_rejected`, `export_samplerate_change_cancels`, `export_wav_header_valid`, `export_no_audio_thread_allocation` (`TestExport.cpp`) | DAW drag-out/import unverified | Automated; DAW unverified |
| F-S06 A/B level matching | `match_off_identity`, `trim_bounded_6dB`, `silent_side_no_trim`, `trim_smoothed_click_free`, `not_saved_in_state`, `recipe_ab_alternates_and_matches`, `swap_does_not_mutate_automation` (`TestAbCompare.cpp`) | audible loudness match unverified | Automated; audibility unverified |

## Test command policy

Use the project scripts and current CI workflow as the source of truth; do not paste commands from an old branch without checking they still exist.

- Fast preflight and local subset: `bash tools/dev/check.sh build-dev`.
- Targeted incremental build: `bash tools/dev/build-target.sh build-dev <tests|vst3|clap|standalone|plugin|all> [filter]` (available after PR #21 or this consolidated PR #24 is merged).
- Full suite: build `GapFillTests`, then run it with no filter; run the Combinatorial group explicitly as well.
- CI/release: follow `docs/VALIDATION.md`; do not treat a fast local subset as a release gate.

**Filter safety:** every test invocation must report the number of tests selected and executed, and exit nonzero for an unmatched filter. This prevents silent green runs. PR #22 adds a negative filter and zero-tests failure; until that change is merged and verified, do not rely on the `-Combinatorial` exclusion form as a complete suite check.

## Golden audio and artifact policy

1. Store a small deterministic fixture or generated signal; do not commit private DAW sessions or user audio without explicit approval.
2. Compare output at a fixed sample rate, block-size sequence, seed, PPQ, transport state and parameter snapshot.
3. Report max absolute error, RMS error, finite/NaN count, and alignment/latency difference. Use project-approved tolerances from engineering standards.
4. A changed golden requires a named feature/fix, explanation of the audible change, and review in a separate labeled PR. Never regenerate goldens automatically to make a failing test pass.
5. Test the actual packaged asset on at least one representative install/host path before release and record checksums.

## Evidence record template

```text
Requirement/ticket:
Commit / PR:
Test names and selected/executed/passed/failed counts:
CI run URLs and job conclusions:
OS / architecture / plugin format / host version:
Sample rate / callback sizes / transport mode:
Manual steps and observed result:
Audio metrics or crash trace:
Known unverified checks:
```
