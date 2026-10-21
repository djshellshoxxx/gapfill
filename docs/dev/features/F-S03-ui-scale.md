# F-S03 UI scale setting
Status: implementation spec, Gate C candidate. Category UX, value H, size M. **Canonical spec: `05-SPEC-USABILITY-VALUE.md` U-8.** This file adds the missing implementation detail and defines the shared **global settings store** that F-S01, F-S02, F-S09 and F-S11 also use (U-4 specifies it but U-4 is later).

## 1 Summary & user-visible behaviour
- Options page: **Interface size** 100 / 125 / 150 / 175 / 200 % (default 100). Applies immediately to the open editor and to every instance on next editor open; stored per user, not per project.
- The size the project remembers (`editorWidth/Height` root properties, today `savedEditorWidth/Height`) stays in **logical pixels**, so a project moved between machines or scale settings opens at the same layout.
- If the chosen scale would make the window larger than the display work area, the scale is reduced to the largest step that fits and a note "Reduced to 125 % to fit this screen" shows once in the Options page and status line. The stored preference is not overwritten.
- Hosts that supply their own DPI scale (`setScaleFactor`) keep working: effective scale = host scale x user scale.

## 2 Success/quality metric
- At 100 / 125 / 150 / 200 % on a 1920x1080 display at host scale 1.0 and 2.0: no clipped labels, no overlapping controls, hit targets >= 24 logical px, screenshots match stored references (<= 0.5 % pixel diff after font hinting tolerance).
- `getWidth()/getHeight()` and saved size identical at every scale; preference survives editor close/open and a second instance.

## 3 Design
- **Global settings module (shared, defined here):** `Source/features/settings/GlobalSettings.{h,cpp}`
  ```cpp
  class GlobalSettings {
  public:
      static GlobalSettings& get();                           // message thread only; lazy, no static-init IO
      int   getInt (const juce::String& key, int def) const;  void setInt (const juce::String& key, int v);
      bool  getBool (const juce::String& key, bool def) const; void setBool (const juce::String& key, bool v);
      juce::StringArray getStringList (const juce::String& key) const; void setStringList (const juce::String& key, const juce::StringArray&);
      enum class LoadStatus { Loaded, Missing, Unreadable };   // Missing = first ever run; Unreadable = do not nag
      LoadStatus loadStatus() const noexcept;  bool isWritable() const noexcept;
      void flushNow();                                        // used at editor close and tests
      static void setFileForTests (const juce::File&);
  };
  ```
  Backed by `juce::PropertiesFile` at `<userApplicationDataDirectory>/GapFill/settings.xml` (same root as the existing user preset folder, `PluginEditor::userPresetDirectory()`), opened lazily on first use, writes debounced 500 ms via `juce::Timer`, `ignoreCaseOfKeyNames=false`, `storageFormat=storeAsXML`. Unreadable/corrupt file: defaults apply silently, file is not overwritten until a setting changes (U-4). Keys are namespaced (`ui.scale`, `firstRun.done`, `presets.favourites`, `telemetry.showFooterWarnings`, `status.showRangeHints`). Size cap 256 KiB; list values capped 2000 entries. No network, no telemetry.
- **Scale module:** `Source/features/uiscale/UiScale.{h,cpp}` (pure): `int clampScalePercent (int requested, juce::Rectangle<int> workArea, juce::Point<int> logicalSize, float hostScale) noexcept` returns the largest step in {100,125,150,175,200} <= requested that satisfies `logical * hostScale * step/100 <= workArea * 0.95`; `float effectiveScale (float host, int percent)`.
- **Editor hook** (minimal, in `PluginEditor`): override
  ```cpp
  void setScaleFactor (float hostScale) override { hostScaleValue = hostScale; applyScale(); }
  void applyScale() { AudioProcessorEditor::setScaleFactor (hostScaleValue * userPercent / 100.0f); }
  ```
  `AudioProcessorEditor::setScaleFactor` installs `setTransform (AffineTransform::scale (s))` and calls `editorResized` (JUCE `juce_AudioProcessorEditor.cpp` l.227), so composing host x user there keeps the wrapper-reported size consistent. `setResizeLimits (kMinWidth, kMinHeight, 2600, 1800)` stays in logical units; `resized()` and `proc.setSavedEditorSize` keep reading `getWidth()/getHeight()` (logical because a transform does not change bounds).
