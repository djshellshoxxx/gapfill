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
| FX-0 effect-card framework | empty-chain identity, ramp, tail flush, panic clears, duck on tails (`TestFxChain.cpp`) | per-format validators | Specified |
| SP-0 spectral core | identity reconstruction, latency exact, block invariance, 44.1-192 kHz (`TestSpectralCore.cpp`) | validators; CPU budget | Specified |
| N-FX1 Reverse Swell | landing time +-1 sample, never overlaps protNext, curves, neutral (`TestSwell.cpp`) | audible check on drum loop in a DAW | Specified; audible check unverified |
| N-FX2 Granular Cloud | <= 48 grains, density limit, jitter/pitch bounds, ends before protNext, neutral (`TestCloud.cpp`) | audible check; CPU per voice | Specified |
| N-USE-a Panic & Recover | wet silent <= 5 ms, dry bit-identical, every algo/card, not quantised, not saved (`TestPanic.cpp`) | Hardware button + real DAW | Specified; hardware unverified |
| N-USE-b / F-S01 Health & telemetry | each code with hysteresis, priority, zero cost closed (`TestHealth.cpp`) | Standalone + DAW without sidechain/transport | Specified |
| N-MUS Scale Lock | 12 scales x 12 roots, tie rule, RNG untouched, comb in-scale (`TestScale.cpp`) | audible check in a key | Specified |
| F-L07 / N-VAL Scenes and Bank | quantised boundary exact PPQ, block invariance, loop/seek re-arm, file round trip, hostile import (`TestScenes.cpp`, `TestSceneBank.cpp`) | Looping DAW project on the bar line; cross-OS bank import | Specified; host unverified |
| N-EASY Fix-it | one log per rule, apply/undo bit-exact, forbidden IDs, cooldown (`TestFixit.cpp`) | DAW with automation lane present | Specified |
| F-M10, F-S07, F-S08, F-P02 | hold/latch/timeout, storm pattern grid, roulette no-repeat, forced fill safety (`TestLive.cpp`) | hardware pads/MIDI | Specified; hardware unverified |
| F-M03 timeline, F-D02 events | reason text equals engine record, ring wrap, replay reproduces outcomes (`TestTimeline.cpp`, `TestEvents.cpp`) | screenshots min/default/large | Specified |
| F-D01 / F-D03 / F-D04 | budget and quiet zone, quality caps and Auto hysteresis, contrast ratios (`TestBudget.cpp`, `TestQuality.cpp`, `TestAccess.cpp`) | CPU-limited machine; contrast screenshot | Specified |
| Musical context (F-L09, F-L10, F-L22, F-M21, F-X03, F-X04, F-X05) | groove estimate within 2 ms, motif table, hysteresis, curve shapes, 24-key detection, boundary detection (`TestGroove.cpp` etc.) | host tempo/meter session | Specified; heuristics need listening tests |
| Effect cards (F-M14, F-M15, F-M16, F-M19, F-M20, F-L14, F-L15, F-L19, F-L20, F-L21, F-X01, F-X02) | per-card lists in doc 12 section 7 | per-format validators; audible bypass/automation | One row per card to be split out when its PR starts |
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
