# F-L02 Fill History and Save Recipe
Status: implementation spec, Gate C candidate (backlog `08` F-L02, existing ticket U-1). Category UX/VAL, value H, size L. Canonical behaviour: `05-SPEC-USABILITY-VALUE.md` U-1 (8-entry strip, `.gfrecipe`, `RecipeHistory` owned by the processor per revision R2). Not repeated here; this file adds the missing format, file-system, threading and UI detail.

## 1 Summary & user-visible behaviour
- A strip under the visualiser shows the last 8 **natural** fills, newest first, as chips: `Stutter 1/32 - High - 180 ms`, with a letter/icon for the algorithm family (colour is secondary). Previews are never listed.
- Chip actions (click selects; right-click or the action row): **Audition this** (needs F-M01/E-1; hidden until present), **Lock this** (F-L01), **More like this** (F-L03), **Save recipe...**, **Export fill...** (F-L04; hidden until present). Menu on the strip: **Load recipe...**, **Open recipes folder**.
- **Save recipe** writes `<Documents>/GapFill/Recipes/<name>.gfrecipe` (<= 1 KiB). **Load recipe** reads one file and places it into Lock as *armed-with-recipe, Lock off* (user decides when to engage) and selects a chip-like "Loaded" entry above the strip; it does not play anything.
- History survives closing the editor and is saved with the project (`<History>`). Loading a project never plays or replays anything.
- Entries are never merged or de-duplicated in v1 (strip = literally the last 8 fills); a repeat counter is explicitly out of scope.

## 2 Success/quality metric
- From a fill being heard to a saved recipe file: <= 3 interactions (select chip, Save, confirm name).
- Round-trip: a saved then loaded recipe equals the original in all 16 bytes except `flags` (preview/lock/bias bits cleared) and `seed` (kept).
- The strip shows a new fill within 100 ms of 15 Hz timer tick after `FillStarted` (two timer ticks max), 0 dropped natural fills at 100 fills/second stress (drops are counted and displayed in the debug overlay only).

## 3 Design
Files (cmake `cmake/sources/history.cmake`, tests `Tests/features/TestHistory.cpp`):
- `Source/features/history/RecipeHistory.{h,cpp}` (message thread, **owned by the processor**): `class RecipeHistory { static constexpr int capacity = 8; void push (const HistoryEntry&); const HistoryEntry& at (int newestFirst) const; int size() const; std::uint32_t generation() const; void clear(); }` with `struct HistoryEntry { FillRecipe recipe; std::int64_t outputPos; double bpmAtStart; std::uint32_t serial; }`. `generation` is a `std::atomic<uint32_t>` bumped on every change; the editor polls at 15 Hz and repaints on change.
- `Source/features/history/Describe.{h,cpp}`: `juce::String describeRecipe (const FillRecipe&, double bpm)` and `const char* shortAlgoName (Algo)`; string-table based (no concatenated English), `bpm <= 0` -> omit tempo-dependent text. Division text from `Division` names; length as `~180 ms` rounded to 10 ms.
- `Source/features/history/RecipeFile.{h,cpp}`: pure, no GUI. 
  ```cpp
  struct RecipeFileResult { bool ok; FillRecipe recipe; juce::String name; juce::String error; };
  RecipeFileResult readRecipeFile (const juce::File&);                       // size cap 4096 B, message thread / worker
  bool writeRecipeFile (const juce::File&, const FillRecipe&, juce::StringRef name, juce::String& error);
  juce::String sanitiseRecipeName (juce::StringRef);   // 1..48 chars, printable, no path separators or ".." , trims, falls back to "Recipe"
  ```
  Format (`.gfrecipe`, XML, UTF-8): `<GapFillRecipe format="1" name="..." recipe="<RecipeCodec 24 chars>" appVersion="0.x.y" stateVersion="2"/>`. Reader rules: root tag exact, `format` integer 1 (higher -> "Recipe is from a newer GapFill"), attributes length-capped (name 48, recipe 24, appVersion 32), unknown attributes ignored, `File::getFileName()` only (no path from file contents), decode via `decodeRecipe` (F-L01), `flags` forced to 0 except none. Writer: temp file in the same folder then `moveFileTo` (atomic replace); create folder with `createDirectory()`; collision -> append ` (2)`..`(99)`; errors returned, never swallowed.
- `Source/features/history/FileActions.{h,cpp}` (GUI side): uses `juce::FileChooser::launchAsync` (the build sets `JUCE_MODAL_LOOPS_PERMITTED=0`, so no `browseForFileToSave`); chooser instance owned by the editor component and reset on destruction; callbacks check a `SafePointer` before touching the editor.
- `Source/features/history/Gui.{h,cpp}`, `Help.cpp`, `Info.cpp`; registered in slot `CreativeExtra` (strip) with order after Lock.
Data flow: audio thread `FillEvent::FillStarted(recipe, pos)` (natural only) -> processor 15 Hz timer drains -> `RecipeHistory::push`. Actions on a chip push commands (`LockRecipe`, `LikeRecipe`, `PreviewRecipe`, `ExportSegment`) via the SPSC queue; none of them mutate history.
Minimal hooks in existing files: processor owns one `RecipeHistory` member and drains `fillEvents` in `timerCallback` (call into `history/Drain.h`); `StateSubtrees` registry entry; nothing in `FillEngine` beyond F0-4 event emission.

