# F-S08 Glitch Roulette
Status: implementation spec, Gate C candidate. Category FUN, value M, size M. **Extends the existing Surprise Me feature** (`GapFillAudioProcessor::surprise()`, `surpriseParameters()` in `Source/Presets.cpp`, 8 curated profiles, one Undo snapshot per press, `Presets.h` documents the contract). Roulette must reuse that machinery, not copy it.

## 1 Summary & user-visible behaviour
- **ROULETTE** spins to a random, coherent glitch-leaning creative setup. Each spin shows the result in the status line (`Roulette: Glitch Storm, Stutter only`). Spin as often as you like.
- **RESTORE** (button next to it, same MIDI/keyboard routes) returns the creative settings to exactly what they were before the first spin of the session. A session starts at the first spin and ends on Restore, on any manual edit of a creative parameter (the result is then "kept"), on a preset/file load, or on Undo.
- Difference from Surprise: Surprise is a one-shot jump with one Undo entry per press and always leaves Fill Type on Auto. Roulette is a session: many spins, **one** Undo entry, a one-press way back, a glitch-biased profile subset, and on about half of the spins a single-algorithm "locked wheel" (Fill Type set to one algorithm from the profile core, weights kept) for a purer glitch.
- Optional auto-revert (Off / 10 s / 20 s / 40 s, default Off) restores after a timeout so a live experiment cannot be forgotten. Panic (F-S12) also restores.
- Only creative parameters change (same contract as Surprise): detector, protection, latency, spectral lanes, mix and bypass are never touched.

## 2 Success/quality metric
- Restore is bit-exact: all touched parameters return to the normalised values recorded before spin 1, in 100 % of 1000 randomized sessions.
- 1000 spins never touch a parameter outside `creativeParameterIDs()` and never produce an out-of-range value; never repeat the previous profile.
- N spins create exactly 1 Undo entry (Surprise: N entries).
- Existing `surprise()` results are unchanged for a fixed seed after the refactor (characterization test).

## 3 Design
- **Separate prerequisite refactor PR** (AGENTS.md rule 5): in `Source/Presets.cpp/.h` extract `void applySurpriseProfile (juce::AudioProcessorValueTreeState&, juce::Random&, int profileIndex, juce::String& name)` from `surpriseParameters`, keeping `surpriseParameters` as a thin caller with identical RNG draw order. Add `const char* getSurpriseProfileName (int)` and `bool surpriseProfileIsGlitchy (int) noexcept` (indices 1, 2, 5, 6, 7: Drum Edits, Glitch Storm, Pitch Bender, Industrial Grit, Tape Breakdown; test asserts they exist by name). Characterize first with golden parameter dumps for seeds 1..50.
- New module `Source/features/roulette/` (`cmake/sources/roulette.cmake`):
  ```cpp
  class RouletteSession {            // message thread only, owned by the processor
  public:
      explicit RouletteSession (GapFillAudioProcessor&, std::uint32_t rngSeed = 0 /*0 = time based*/);
      juce::String spin();            // returns status text; starts a session if none
      bool restore();                 // false if no active session
      bool isActive() const noexcept;
      void commit() noexcept;         // ends session without restoring (manual edit, preset load)
      void setAutoRevertSeconds (int) noexcept;
      void tick() noexcept;           // from processor 15 Hz timer: auto-revert and listener polling
  };
  ```
  Session start: record `before[id]` (normalised) for every id in `rouletteTouchedIds()` = `creativeParameterIDs()` minus `performanceParameterIDs()`; call `processor.pushUndoSnapshot()` once. Each spin: choose a profile among the glitchy subset (never equal to the previous), call `applySurpriseProfile`, then with 50 % probability set `fillType` to one profile-core algorithm (index `algo+1`) otherwise leave Auto. Spin uses the session's own `juce::Random` (never `randomSource`, never the audio engine RNG; 10-SPEC rule: UI actions must not consume the engine stream).
  End-of-session detection: a `juce::AudioProcessorValueTreeState::Listener` on touched ids ignores changes made inside `spin()/restore()` (guard flag) and calls `commit()` on any other change; `programChangeInProgress` or `applyParameterState` also commits (`GapFillAudioProcessor` calls `roulette.commit()` in `selectPreset`, `resetAllParameters`, `undo`, `applyParameterState`, a four-line hook).
  Restore: write back `before[id]` for ids that still hold their last-spun value; user edits to non-touched parameters (mix etc.) made meanwhile are untouched.
- Commands: `CommandId::GlitchRoulette` and `RouletteRestore` (F-S12 `CommandBindings`) drained on the processor timer (message thread): works with the editor closed, latency up to about 67 ms (documented, not for tight timing; Storm is the tight-timing tool).
- Panic: F-S12's panic request is also delivered to the message thread (drained flag on the same timer) which calls `restore()` and cancels auto-revert.
- Hooks: `PluginProcessor.{h,cpp}` (member, getter, `tick()` in `timerCallback`, commit hooks), editor strip component. No DSP files change.

