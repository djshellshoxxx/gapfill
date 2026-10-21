# F-M12 Preset browser (completion spec over U-4)

Status: canonical behaviour is `05-SPEC-USABILITY-VALUE.md` U-4 (search, categories, favourites, user preset management, scan caps, tests). Code inspection found conflicts and omissions in U-4; this file lists them and supplies the missing design. It sits on top of three modules specified elsewhere and must reuse them: `F-S03-ui-scale.md` `GlobalSettings`, `F-S11-preset-favorites-next-previous.md` (`PresetCatalog`, `FavouritesStore`, `PresetNavigator`, prev/next/MIDI/Program Change), and `F-S12-safe-return-panic.md` `CommandBindings`.

## 1 Summary & user-visible behaviour
The preset name in the header becomes a button that opens a browser popover: search box, category chips (Subtle, Rhythmic, Glitch, Ambient, Dub, Destructive, Utility, Uncategorised, Favourites), list with name + one-line description, star toggle, and a footer with "Save as user preset...", the F-M11 Apply chip and Close. The existing `<` `>` arrows stay (F-S11 behaviour). Keyboard: typing focuses search, Up/Down move, Enter loads, Esc closes, `Ctrl/Cmd+F` focuses search, Space toggles the star. User preset management: save with name/category/description, rename, delete (OS trash, confirm), reveal in file manager.
Findings against the code and sibling specs:
1. **Location conflict.** U-4 says `<Documents>/GapFill/Presets/<Category>/`; shipped code and F-S11 use `userApplicationDataDirectory/GapFill/Presets` (flat). Keep that root; categories are subfolders under it; flat files show as Uncategorised; no migration, no Documents folder. (Owner decision D1; F-S11 already recommends the same.)
2. **File format.** A `.gapfill` file is the bare APVTS state XML (`saveParameterStateToFile` writes `apvts.copyState().createXml()`), not a root with its own properties. Metadata = optional XML root attributes `name`, `category`, `description`, `author` written at save time. They must be stripped before `replaceState` on load, otherwise they leak into live session state. Caps 48/32/200/48 chars, printable text only; unknown attributes ignored.
3. **Atomic writes.** `xml->writeTo (file)` is not atomic. Save/rename use `juce::TemporaryFile`.
4. **Recursion.** F-S11's `PresetCatalog` is non-recursive; categories need depth <= 2 (root + one category folder). Extend it here (cap/skip rules unchanged: 2000 files, > 1 MB skipped, symlinks skipped, 64 MB total read, metadata via `XmlDocument::getDocumentElement (true)` outer element only).
5. **Factory categories** cannot be added to `Presets.cpp` before the preset pass (hot-file rule). `FactoryMeta.cpp` (name -> category, tags) lives in this feature; a test fails when a factory preset has no entry. Factory renames need an alias entry so favourites (`F:<name>` ids) survive.
6. **Multi-instance settings.** `GlobalSettings` (F-S03) is a message-thread singleton per process; several plugin instances plus the standalone share one file. Requirement added to that module by this ticket: `InterProcessLock`, `reload()` before each mutation, and union-merge for string lists (favourites), with a two-instance test.
7. **Next/prev commands.** U-4 asks for MIDI-learnable `PresetNext/Prev`; F-S11 delivers them via `CommandBindings`. F-M12 adds no parameters for them.

## 2 Success / quality metric
- Find time: in a 5-tester script (find "dub" presets, favourite two, load one with keyboard only) all complete in < 45 s.
- Scan: 2000 synthetic user presets scanned in the background with no message-thread stall > 5 ms; popover opens < 100 ms from cache.
- Safety: 0 crashes/hangs over fuzzed files (truncation, bit flips, 1 MB+ files, symlink loops, unreadable folders); no file outside the preset root read or written.
- Integrity: favourites survive rename/move/restart; two instances editing favourites both persist (merge test).

## 3 Design
Files (new): `Source/features/presetbrowser/{PresetMeta.h,PresetMeta.cpp,FactoryMeta.cpp,Search.h,Search.cpp,UserPresetStore.h,UserPresetStore.cpp,Gui.h,Gui.cpp,Help.cpp}`, `Tests/features/TestPresetBrowser.cpp`, cmake fragments. Edits to F-S11/F-S03 modules are limited to the extensions named in section 1 (recursive scan + metadata fields in `PresetRef`; merge/lock in `GlobalSettings`).
- `PresetRef` (F-S11) gains `category`, `description`, `author` (strings, empty = none). Ids stay `F:<factory name>` / `U:<relative path, '/'>`.
- Search (`Search.h`, pure): tokens split on whitespace, lower-cased; every token must match name, description or category; rank name-prefix > word-start in name > substring in name > description/category; stable `compareIgnoreCase` tie-break. No fuzzy matching in v1.
- `UserPresetStore`: `bool save (name, category, description, juce::String& error)` sanitises (max 48 chars; strips `<>:"/\|?*` and control chars; trims dots and spaces; rejects Windows reserved names CON PRN AUX NUL COM1-9 LPT1-9; case-insensitive collisions get " (2)"); `rename`, `trash` (`File::moveToTrash`, falls back to confirm-delete), `reveal` (`File::revealToUser`). Category folders come from the fixed list (user text maps to it or Uncategorised); file names always via `getFileName()` sanitising (06 section 3). Favourite keys are rewritten on rename and removed on delete.
- Loading stays on the single validated path (`loadParameterStateFromFile`, `selectPreset`); Undo coalescing is F-S11's; quantised loading is F-M11 (browser passes the chosen Apply grid to the same entry point).
- Background scan: one low-priority `juce::Thread` owned by the processor (outlives the editor), stopped in the processor destructor (`stopThread (2000)`), results delivered as an immutable `shared_ptr` snapshot (F-S11 pattern) via `callAsync` guarded by `WeakReference`. Never touches the audio thread or APVTS.
- Hooks in existing files: header preset area (`presetBox` becomes the browser button; the combo/program list stays internally for host programs), `userPresetDirectory()` delegates to the catalog root, `saveParameterStateToFile` gains optional metadata attributes (one defaulted parameter).

