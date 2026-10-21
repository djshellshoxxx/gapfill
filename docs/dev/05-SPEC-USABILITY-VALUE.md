# 05 - Usability and value features

Tickets: **U-1** Fill History (+ Save Recipe), **U-2** Fill Export, **U-3** MIDI note
trigger, **U-4** Preset browser, **U-5** Simple / Advanced view, **U-6**
Auto-Calibrate, **U-7** First-run guide, **U-8** UI scale, **U-9** Parameter info
and status line, **U-10** Preset pass and quality gate, plus a backlog list.

Global rule for every UI ticket here: (1) lives in an editor **slot** (F0-9) or its
own overlay, (2) tooltip + accessibility title/description + keyboard focus (opt-in
keyboard mode), (3) respects Reduced Motion, (4) survives editor close/re-open and
window resize at the minimum size, (5) adds a Help section through the registry.

---

## U-1 Fill History and recipes   (size L, depends F0-3, F0-4, F0-6, F0-9, C-5 for Lock)

### Behaviour
* A strip of the last **8 natural fills**, newest first. Chip text from
  `describeRecipe()`: `Stutter 1/32 - High - 180 ms`; colour by algorithm family
  (same palette as the visualiser; also an icon/letter, never colour-only).
  Previews are not listed.
* Click selects a chip. Right-click or the strip's action row offers:
  **Audition this** (plays that exact recipe through the preview voice, §E-1 addendum),
  **Lock this** (C-5), **More like this** (C-11), **Save recipe...**.
* **Save recipe** writes a `.gfrecipe` file (XML: `name`, `recipe` base64, `appVersion`,
  `stateVersion`; <= 1 KB) to `<Documents>/GapFill/Recipes/`. **Load recipe...** (menu on
  the strip) validates and puts it into Lock (armed, off) so the user decides when to
  engage. Same bounded-load rules as presets (size cap 4 KiB, atomic validation,
  user-facing error text).

### Design
* `RecipeHistory` (message thread, **owned by the processor**, R2): 8-entry ring plus
  `std::atomic<uint32_t> generation`; the processor timer fills it from `fillEvents`
  (`FillStarted`). The editor polls `generation` at 15 Hz and repaints on change.
* Persisted in `<History>` (<= 8 x 16 B base64 + a count). Invalid recipe entries are
  dropped individually; the rest of the state loads.
* `describeRecipe (const FillRecipe&, double bpm)` is pure, localisation-ready (string
  table), and unit-tested for all algorithms.
* **E-1 addendum**: `Command::PreviewRecipe` carries a recipe; `startPreview` accepts an
  optional `const FillRecipe*` and then skips `chooseAlgorithm`/band/length selection
  (clamping length to `min (recipe.lengthMs, 2000)`), everything else identical.

### Tests
`history_rolls_after_eight` | `history_excludes_previews` | `history_order_newest_first` |
`history_survives_editor_recreate` | `history_roundtrip_state` | `history_bad_entry_dropped` |
`audition_recipe_plays_selected` | `lock_this_sets_selected_recipe` | `recipe_file_roundtrip` |
`recipe_file_rejects_oversize_malformed_out_of_range` | `describe_recipe_all_algorithms` |
`history_queue_overflow_counts_drops`.

---

## U-2 Fill Export (stem of what GapFill added)   (size L, depends E-1, F0-4)

Value: users can keep the fills they liked as audio (sampling, layering, auditioning
outside the plugin). Optional per `include.md` (effects are exempt from "Export audio"),
so it is labelled **Export Fill** and lives in the FILE menu and the History strip.

### Behaviour
* **Export Fill...** exports the **wet bus** (only what GapFill generated, post
  Low/Centre protect and width, pre dry/wet mix) of the selected History fill, or of the
  last fill / preview when nothing is selected, as a 24-bit WAV at the session sample
  rate, with 20 ms lead-in and the voice's fade-out tail. File name default
  `GapFill_<Algo>_<bpm>bpm_<yyyyMMdd-HHmmss>.wav`.
* Drag handle on the History chip: drags the temp WAV into the DAW
  (`DragAndDropContainer::performExternalDragDropOfFiles`).
* Not available when the buffer is older than the ring (shows "That fill is too old");
  ring length 8 s.

### Engine design (no locks, bounded work)
* `WetTap`: a preallocated stereo ring (`8 s x channels x float`, allocated in
  `prepareToPlay`; at 192 kHz stereo about 12 MB - allowed only because it is armed
  on demand) written once per block from `wetBuffer` **only while armed**. The editor arms
  it when the History strip or the FILE menu is first used in the session and disarms
  after 60 s idle. Disarmed => zero extra work.