## 4 Parameters & state
No APVTS parameters (everything is a command or state; nothing here is meaningful to automate). Prefix rule satisfied vacuously.
State: `<History v="1" count="n"><E r="<codec>" p="<outputPos>" b="<bpm*100>"/>...</History>`, <= 8 entries, <= 1 KiB. Validation per entry: codec decodes, `isValid`; invalid entries are dropped individually, the rest load (U-1). `outputPos` is stored but only used to disable Export ("That fill is too old") after reload. Old sessions: subtree absent -> empty history. Neutral proof: collecting events has no effect on DSP; with 0 events the output is untouched; golden A-F unchanged.
User prefs (recipe folder override) are not persisted in v1; the fixed folder is `File::getSpecialLocation (userDocumentsDirectory)/GapFill/Recipes`.

## 5 Real-time safety
Audio thread only emits `FillStarted` (one 40-byte POD push per started fill, non-blocking). `RecipeHistory`, file I/O, `describeRecipe`, `FileChooser` are message-thread only. Overflow: full `fillEvents` drops the newest event and increments `droppedEvents`; History simply misses that fill (documented, tested). Folder listing for "Load" runs on a `juce::ThreadPool` job (cap 200 files, ignore > 4 KiB, no recursion) and posts the result with `MessageManager::callAsync` guarded by a `SafePointer`.

## 6 UI
Placement per `11`: compact strip inside the visualiser/Creative area, not the header; collapses to the most recent chip plus "..." menu at minimum width. Each chip: accessible name = `describeRecipe` text ("Fill 1 of 8, Stutter 1/32, high band, about 180 milliseconds"), roles button; arrow keys move between chips in keyboard mode, Enter opens the action menu, Delete is not bound. Tooltips: chip "Click to select. Right-click for Lock, More like this, Save." Save dialog default name `Algo_Division_ms`. Reduced Motion: new chips appear without slide/fade. Error text appears in the footer status line for 4 s ("Recipe file is too large"). Help section "Fill History" (order 1110).

## 7 MIDI/automation/CLAP/host impacts
None in the host: no parameters, no latency. MIDI-learnable actions come from F-S12's `CommandBindings` when present (Lock latest, Like latest); chip-specific actions are mouse/keyboard only. Recipe files are host-independent. Standalone uses the same folder. Hosts that restore state while the editor is closed (all) still populate history.

## 8 Tests
Group `History`: `history_rolls_after_eight` | `history_excludes_previews` | `history_order_newest_first` | `history_survives_editor_recreate` | `history_roundtrip_state` | `history_bad_entry_dropped_rest_loaded` | `history_queue_overflow_counts_drops` | `describe_recipe_all_15_algorithms` | `describe_recipe_no_tempo` | `recipe_file_roundtrip` | `recipe_file_rejects_oversize_malformed_out_of_range` | `recipe_file_rejects_path_in_name` | `recipe_file_newer_format_message` | `recipe_file_write_atomic_on_collision` | `recipe_file_fuzz_truncation_bitflip` (in `TestFuzzFiles.cpp`) | `load_recipe_arms_lock_but_leaves_it_off` | `history_no_audio_thread_allocation` | `export_disabled_for_reloaded_entries`. Fixtures: `Tests/fixtures/recipes/{good,oversize,badcrc,future,traversal}.gfrecipe`.

## 9 Dependencies on other tickets
Hard: B3 (`FillRecipe`), B4 (queues), B6 (subtrees), B5 (slots), F-L01 (`RecipeCodec`). Soft (actions appear when present): E-1/F-M01 Audition, F-L01 Lock, F-L03 Like, F-L04 Export. Build order: F-L01 -> F-L02 -> F-L03/F-L04.

## 10 Effort & risks
Effort L. Risks: async chooser lifetime on editor close (use `SafePointer`, owner on editor); Documents folder not writable (sandboxed hosts) -> clear error and chooser fallback; large Windows paths; AU/VST3 hosts that block file dialogs (shown as error, not hang); strip crowding at minimum width.

## 11 Acceptance checklist
- [ ] 8-entry ring, newest first, previews excluded, persisted and per-entry validated
- [ ] `.gfrecipe` reader/writer bounded, atomic, fuzzed; all error strings user-facing
- [ ] Load puts the recipe into Lock without engaging it
- [ ] Async dialogs only; no modal loops; no crash on editor close during dialog
- [ ] Strip accessible, keyboard operable, correct at minimum window size
- [ ] No parameters added; golden and old-state fixtures unchanged
- [ ] AllocGuard and TSan queue tests green
- [ ] Manual: save/load recipe on Windows and Linux standalone and one host