- Font floor: nothing below 11 effective px except the 9 px version marker (theme.md). `LookAndFeel` font helpers clamp `max (11/ scale-aware, requested)` only where a font is requested below 11 logical px; existing theme sizes unchanged.
- Standalone: JUCE's standalone window sizes itself to the editor; verify it honours the transformed size (risk below).

## 4 Parameters & state
No APVTS parameters (UI preference, not automatable). Stored in the global settings file only, key `ui.scale` (int percent, default 100, valid set {100,125,150,175,200}; any other value is snapped on read). Plugin state unchanged: old sessions load identically; neutral proof = scale 100 produces no transform change (`getTransform().isIdentity()` when host scale is 1).

## 5 Real-time safety
Message thread only. No audio-thread interaction. Settings file IO happens on the message thread, never in `processBlock`; debounced write avoids IO bursts while a user clicks through sizes.

## 6 UI
- Placement: Options page, `OptionsExtra` slot, one combo "Interface size" with hint "Makes the whole plug-in larger. Does not change your saved layout."
- Tooltip: "Scale the whole interface. Your DAW's own scaling is applied on top." Help: entry under Options (order 1030).
- Accessible: combo title "Interface size", options announced as "125 percent". Keyboard: reachable in Options tab order; arrow keys change value; change is applied on selection, not on highlight.
- Reduced motion: scale change is a single layout pass, no animation.
- Narrow: Options panel itself is 520x440 logical (`PluginEditor.cpp` `optionsView.setBounds`), so at 200 % it needs 1040x880 physical; the clamp uses the larger of editor and panel minimums.

## 7 MIDI/automation/CLAP/host impacts
None for MIDI/automation. Host behaviours differ: some hosts call `setScaleFactor` after editor creation and again on monitor change, so `applyScale` must be idempotent. Windows per-monitor DPI, macOS Retina (JUCE handles backing scale; user scale multiplies on top) and Linux/X11 (hosts often supply no scale) must be checked. CLAP/VST3 hosts that cannot resize the plug-in window will ignore growth; then the clamp reports the achieved scale and the note shows.

## 8 Tests (`Tests/features/TestUiScale.cpp`)
`scale_clamps_to_display` | `scale_steps_snap` | `effective_scale_composes_host_and_user` | `logical_size_unchanged_by_scale` | `scale_persisted_roundtrip` (temp settings file) | `settings_file_unreadable_defaults` | `settings_corrupt_not_overwritten_until_change` | `settings_debounce_single_write` | `settings_list_caps` | `second_instance_reads_same_value` | `set_scale_factor_idempotent` | `min_size_layout_no_overlap_at_each_scale` (component bounds intersection check, extends existing layout tests) | screenshots 100/150/200. Edge: work area smaller than minimum editor size (scale forced to 100, window may exceed screen with scrolling disabled, documented).

## 9 Dependencies & build order
Depends on B5 (slots) only for the Options row. Build **first** in the F-S UI group because `GlobalSettings` is a prerequisite for F-S09 and F-S11 and a soft dependency of F-S01/F-S02. U-4 must reuse this module instead of creating a second settings file.

## 10 Effort & risks
M. Risks: wrapper/host disagreement on window size when a transform is present (test Reaper, Live, FL, Bitwig, Logic, standalone on each OS); text blur at fractional host scales; two instances writing the settings file (last writer wins, single key writes are atomic via PropertiesFile, document); memory of `AffineTransform` interplay with MouseCursor hotspots in some hosts.

## 11 Acceptance checklist
- [ ] U-8 acceptance met; logical saved size independent of scale
- [ ] `GlobalSettings` implemented once, with tests, reused by later tickets
- [ ] Host x user scale composition verified in at least one host per OS (record unverified ones)
- [ ] Clamp note shown when display too small
- [ ] Screenshots at minimum size for 100/150/200 reviewed
- [ ] Settings corruption/missing handled silently, no crash
- [ ] No APVTS or plugin-state change; golden output unchanged