## 4 Parameters & state
No APVTS parameters (command; Roulette writes existing parameters only, like Surprise). Reserved prefix `fs08_` unused. Runtime-only session (never serialised, as for momentary/command state in F0-6). `<Perf rouletteRevertSec="0"/>` is the only persisted value (0/10/20/40; invalid => 0). Old sessions load unchanged; with the button unused nothing differs (neutral). A project saved mid-session stores the current (spun) parameter values like any edit; it reopens with no session (Restore disabled), recoverable only through Undo within the same editor/processor lifetime.

## 5 Real-time safety
Nothing new runs on the audio thread. Parameter writes use `setValueNotifyingHost` on the message thread exactly as Surprise does (about 18 writes). MIDI commands cross to the message thread through F-S12 atomics. No allocation concerns beyond message-thread containers (`before` is a fixed `std::array` sized by the touched-id count at construction).

## 6 UI
- Slot `PerformanceStrip` (F0-9), shared with F-S07/F-S12: ROULETTE button plus a small RESTORE button (enabled only in an active session). Existing header SURPRISE stays untouched.
- Tooltip: "Spin for a random, matching set of glitch settings. Restore brings back your sound from before the first spin." Restore: "Go back to your settings from before you started spinning." Help "Glitch Roulette vs Surprise" (order 1090). Context menu: Auto-revert, Learn MIDI (Spin / Restore).
- Accessibility: titles "Glitch Roulette spin", "Glitch Roulette restore"; result announced through the status label; Restore disabled state exposed. Keyboard: both reachable in Tab order; Enter/Space activate. Reduced motion: no wheel animation or flash; result is text.
- Narrow: overflow rules shared with F-S07 (strip never hides Panic).

## 7 MIDI/automation/CLAP/host impacts
Parameter writes are visible to hosts like any knob move or Surprise press (write-armed automation records them); `updateHostDisplay()` after each spin/restore. Program name shows "Custom". MIDI binding via `<Commands>`. CLAP/VST3/AU behave identically; no new exported parameters.

## 8 Tests (`Tests/features/TestRoulette.cpp`)
`surprise_results_unchanged_after_extraction` (seeds 1..50) | `roulette_touches_only_creative_ids` | `roulette_excludes_performance_ids` | `roulette_values_in_range_1000_spins` | `roulette_never_repeats_profile` | `roulette_uses_only_glitchy_profiles` | `roulette_locked_wheel_valid_fill_type` | `roulette_restore_bit_exact_1000_sessions` | `roulette_one_undo_entry_for_n_spins` | `roulette_manual_edit_commits_session` | `roulette_preset_load_commits` | `roulette_undo_commits` | `restore_keeps_user_mix_edits` | `roulette_auto_revert_fires_once` | `roulette_panic_restores` | `roulette_does_not_consume_engine_rng` (engine output identical for equal seeds with and without spins that restore) | `roulette_seeded_deterministic` | `roulette_midi_spin_without_editor` | `roulette_no_state_serialised_except_revert_setting` | `roulette_no_audio_thread_activity`. Edge: Restore with no session, spin during A/B swap (A/B excluded from session ownership: swap commits).

## 9 Dependencies & build order
Needs the Surprise extraction PR (above), F-S12 (commands, Panic delivery), F0-9 slot, `performanceParameterIDs()` from the F0 registry. Soft: F-S07 (shares strip), F-S02 (status). No E-1 dependency. Order: Surprise extraction -> F-S12 -> F-S08.

## 10 Effort & risks
M. Risks: hot-file edit to `Presets.cpp` (kept to the extraction, no factory-data change, which the U-10 rule reserves for the preset pass); session ending surprises users (status text "Roulette kept" when a manual edit commits); overlap in the user's mind with Surprise (Help comparison); host write-automation records spins (documented). Owner decision: confirm the glitchy subset (5 of 8 profiles) and the 50 % locked-wheel odds.

## 11 Acceptance checklist
- [ ] Extraction PR merged with unchanged Surprise results
- [ ] Spin/restore contracts (creative-only, bit-exact restore, single undo) proven by tests
- [ ] Session ends correctly on edit, preset load, undo, panic, auto-revert
- [ ] MIDI spin/restore works with editor closed
- [ ] Engine RNG untouched; golden output unchanged when feature idle
- [ ] Help comparing Roulette and Surprise; tooltips; accessible names; screenshot at minimum size
- [ ] Manual check in one DAW per OS that spins/restores are recorded/ignored per host automation mode
