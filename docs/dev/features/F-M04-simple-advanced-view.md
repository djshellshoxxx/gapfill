# F-M04 Simple / Advanced view (completion spec over U-5)

Status: canonical behaviour is `05-SPEC-USABILITY-VALUE.md` U-5; layout rules `11-GUI-PLACEMENT-AND-FEATURE-PLAN.md` (Progressive disclosure 1-7). This file maps U-5 onto the real editor components and closes its gaps.

## 1 Summary & user-visible behaviour
- Two views of the same live state. Hiding a control never resets, disables, unmaps or un-automates it (MIDI mappings, host automation, Undo, presets all still act on hidden controls).
- Default: new instances open in Simple; any instance restored from a state that has no `<Perf>/viewMode` (saved by 0.0.1 or earlier) opens in Advanced so existing users see no change.
- Placement correction to U-5: U-5 puts the toggle in `TopBarRight`, but the header is at capacity (docs/dev/11). The toggle goes at the right end of the existing tab strip row (same row as CREATIVE / AUTO WEIGHTS / SPECTRAL), a two-segment control `SIMPLE | ADVANCED`. Header untouched. (Owner decision D1.)
- Actual component mapping (`PluginEditor.h`, `Panels.h`):

| Component | Simple | Advanced |
|---|---|---|
| `visualiser` (+ legend, timeline chevron) | shown | shown |
| `CreativePanel`: fill, intensity, complexity, chaos, density, fillType | shown | shown |
| `CreativePanel`: seed, width, sync + stutterDiv/delayDiv, pingPong, monoWet | hidden | shown |
| `CreativePanel`: capture toggle (+ F-M08 controls) | shown | shown |
| Slot `CreativeExtra`: Audition fill, Personality (F-M06) | shown | shown |
| Slot `CreativeExtra`: envelope (F-M05), breathing (F-M07), band types (F-M09) | hidden | shown |
| `MixPanel` (dry, wet, output, meters, status) | shown | shown |
| `DetectionPanel` | hidden except `sensitivity` | shown |
| `ProtectionPanel` | hidden except `transientProtect` | shown |
| Tabs `AUTO WEIGHTS`, `SPECTRAL` | hidden (only CREATIVE) | shown |
| Header: preset, A/B, copy, random, surprise, undo, reset, help, bypass | shown | shown |
- Recommended override of U-5 (D2): keep `sensitivity` and `protTrans` visible in Simple as a two-knob "Detection" strip. Without them a new user cannot fix "nothing detected" or "fills step on hits" (the top two E-3 reasons) without leaving Simple. Removing them returns to the literal U-5.
- First-use hint (once): "More controls: Advanced" chip beside the toggle; dismissed permanently by any toggle use.

## 2 Success / quality metric
- Discoverability: in a 5-tester first-run script, >= 4 produce an audible fill and adjust Fill Type or Density within 90 s without opening Advanced.
- Safety: 100% of parameters remain reachable (Advanced) and unchanged when toggling views (test compares all APVTS raw values before/after 50 random toggles).
- Layout: Simple view fits 1020x620 with no scrolling and no clipped labels at UI scales 100/125/150/200%.

## 3 Design
- Files: `Source/features/view/{ViewMode.h,ViewMode.cpp,Gui.cpp,Help.cpp}`, `Tests/features/TestViewMode.cpp`, cmake fragments. Core: `enum class ViewMode { Simple = 0, Advanced = 1 };` owned by the processor (`std::atomic<int> viewMode`, message-thread writes) so it survives editor re-creation.
- Visibility is driven by a declarative table, not scattered `setVisible` calls:
  ```cpp
  struct ViewRule { const char* componentId; bool simple; bool advanced; };   // componentId = registered slot / panel id
  bool isVisible (const char* componentId, ViewMode) noexcept;                 // pure, unit-testable
  ```
  Panels register their id through the F0-9 slot host; legacy panels get ids in one hook in `PluginEditor::resized()` (`applyViewMode()` returning bool visible per id, then existing layout code skips hidden panels). This is the single permitted `PluginEditor.cpp` edit. Per-control hiding inside `CreativePanel`/`DetectionPanel`/`ProtectionPanel` uses `setVisible` plus a layout flag so hidden controls do not take space; they remain constructed, so attachments, MIDI-learn menus and tooltips stay alive.