* Each `FillStarted` / `PreviewStarted` event carries `startPos` and `lengthSamples`
  (absolute output positions) so the exporter can locate the segment.
* Export request is a command `ExportSegment { startPos, lengthSamples }`. The audio
  thread copies the segment into a preallocated `exportBuffer` (<= 4096 samples per
  block) and sets `exportState` (Idle -> Copying -> Ready, atomic). The message thread
  reads `exportBuffer` only when `Ready` then sets `Idle`. One request at a time; a second
  request while busy is rejected with a status message.
* WAV writing (`juce::WavAudioFormat`), file chooser and drag-out run on the message
  thread (file I/O never on the audio thread). Write errors are shown, never swallowed.

### Tests
`export_segment_matches_wet_bus` (compare exported samples with an offline capture of the
wet bus) | `export_disarmed_zero_cost` (ring not written when disarmed) | `export_too_old_rejected` |
`export_bounded_copy_per_block` | `export_busy_rejected` | `export_samplerate_change_cancels` |
`export_wav_header_valid` | `export_no_audio_thread_allocation`.

---

## U-3 MIDI note trigger for Audition   (size S, depends E-1)

* Options page: **Trigger note** (Off / 0-127 / "Learn") and **Channel** (Any / 1-16),
  stored in `<Perf>` (not automatable).
* `processBlock` scans the `MidiBuffer` once: a `NoteOn` matching note + channel with
  velocity > 0 sets `previewRequested` and `previewVelocity`; velocity maps to preview
  amplitude `lerp (0.5, 1.0, vel/127)`. `NoteOff` is ignored. No MIDI is output
  (`producesMidi()` stays false). CC handling in `MidiLearnManager` is unchanged.
* **Learn**: next NoteOn within 10 s (same bounded session behaviour as CC learn,
  status line shows the countdown) becomes the trigger.
* Tests: `note_trigger_fires_once_per_noteon` | `wrong_note_or_channel_ignored` |
  `velocity_scales_amplitude` | `learn_times_out` | `trigger_roundtrip_state` |
  `note_ignored_when_bypassed` | `coalesces_with_ui_request`.

---

## U-4 Preset browser   (size L; schedule in the UI-refactor window, see plan)

Today: dropdown of 17 factory presets + user presets on disk, FILE menu Save/Open.

### Behaviour
* Browser popover: **search box**, **category** filter (Subtle, Rhythmic, Glitch,
  Ambient, Dub, Destructive, Utility, Favourites), list with name + one-line description,
  **prev / next arrows** next to the preset name (wrap-around, keyboard Up/Down in
  the list, MIDI-learnable commands `PresetNext/Prev`).
* **Favourites**: star toggle; stored in the global settings file (below) by preset
  identity (factory: name; user: path relative to the preset root).
* **User presets** live in `<Documents>/GapFill/Presets/<Category>/Name.gapfill`.
  "Save as user preset..." dialog asks name (<= 48 chars, filename-sanitised, no path
  separators), category, optional description. Context menu: rename, delete (confirm),
  reveal in file manager. Existing flat files stay visible under "Uncategorised".
* `.gapfill` root gains optional properties `name`, `category`, `description`, `author`
  (each length-capped, printable text only). Missing => derived from filename. They do
  not affect validation of the parameter tree.
* Loading remains the single validated path (`loadParameterStateFromFile`): every load
  pushes an Undo snapshot and shows rejection reasons exactly as today.

### Engineering
* Factory presets gain a `category` field (edited only in the preset pass, U-10).
* Scanning the user folder runs on a background thread, results cached and refreshed on
  popover open; cap 2000 files, ignore files > 1 MB; never touches the audio thread.
* Global settings file: `juce::PropertiesFile` at
  `<userApplicationDataDirectory>/GapFill/settings.xml`, read once at editor creation
  (message thread), written on change (debounced 500 ms). Holds: favourites, UI scale,
  first-run done, legend collapsed. No network, no telemetry (README privacy statement).
  If the file is unreadable the defaults apply silently.

### Tests
`search_filters_name_and_description` | `category_filter` | `next_prev_wraps` | `favourites_roundtrip` |
`sanitise_preset_name` | `user_scan_caps_and_skips_oversize` | `legacy_flat_files_listed` |
`load_pushes_undo` | `corrupt_properties_ignored_not_fatal` | `settings_file_unreadable_defaults`.

---

## U-5 Simple / Advanced view   (size L; UI-refactor window)

* A toggle in the top bar (`TopBarRight`) switches the editor between **Simple** and
  **Advanced**. Both show the same live state; hiding a control never resets or disables it.
