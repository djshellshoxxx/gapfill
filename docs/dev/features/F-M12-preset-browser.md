# F-M12 Preset browser (completion spec over U-4)

Status: canonical behaviour is `05-SPEC-USABILITY-VALUE.md` U-4 (search, categories, favourites, user preset management, scan caps, settings file, tests). Code inspection found conflicts and omissions in U-4; this file lists them and supplies the missing design. F-S11 (favourites + next/prev) is a subset of this ticket.

## 1 Summary & user-visible behaviour
The preset name in the header becomes a button that opens a browser popover: search box, category chips (Subtle, Rhythmic, Glitch, Ambient, Dub, Destructive, Utility, Uncategorised, Favourites), list with name + one-line description, star toggle, prev/next arrows beside the name, and a footer with "Save as user preset...", the F-M11 Apply chip, and Close. Keyboard: typing focuses search, Up/Down move, Enter loads, Esc closes, `Ctrl/Cmd+F` focuses search, Space toggles the star. User preset management: save with name/category/description, rename, delete (moves to the OS trash, confirm), reveal in file manager. Findings against the code:
1. **Location conflict.** U-4 says `<Documents>/GapFill/Presets/<Category>/`; the shipped editor uses `userApplicationDataDirectory/GapFill/Presets` (flat, `PluginEditor.cpp` `userPresetDirectory()`). Existing users' files live there. Keep the existing root; categories are subfolders under it; flat files show as Uncategorised. No migration, no Documents folder. (Owner decision D1.)
2. **File format.** A `.gapfill` file is the bare APVTS state XML (`saveParameterStateToFile` writes `apvts.copyState().createXml()`), not a root with its own properties. Metadata = optional XML root attributes `name`, `category`, `description`, `author` written at save time. On load these must be stripped before `replaceState`, otherwise they leak into the live session state. Unknown attributes are ignored; length caps 48/32/200/48 chars, printable text only.
3. **Atomic writes.** `xml->writeTo (file)` is not atomic. All browser writes (save, rename, favourites) use `juce::TemporaryFile`.
4. **Next/prev "MIDI-learnable commands".** `MidiLearnManager` maps CC to parameter index only. Solution: two momentary bool parameters `fm12_next`, `fm12_prev` (non-automatable, never "on" at rest) that the existing learn UI can bind; a rising edge is consumed on the message thread (also settable from the header `<` `>` buttons, which become parameter-backed so they get the standard right-click learn menu).
5. **Factory categories** cannot be added to `Presets.cpp` before the preset pass (hot-file rule). A table `FactoryMeta.cpp` (name -> category, tags) lives in the feature; a test fails if a factory preset has no entry.
6. **Multi-instance settings.** Several instances (and the standalone) share one settings file; naive load/save clobbers favourites.

## 2 Success / quality metric
- Find time: in a 5-tester script (find "dub" presets, favourite two, load one with keyboard only) all complete in < 45 s.
- Scan: 2000 synthetic user presets scanned in the background with the message thread never blocked > 5 ms per frame (timing test); popover opens < 100 ms with a cached list.
- Safety: 0 crashes/hangs over fuzzed files (random truncation, bit flips, 1 MB+ files, symlink loops, unreadable folders); no file outside the preset root is read or written.
- Data integrity: favourites survive rename/move/restart; concurrent edits from two instances both persist (merge test).

