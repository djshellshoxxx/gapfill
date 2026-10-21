# F-S11 Preset favorites / next-previous
Status: implementation spec, Gate C candidate. Category UX, value M, size M. **Subset of `05-SPEC-USABILITY-VALUE.md` U-4 (Preset browser)**: favourites, prev/next with wrap, MIDI-learnable `PresetNext/Prev`, global settings file. This ticket delivers those parts without the browser popover, and builds the catalog/navigator that U-4 must reuse. Read U-4 first; only gaps and decisions are written here.

Existing behaviour: the header has `prevPreset/nextPreset` arrows that step through **factory presets only** (`PluginEditor.cpp` ~l.168), user presets (`*.gapfill` in `<userApplicationData>/GapFill/Presets`, non-recursive, scanned on the message thread by `refreshPresetList`) are reachable only from the dropdown, there is no favourites list, no MIDI/keyboard stepping, and every `selectPreset` pushes an Undo snapshot (32-step stack). Note: U-4 says user presets live in `<Documents>/GapFill/Presets`; the code uses `userApplicationDataDirectory`. This ticket keeps the code location; U-4 text needs correcting (owner decision, default: keep code location).

## 1 Summary & user-visible behaviour
- **Favourites:** FILE menu "Add current preset to favourites" / "Remove from favourites" (also `Ctrl/Cmd+D`). The preset dropdown shows a "Favourites" section first.
- **Step scope:** FILE menu "Arrows step through: All presets / Favourites only" (default All). With Favourites only and none saved, stepping falls back to All and says "No favourites yet - stepping all presets".
- **Arrows now step factory + user presets** (order: factory index order, then user presets sorted by name, case-insensitive), wrapping at both ends. Invalid user files are skipped with "Skipped X: not a valid GapFill preset".
- **Keyboard:** `Ctrl/Cmd+Up` previous, `Ctrl/Cmd+Down` next while the editor has focus. **MIDI:** `PresetNext` / `PresetPrev` bindings (CC press or note, via F-S12 `CommandBindings`), works with the editor closed; optional **MIDI Program Change** (default off) selects factory program n.
- **Undo coalescing:** a burst of steps (each within 3 s of the previous) creates **one** Undo entry, so browsing 20 presets does not push real work out of the 32-step stack. Undo returns to the sound from before the burst.

## 2 Success/quality metric
- Stepping N times from any start visits every eligible preset exactly once per cycle and returns to the start (property test, N up to 2025 presets).
- 20 consecutive steps leave exactly 1 new Undo entry; Undo restores the pre-burst parameters bit-exactly.
- Step to audible new sound <= 1 timer tick (67 ms) + preset load for MIDI; <= 1 frame (UI) for arrows. Catalog refresh never blocks the message thread > 5 ms (directory scan on a background job).

## 3 Design
New module `Source/features/presetnav/` (`cmake/sources/presetnav.cmake`):
```cpp
struct PresetRef { enum class Kind { Factory, User } kind; juce::String id; juce::String displayName; int factoryIndex; juce::File file; };
// id: "F:<factory name>" or "U:<path relative to the preset root, '/' separators>"
class PresetCatalog {                                  // message thread; immutable snapshots
public:
    using Snapshot = std::shared_ptr<const std::vector<PresetRef>>;
    Snapshot get() const noexcept;                     // factory + last scanned user list
    void rescanAsync (std::function<void()> onDone);   // background job; cap 2000 files; skip > 1 MiB; non-recursive for now
};
class FavouritesStore {                                // GlobalSettings key "presets.favourites" (string list, cap 500, entries <= 260 chars)
public:
    bool isFavourite (const juce::String& id) const; void set (const juce::String& id, bool fav);
};
class PresetNavigator {
public:
    enum class Scope { All, Favourites };
    PresetNavigator (GapFillAudioProcessor&, PresetCatalog&, FavouritesStore&, GlobalSettings&);
    bool step (int direction /* +1, -1 */);            // returns false if nothing loadable
    bool loadById (const juce::String& id);
    juce::String currentId() const;                    // runtime only
    juce::String lastStatus() const;
};
```
- `step`: take the catalog snapshot, filter by `Scope`, find the index of `currentId` (unknown/Custom => before first for +1, after last for -1), move with wrap, load; on a failed user load try the next entry (max 5 tries). Factory load = `GapFillAudioProcessor::selectPreset (index, pushUndo)`; user load = `loadParameterStateFromFile (file, error, pushUndo)`. Both existing methods gain a trailing defaulted parameter `bool pushUndo = true` (signature-compatible; the one processor hook). The navigator pushes one snapshot itself when the previous step was more than 3 s ago.
- Processor keeps `lastLoadedPresetId` (runtime string guarded by message-thread access) so MIDI stepping works without an editor; the editor's `userPresetFiles` list and `refreshPresetList` are rebuilt from `PresetCatalog` (previous entry points kept until callers migrate; characterize first).
- Commands: F-S12 `takePressed (PresetNext/PresetPrev)` drained in `GapFillAudioProcessor::timerCallback` (15 Hz). Program Change: `CommandBindings::processMidi` stores the last PC number in `std::atomic<int> pendingProgram` (-1 none) honouring `<Perf> programChangeChannel` (-1 any); the timer calls `setCurrentProgram` for 0 <= n < `getNumPrograms()`; others ignored. Bank Select is ignored.
- `GlobalSettings` (F-S03) holds favourites and `presets.stepScope`; settings unreadable => no favourites, scope All, no crash.