* **Simple** shows: visualiser + legend, the creative row (Fill, Intensity, Complexity,
  Chaos, Density), Fill Type, Personality, Macros (if on), Mix (Dry/Wet/Output), Surprise / Random /
  Undo, Audition / Capture / Lock, preset browser, History strip. Everything else
  (detector panel, 15 weights, protection, spectral lanes, sync, cross-over, MIDI/Options)
  appears in **Advanced**.
* Default: **Simple for brand-new instances**; states saved before this feature
  (no `<Perf>` subtree) open in **Advanced** so existing users see no change. Choice
  persists in `<Perf>/viewMode`.
* Simple view MUST remain usable at the minimum window size without scrolling.
  A first-use hint in Simple points to Advanced ("More controls: Advanced").
* Tests: `view_mode_roundtrip` | `old_state_opens_advanced` | `new_instance_simple` |
  `hidden_controls_keep_parameters` | `keyboard_focus_only_visible_controls` | screenshot
  test at minimum size (each view) comparing against a stored golden image with tolerance.

---

## U-6 Auto-Calibrate   (size L, depends F0-4; independent of other features)

### Goal
A **Learn** button that listens to the user's real material for a few seconds and
sets Sensitivity, Gap Threshold and Min Gap so the detector finds sensible events and
gaps for *that* track, reducing the biggest tuning barrier.

### Algorithm (uses the real detector, no guessed formulas)
1. **Record**: on **Learn**, the audio thread copies the (mono-summed) main input of the
   next `N = 8 s` (user choice 4-15 s) into a preallocated recording buffer
   (`15 s x sampleRate x float`, allocated lazily on the message thread the first time
   Learn is pressed, **never** on the audio thread; if allocation hasn't completed the
   request is refused with "Try again"). Progress bar + Cancel. Requires input RMS above
   -60 dBFS for at least 25 % of the time, else "Not enough signal".
2. **Analyse** on a background `juce::Thread` with its **own private `AnalysisEngine`**
   (same class, same hop grid) fed with the recording for each candidate
   `(sensitivity in {30,40,50,60,70,80}) x (gapThresh in {40,50,60,70})` = 24 runs, all other
   analysis settings taken from the current parameters. Per run measure:
   `eventsPerSec`, `gapFraction` (time with `conf >= 0.55`), `chatter` (fraction of
   events closer than 30 ms to the previous event).
3. **Score** = distance of each metric from its target band
   (`eventsPerSec in [0.8, 10]`, `gapFraction in [0.15, 0.45]`, `chatter < 0.05`), plus a small
   penalty for distance from the current setting (prefer minimal change). Choose the
   best; tie -> the lower sensitivity.
4. **Min Gap** = clamp of the 20th percentile of measured gap lengths x 0.8, within 20-200 ms.
5. **Apply** on the message thread: push an Undo snapshot, then set the three parameters
   (host-notifying). Show a result card: "Sensitivity 50 -> 62 - Gap Threshold 60 -> 55 -
   Min Gap 55 ms. Found 3.2 hits/s, gaps 28 % of the time." with **Revert** (the Undo).
6. Detector source `Sidechain`/`Hybrid` warns "Calibrated from the main input only".
   Does nothing to protection, fill, or creative parameters.

### Constraints
Analysis thread uses lower priority, aborts on Cancel/editor close/`releaseResources`,
total CPU bounded (24 runs x 8 s audio must finish in < 3 s on the reference machine;
record it). The audio thread only copies samples into the preallocated buffer.

### Tests
Synthetic material with known structure: 120 BPM 4-on-the-floor at 3 dynamics, a sparse
snare pattern, a dense hi-hat loop, a pad (no events), and noise. Assert for each the
chosen settings lie in documented bands and the resulting `eventsPerSec/gapFraction`
are inside targets; `silence_refused` | `cancel_leaves_params_untouched` |
`apply_is_undoable` | `deterministic_same_input` | `no_audio_thread_allocation_during_record` |
`large_sample_rate_192k_ok`.

---

## U-7 First-run guide   (size M, depends F0-9, U-4's settings file)

* On the first editor open per user (global settings flag `firstRunDone`, **not** per
  project - otherwise it would reappear in every new session), show a dismissible
  3-step coach overlay: (1) "Put GapFill on a drum bus or full mix and press play" -
  highlights the visualiser; (2) "Raise Fill, or press SURPRISE" - highlights Fill /
  Surprise; (3) "Use AUDITION to hear a fill any time" - highlights Audition. Buttons: Next, Skip,
  "Don't show again". Re-openable from Help ("Show quick tour").