## 3 Design
Files: `Source/features/presetbrowser/{PresetCatalog.h,PresetCatalog.cpp,PresetMeta.h,PresetMeta.cpp,FactoryMeta.cpp,Settings.h,Settings.cpp,UserPresetStore.h,UserPresetStore.cpp,Gui.h,Gui.cpp,Help.cpp}`, `Tests/features/TestPresetBrowser.cpp`, `Source/params/fm12.{h,cpp}`, `tools/param_ids/fm12.lock`, cmake fragments.
- `struct PresetEntry { juce::String key; juce::String name, category, description, author; bool factory; int factoryIndex; juce::File file; bool favourite; };` key = `factory:<name>` or `user:<path relative to root, '/' separators>`; factory renames require an alias in `FactoryMeta.cpp` so favourites survive.
- `PresetCatalog` (processor-owned, so it outlives the editor): `void refreshAsync()`; `std::vector<PresetEntry> snapshot() const` (copy under a short `SpinLock`, message thread only); listener callbacks via `juce::MessageManager::callAsync` guarded by a `WeakReference`. Scan runs on one `juce::Thread` (low priority): depth <= 2, `findChildFiles` for `*.gapfill`, skip symlinks and files > 1 MB or 0 bytes, cap 2000 files and 64 MB total read, metadata read via `XmlDocument::getDocumentElement (true)` (outer element only; no full parse), cache keyed `(path, mtime, size)`. Thread is stopped (`stopThread (2000)`) in the processor destructor; never touches the audio thread or APVTS.
- Search: lower-cased tokens split on whitespace, all tokens must match name, description or category; rank: name prefix > word start in name > substring in name > description/category; stable alphabetical tie-break (`compareIgnoreCase`). No fuzzy matching in v1.
- `Settings` (`juce::SharedResourcePointer<GapFillSettings>` so instances in one process share it): `PropertiesFile` at `<userApplicationDataDirectory>/GapFill/settings.xml`, `millisecondsBeforeSaving = 500`, `processLock = InterProcessLock ("GapFill-settings")`, `reload()` before each mutation, favourites stored as a sorted set of keys (union-merge on conflict), also legend collapsed / UI scale / first-run keys owned by other tickets (namespaced keys). Unreadable or malformed file -> defaults silently, never fatal. No network, no telemetry.
- `UserPresetStore`: `bool save (name, category, description, juce::String& error)` (sanitises: max 48 chars; strips `<>:"/\|?*` and control chars; trims dots/spaces; rejects Windows reserved names CON PRN AUX NUL COM1-9 LPT1-9; case-insensitive collision -> " (2)"); `rename`, `trash` (`File::moveToTrash`, falls back to confirm-delete if unsupported), `reveal` (`File::revealToUser`). Category folder names come from a fixed list; user-typed categories map to the nearest fixed one or Uncategorised (no arbitrary folder creation in v1). Paths never built from user text without `getFileName()` sanitising (06 section 3).
- Loading stays on the single validated path (`loadParameterStateFromFile`, `selectPreset`), which already pushes Undo; quantised loading is F-M11.
- Hooks: header `presetBox` area (button + popover via `juce::CallOutBox`), `prevPreset/nextPreset` become parameter-backed buttons, `userPresetDirectory()` moves behind `UserPresetStore::root()`, processor timer consumes `fm12_next/prev` edges.

## 4 Parameters & state
New group `GAPFILL_PARAMS_FM12` appended after FM10 in the permanent group order. Files `Source/params/fm12.{h,cpp}`, lock `tools/param_ids/fm12.lock`, version hint 1:

| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fm12_next` | bool, non-automatable, momentary | off | n/a |
| `fm12_prev` | bool, non-automatable, momentary | off | n/a |

- The two triggers are written back to 0 by the message thread after each edge, excluded from presets, Undo, A/B and Random; saved value is always 0 (`performanceParameterIDs()`). `MidiLearnManager` must be verified to list non-automatable parameters (it builds from `allParameterIDs()`, which is generated from the X-macro, so it does).
- State: browser UI prefs in `<Perf>` (`lastCategory`, `listScroll` not persisted). Favourites live in the settings file, not in sessions. Old sessions: unaffected. Metadata attributes absent -> derived from filename.
- Neutral proof: with the browser never opened, sound/parameters identical (golden); loading an old `.gapfill` yields the same parameter values (state fixtures).

## 5 Real-time safety
No audio-thread code. The only audio-adjacent surface is the two parameters (ParamCache atomics are never read by DSP). All file IO, scanning and settings on message/background threads; the scan thread holds no lock the audio thread can take. Cancellation: `signalThreadShouldExit` checked per file; popover close does not cancel an in-progress scan (cached for next open).

## 6 UI
- Placement (docs/dev/11): replaces the preset combo in the header (same footprint, no new row). Popover opens below it; at minimum size it is a 420x360 callout that never exceeds the editor bounds.
- Name button: accessible name "Preset: <name>. Open preset browser". Star: "Favourite, on/off". Arrows: "Previous preset" / "Next preset" (wrap-around over All, factory first then user alphabetical; F-S11 adds favourites-only stepping).
- Tooltips: browser "Search and choose presets"; star "Mark as favourite"; save "Save the current sound as a preset"; delete "Move this preset to the trash".
- Empty/error states: "No presets match 'xyz'"; scan errors "Some files could not be read (3)" with details list; save errors state what failed and what to do.
- Help (order 2100): where presets live (per OS path shown from `root()`), categories, favourites, how to back up, how to learn Next/Prev on a controller.
- Keyboard & focus as in section 1; focus returns to the name button on close; hit targets >= 24 px; Reduced Motion: no popover animation.
- Narrow: description column hidden below 1100 px (shown in a tooltip).

## 7 MIDI / automation / CLAP / host
Next/prev via CC using normal learn (CC >= 64 edge). Host program list (`getNumPrograms/setCurrentProgram`) is unchanged and remains factory-only; host program changes are immediate (see F-M11). User presets are not exposed as host programs. CLAP/VST3: two non-automatable parameters are hidden from automation lists where hosts honour the flag. No latency impact.

## 8 Tests (`Tests/features/TestPresetBrowser.cpp`)
U-4 list plus: `metadata_attrs_stripped_before_apply` | `old_flat_files_listed_uncategorised_same_root` | `factory_meta_covers_every_factory_preset` | `search_ranking_prefix_before_substring_stable` | `sanitise_reserved_windows_names_and_trailing_dots` | `case_insensitive_collision_suffix` | `atomic_save_leaves_old_file_on_failure` | `scan_skips_symlink_loop_and_oversize_and_caps_2000` | `scan_cancel_on_destruction_no_use_after_free` | `settings_merge_two_instances_favourites_union` | `favourite_key_survives_rename` | `trash_failure_falls_back_to_confirm` | `fm12_edge_consumed_once_and_reset` | `fm12_not_saved_in_preset_or_undo` | `next_prev_wraps_factory_then_user` | `fuzz_preset_files_no_crash` (truncate/bit flip/oversize/deep XML) | `popover_keyboard_flow` | `layout_min_size_no_clipping`. Fixtures: temp preset roots with 0/3/2500 files, Windows-reserved names, symlink loop (POSIX), corrupt XML set.

## 9 Dependencies & build order
Needs F0-1, F0-6, F0-9; benefits from F-M04 (visible in both views) and U-10 preset pass (categories only informational). Provides the control surface for F-M11's Apply chip and absorbs F-S11. Schedule in the UI-refactor window with F-M04/F-S03. Command-learn table is not needed (parameters used).

## 10 Effort & risks
Effort L. Risks: (1) the existing editor preset code (`refreshPresetList`, `presetSelected`, A/B) is entangled with `presetBox`; extract behind `PresetCatalog` in a separate refactor PR first; (2) cross-process settings file locking on network homes; (3) Windows trash semantics for plugin-hosted processes; (4) users who keep presets elsewhere (use FILE menu Open, unchanged).
Owner decisions: D1 keep AppData root (recommended) vs move to Documents; D2 allow user-defined category folders in a later version.

## 11 Acceptance checklist
- [ ] Existing user presets appear unchanged under Uncategorised; no migration
- [ ] Search, categories, favourites, save/rename/delete/reveal work on Windows, Linux, macOS
- [ ] Metadata stripped on load; saves are atomic; fuzz suite clean
- [ ] Background scan capped, cancellable, never blocks message thread > 5 ms
- [ ] Two-instance favourites merge verified
- [ ] `fm12_*` IDs locked, never persisted ON, learnable on a CC
- [ ] Keyboard/screen-reader flow and 1020x620 layout verified; Help registered