## 4 Parameters & state
No APVTS parameters (browsing is not automatable; hosts already expose programs). Reserved `fs11_` unused. Favourites and scope are per-user global settings, not in the project; `<Perf>` gains `respondProgramChange` (bool, default false) and `programChangeChannel` (-1..15, default -1). Current preset identity is runtime only (the project still stores `program` for factory presets exactly as today). Old sessions load unchanged; with no favourites, bindings or PC option nothing differs from today except that arrows can now reach user presets (documented behaviour change; test-covered).

## 5 Real-time safety
Audio thread only does the MIDI scan already added by F-S12 (plus one atomic store for Program Change). All catalog IO, file parsing and preset loading are on the message thread or a background job; the catalog snapshot is an immutable `shared_ptr`, so a rescan during a step cannot invalidate it. No allocation on the audio thread.

## 6 UI
- Placement: no new permanent control. Arrows already exist; favourites live in the FILE menu and dropdown section; stepping scope in the FILE menu. A small status message on every step: `Preset 12/25: Dub: Echo Throw` (F-S02 `StatusSource`).
- Tooltips: arrows "Previous / next preset (Ctrl/Cmd+Up / Down). Steps through favourites only if you chose that in the File menu." Help section "Presets, favourites and stepping" (order 1110) including MIDI setup.
- Accessibility: menu items named with their check state ("Add to favourites" / "Remove from favourites"), step result announced via the status label, keyboard shortcuts documented in Help. Reduced motion: none involved. Narrow: nothing added to the layout.

## 7 MIDI/automation/CLAP/host impacts
Program Change reaches an effect only if the host forwards it (many hosts treat PC as host-level; verify in REAPER, Bitwig, FL; Live and Logic caveats as in F-S04). Host program list = factory presets only (unchanged); user presets are not host programs. Stepping triggers normal parameter notifications and `updateHostDisplay()`. A preset with a different `lookahead` changes reported latency (existing behaviour; F-S10 presets avoid it). Hosts with parameter-write automation armed record the jumps like any preset load.

## 8 Tests (`Tests/features/TestPresetNav.cpp`)
U-4 subset: `next_prev_wraps` | `favourites_roundtrip` | `user_scan_caps_and_skips_oversize` | `legacy_flat_files_listed` | `load_pushes_undo` | `settings_file_unreadable_defaults`. Added: `step_visits_all_once_per_cycle` | `step_scope_favourites_only` | `step_favourites_empty_falls_back_to_all` | `step_skips_invalid_user_file_max_5` | `step_burst_creates_single_undo_entry` | `undo_after_burst_restores_pre_burst_state` | `step_from_custom_state_starts_at_edge` | `favourite_identity_factory_vs_user_same_name` | `favourite_missing_file_hidden_not_fatal` | `favourites_cap_500` | `catalog_rescan_during_step_uses_snapshot` | `catalog_scan_off_message_thread` | `midi_next_prev_without_editor` | `program_change_channel_filter_and_range` | `program_change_default_off` | `press_flood_coalesces` | `shortcut_keys_step` | `processor_select_preset_default_arg_unchanged` (old callers). Fixtures: temp preset folder with 3 valid, 1 corrupt, 1 oversize, 1 non-`.gapfill` file.

## 9 Dependencies & build order
Hard: F-S03 (`GlobalSettings`), F-S12 (`CommandBindings`). Soft: F-S02 (status), F-S10 (more presets to step through), U-4 (reuses `PresetCatalog/FavouritesStore/PresetNavigator`; U-4 must not create a second settings file or scanner). Order: F-S03 -> F-S12 -> F-S11 -> U-4.

## 10 Effort & risks
M. Risks: moving user-preset scanning out of the editor (characterization test first); stepping into a preset that changes lookahead (PDC) during a set; large preset folders on network drives (background scan, 2000 cap); Program Change semantics differing per host; shortcut conflicts with host key handling (verify `Ctrl/Cmd+Up/Down` and `Ctrl/Cmd+D` in REAPER, Live, FL, Logic, Bitwig; make configurable only if a conflict is found). Owner decision: preset root location (code vs U-4 text) and whether arrows should include user presets by default.

## 11 Acceptance checklist
- [ ] Favourites add/remove/persist; dropdown section; scope setting works
- [ ] Arrows, keyboard and MIDI step through factory + user presets with wrap and invalid-file skipping
- [ ] One Undo entry per burst; Undo restores exactly
- [ ] Catalog scan is async, capped, and resilient to corrupt/oversize files
- [ ] Optional Program Change works only when enabled; channel filter honoured
- [ ] Old callers of `selectPreset` / `loadParameterStateFromFile` behave identically
- [ ] Help, tooltips, accessible names, status messages; minimum-size screenshot
- [ ] Golden output and old-session loading unchanged