* Overlay is keyboard operable, announces each step to screen readers, never blocks the
  host UI thread, and does not appear if the editor is smaller than the minimum.
* Tests: `shows_once_per_user` | `skip_sets_flag` | `help_reopens` | `overlay_accessible_names` |
  `not_shown_when_settings_unreadable_is_safe` (defaults to not showing rather than looping).

---

## U-8 UI scale   (size M; UI-refactor window)

* Options: **Interface size** 100 / 125 / 150 / 175 / 200 % (default 100; stored in the
  global settings file, applies to all instances). Implementation: `editor.setScaleFactor`
  composed with the host DPI scale; the logical (unscaled) editor size remains what is saved
  per project, so projects do not jump between machines.
* Minimum window size is expressed in logical pixels and never exceeds the screen at the
  chosen scale (clamp to `Desktop::getDisplays()` work area; if it cannot fit, reduce scale
  with a note).
* Fonts: keep `theme.md` sizes, but nothing below 11 px effective except the 9 px version
  marker mandated by the theme.
* Tests: `scale_applied_and_persisted` | `logical_size_unchanged_by_scale` | `clamps_to_display` |
  screenshot at 100/150/200.

---

## U-9 Parameter info and status line   (size M; UI-refactor window)

* New `Source/features/info/ParamInfo.{h,cpp}`: a **registry** (`ParamInfoRegistry::add
  ({ id, shortName, oneLine, typicalRange, unit })`, static registration like Help) so each
  feature adds its own entries from `Source/features/<f>/Info.cpp` and the table is never a
  shared hot file. The existing ~66 parameters are registered by U-9 itself in
  `Source/features/info/CoreInfo.cpp`. A unit test requires an entry for every ID in
  `allParameterIDs()` (new params cannot ship without help text) and `oneLine` <= 90 chars.
  From Phase 1 on, every feature PR adds its `Info.cpp`; the coverage test is added in
  Phase 0 as *expected-failing for core IDs* and enabled when U-9 lands (allow-list file
  `Tests/param_info_allowlist.txt`, shrinking only).
* Knob/slider tooltips are generated from it (current text becomes the table text where
  it is already good): "Density - How often an eligible gap gets filled. Typical: 40-70 %".
* **Status line** (`FooterStatus` slot) shows, while a control is dragged or hovered:
  `Name value (base -> effective)`; the E-3 gap outcome when idle; MIDI-learn countdown
  and capture/preview/export messages with a fixed priority order
  (learn > export > preview > capture > last gap outcome > param hover).
* Tests: `every_parameter_has_info` | `tooltip_matches_table` | `status_priority_order` |
  `status_updates_rate_limited`.

---

## U-10 Preset pass and quality gate   (size L, run at the end of each phase)

* Grow the factory bank from 17 to >= 40 in 7 categories (>= 4 per category + a
  "Starting Points" group of 6 that demonstrate Personalities / Macros / Lock).
  Names stay descriptive (`include.md`), unique, <= 32 chars; each has a one-line
  description and category. The preset pass is the **only** ticket that edits `Presets.cpp`
  factory data (C-8's table sharing excepted).
* New quality-gate tests (must pass for every factory preset):
  `preset_loads_valid` | `preset_produces_fills_on_drum_loop` (2 <= fills in 8 bars <= 60) |
  `preset_output_peak_below_plus_3dBFS_and_finite` | `preset_changes_only_documented_params` |
  `preset_neutral_new_features_unless_intended` | `preset_names_unique_and_short`.
* `validate_repo.py` additionally checks each preset has a category from the fixed list.
* Update Help text and `README.md` screenshots/feature list; update `docs/COMPLIANCE.md`.

---

## Backlog (deliberately not specified yet)

Decide before building; each needs a product decision, not just engineering.

| Idea | Why parked |
|---|---|
| Free / Pro split (`include.md` says this may become a pro build) | Needs a decision on which of Macros, History, Export, Calibrate, Evolve are pro-only. Implement with a compile-time `GAPFILL_PRO` flag so one codebase builds both - decide before U-2/C-2 UI ships. |
| Quantised "Fill Now" (fire on next beat) | Needs host-aware start alignment; conflicts with the "fills start where the gap starts" model. |
| Fill morphing / Rhythm memory | Explicitly excluded by the creative spec. |
| Sidechain-routed fill source | New routing + latency semantics. |
| Localisation | After the string table from `describeRecipe`/ParamInfo exists. |
| Preset sharing site / online library | Conflicts with the no-network privacy statement; would need opt-in and a design review. |
| AAX / AUv3 / iOS | Licensing and platform scope. |