- Focus management: when a view change hides the focused control, focus moves to the view toggle and an announcement "Advanced view off, 31 controls hidden" is posted.
- Tab change: if the current tab is hidden (Weights/Spectral) switching to Simple selects Creative; the previous tab is remembered and restored when returning to Advanced.
- Data flow: toggle -> `processor.setViewMode()` -> editor `applyViewMode()` -> `resized()`; state writes via `<Perf>` only.

## 4 Parameters & state
- NEW APVTS IDs: none. View mode is UI preference, not sound; a parameter would be automatable, saved in presets, and affected by Random/Undo.
- State: `<Perf>/viewMode` int {0,1}, plus `<Perf>/viewHintDismissed` bool. Invalid values -> default for new instance (Simple) but if `<Perf>` is absent entirely -> Advanced (rule above). `stateVersion` absent or 1 -> Advanced.
- Neutral proof: Advanced renders the current layout; screenshot test at default size equals the pre-feature golden image within tolerance. Old session loads in Advanced; new instance has no state to load.
- Subtlety: some hosts call `setStateInformation` with the plugin's own default state on instantiate; the saved default state includes `<Perf>` with `viewMode=Simple`, so it round-trips as Simple. Unit test with "state produced by a fresh instance".

## 5 Real-time safety
No audio-thread involvement. `viewMode` is never read from `processBlock`. All hide/show on the message thread; no timers added.

## 6 UI
- Toggle: segmented `SIMPLE | ADVANCED`, each segment >= 24 px high, focus ring 2 px. Tooltip: "Simple shows the main sound controls. Advanced shows detector, protection, weights and spectral lanes. Nothing is reset when you switch." Accessible name "View mode", options announced as radio group.
- Help (order 1300): "Simple and Advanced view", list of what moves, note that hidden controls keep their values.
- Keyboard: toggle reachable in Tab order after tabs; `Alt+V` toggles when the editor has focus (documented in Help; conflicts checked with host shortcuts - if a host consumes it, only the button works).
- Reduced Motion: instant switch, no slide.
- Minimum size: Simple at 1020x620 must not scroll; Advanced keeps today's layout. F0-9's measured minimum size governs if it differs.

## 7 MIDI / automation / CLAP / host
No host-visible change. Hidden parameters remain in the host's parameter list and respond to automation and MIDI learn; the learn menu still works through the right-click on visible controls only (documented: map in Advanced).

## 8 Tests (`Tests/features/TestViewMode.cpp`)
U-5 list plus: `rule_table_covers_every_registered_component` | `toggle_preserves_all_parameters_after_50_random_toggles` | `hidden_controls_still_receive_automation_and_midi_cc` | `focus_moves_to_toggle_when_focused_control_hidden` | `current_tab_falls_back_to_creative` | `fresh_instance_state_roundtrips_as_simple` | `absent_perf_subtree_is_advanced` | `corrupt_viewmode_value_defaults` | `simple_has_no_clipped_controls_at_min_size_all_scales` (layout bounds test, not pixel) | `accessibility_names_unique_in_each_view`. Fixtures: `state_0.0.1_*.bin` (must open Advanced), state generated by fresh instance.

## 9 Dependencies & build order
Needs F0-6 (`<Perf>`), F0-9 (slots). Best scheduled in the UI-refactor window together with F-S03 (UI scale) and F-S02 (status line) because all three touch `resized()`. Should follow F-M05/F-M06/F-M07/F-M09 only for rule-table entries (each ticket adds its rows). F-M12 browser is visible in both views.

## 10 Effort & risks
Effort L (U-5 estimate). Risks: (1) editor layout code is hand-written in `resized()`, hidden-control collapse can leave gaps; (2) screenshot tests per platform; (3) users with muscle memory in existing sessions (mitigated by Advanced default for old states); (4) hidden-control MIDI mappings invisible to the user (document and list mapped hidden controls in Options -> MIDI).

## 11 Acceptance checklist
- [ ] Old-state fixtures open Advanced; fresh instance opens Simple
- [ ] 50-toggle parameter-preservation test passes
- [ ] Simple usable at 1020x620 without scrolling at all supported scales
- [ ] Toggle sits in the tab row; header unchanged
- [ ] Focus, announcements and keyboard path verified
- [ ] Help and tooltip registered; per-ticket rule rows added by other tickets
- [ ] D1/D2 recorded in the decision log (docs/dev/11)