## 4 Parameters & state
- NEW APVTS IDs: none. Browsing is not automatable; hosts already expose factory presets as programs. Reserved prefix `fm12_` unused.
- State: favourites, step scope and category filter live in `GlobalSettings` (keys `presets.favourites`, `presets.lastCategory`), not in sessions. `<Perf>` unchanged. Old sessions unaffected; `.gapfill` files without metadata derive name from filename.
- Neutral proof: with the browser never opened, sound/parameters identical (golden, state fixtures); loading an old `.gapfill` yields the same parameter values.

## 5 Real-time safety
No audio-thread code at all. All file IO, scanning, settings and metadata on message/background threads; no lock shared with the audio thread. Scan cancellation checked per file; closing the popover does not cancel (cached for next open).

## 6 UI
- Placement (docs/dev/11): replaces the preset combo in the header at the same footprint, no new row. Popover opens below it; at minimum size it is a 420x360 callout clamped to the editor bounds.
- Names: name button "Preset: <name>. Open preset browser"; star "Favourite, on/off"; list rows "<name>, <category>, favourite". Tooltips: browser "Search and choose presets"; star "Mark as favourite"; save "Save the current sound as a preset"; delete "Move this preset to the trash".
- States: "No presets match 'xyz'"; scan problems "Some files could not be read (3)" with a details list; save errors state what failed and what to do.
- Help (order 2100): where presets live (path from the catalog root), categories, favourites, backup, how to bind Next/Prev on a controller (via F-S12 learn).
- Keyboard & focus as in section 1; focus returns to the name button on close; hit targets >= 24 px; Reduced Motion: no popover animation. Narrow: description column hidden below 1100 px (tooltip instead).

## 7 MIDI / automation / CLAP / host
Next/prev: F-S11 `CommandId::PresetNext/PresetPrev` (CC press or note) plus optional Program Change; unchanged here. Host program list stays factory-only; user presets are not host programs. Selecting through the browser behaves like any preset load for host notifications. No latency impact beyond a preset's own `lookahead`.

## 8 Tests (`Tests/features/TestPresetBrowser.cpp`)
U-4 list plus: `metadata_attrs_stripped_before_apply` | `old_flat_files_listed_uncategorised_same_root` | `recursive_scan_depth_2_only` | `factory_meta_covers_every_factory_preset` | `search_ranking_prefix_before_substring_stable` | `sanitise_reserved_windows_names_and_trailing_dots` | `case_insensitive_collision_suffix` | `atomic_save_leaves_old_file_on_failure` | `scan_skips_symlink_loop_oversize_and_caps_2000` | `scan_stop_on_destruction_no_use_after_free` | `settings_merge_two_instances_favourites_union` | `favourite_key_follows_rename_and_clears_on_delete` | `trash_failure_falls_back_to_confirm` | `fuzz_preset_files_no_crash` (truncate, bit flip, oversize, deep XML) | `popover_keyboard_flow` | `layout_min_size_no_clipping`. Fixtures: temp preset roots with 0/3/2500 files, reserved names, symlink loop (POSIX), corrupt XML set.

## 9 Dependencies & build order
Hard: F-S03 (`GlobalSettings`) -> F-S11 (catalog/navigator/favourites, needs F-S12 `CommandBindings`) -> F-M12. Also F0-6, F0-9. Benefits from F-M04 (visible in both views) and U-10 preset pass (categories informational only). Provides the control surface for F-M11's Apply chip. Schedule in the UI-refactor window with F-M04.

## 10 Effort & risks
Effort L (M if F-S11 has shipped). Risks: (1) existing editor preset code (`refreshPresetList`, `presetSelected`, A/B) is entangled with `presetBox`; extract behind the catalog in a separate refactor PR first; (2) lock/merge on network home folders; (3) trash semantics inside a plugin host process on Windows; (4) users who keep presets elsewhere keep using FILE -> Open.
Owner decisions: D1 keep AppData root (recommended); D2 allow user-defined category folders in a later version.

## 11 Acceptance checklist
- [ ] Existing user presets appear unchanged under Uncategorised; no migration
- [ ] Search, categories, favourites, save/rename/delete/reveal work on Windows, Linux, macOS
- [ ] Metadata stripped on load; saves atomic; fuzz suite clean
- [ ] Background scan capped, stoppable, never blocks message thread > 5 ms
- [ ] Two-instance favourites merge verified
- [ ] No new APVTS IDs; golden and state fixtures unchanged
- [ ] Keyboard/screen-reader flow and 1020x620 layout verified; Help registered
