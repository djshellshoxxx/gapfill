# F-S09 First-run guide
Status: implementation spec, Gate C candidate. Category UX, value M, size M. **Canonical spec: `05-SPEC-USABILITY-VALUE.md` U-7** (3-step coach overlay, per-user flag, re-openable from Help). This file adds what U-7 leaves open: the settings dependency, tour versioning, multi-instance behaviour, the "small working example", target registry and step availability.

## 1 Summary & user-visible behaviour
U-7 stands: on the first editor open per user, a dismissible three-step overlay highlights (1) the visualiser, (2) Fill / SURPRISE, (3) AUDITION; buttons Next, Skip, "Don't show again"; re-open from Help ("Show quick tour"). Additions:
1. **Working example:** step 2 offers **Load example** which applies the factory preset "Drum Gap Glitch" through `selectPreset` (so it is one Undo step; status "Loaded Drum Gap Glitch. Undo goes back"). Nothing changes unless the user clicks it.
2. **Once per process:** with several instances in a project only the first editor opened in the host process shows the tour (static atomic); other editors stay clean. Help re-open always works.
3. **Tour versions:** `firstRun.doneVersion` (int) vs the current tour version (1). A later release with a new tour shows only a one-line "What's new" hint (not the full overlay) unless the user never completed v1. "Don't show again" sets `firstRun.suppressForever`.
4. **Step availability:** each step is data (`TourStep{ id, title, body, targets, available() }`). Step 3 is shown only if Fill Audition (E-1) exists in the build; otherwise it points at UNDO and the preset arrows ("Every Surprise, Random or preset change can be undone").
5. Esc closes the tour and counts as Skip.

## 2 Success/quality metric
- Appears exactly once per user across 10 launches and 5 simultaneous instances (automated with a temp settings file).
- Never appears when the settings file is unreadable or not writable (cannot persist the flag => no looping).
- First-time task check (usability session, 5 participants): >= 4 of 5 find Fill and SURPRISE and hear a fill within 2 minutes of the tour.

## 3 Design
New module `Source/features/tour/` (`cmake/sources/tour.cmake`):
```cpp
struct TourStep { juce::String id, title, body; std::vector<juce::String> targetIds; std::function<bool()> available; };
class TourModel {                      // pure logic, no GUI
public:
    static constexpr int kTourVersion = 1;
    TourModel (GlobalSettings&, std::vector<TourStep>);
    bool shouldAutoShow (bool settingsReadable, bool writable, bool alreadyShownInProcess) const noexcept;
    int  numSteps() const; int index() const; void next(); void back();
    void skip(); void finish(); void dontShowAgain();      // each persists via GlobalSettings
};
class TourTargets { public: void add (const juce::String& id, juce::Component&); juce::Component* find (const juce::String& id) const; };
class TourOverlay : public juce::Component { /* scrim with cut-outs, callout card, buttons */ };
```
- `GlobalSettings` (defined in F-S03) supplies `loadStatus()` (Missing = true first run; Unreadable = never show) and `isWritable()`. Keys: `firstRun.doneVersion` (int, default 0), `firstRun.suppressForever` (bool, default false).
- Target ids registered by the owners of the controls (`"visualiser"`, `"fill"`, `"surprise"`, `"audition"`, `"undo"`, `"presets"`), through slot factories (F0-9) or one line each in `PluginEditor` construction. If a target is missing or invisible (collapsed Simple/Advanced section), the card is shown centred without a cut-out rather than failing.
- `TourOverlay` is a child of the editor on top of the content (slot `VisualiserOverlay` is for the visualiser only; the tour is a full-editor overlay added by the editor once). It paints a scrim with rounded cut-outs around targets (uses `Component::getBoundsInParent` mapped to editor coordinates, recomputed on `resized()` so it survives UI scale and resizing), and a card with Back/Next/Skip/Don't show again. It never starts a modal loop and never blocks the message thread; mouse events outside the card pass through to the plug-in only for the highlighted targets so the user can try them.
- Auto-show decision happens once, after the first `resized()` with a valid size, via `MessageManager::callAsync` guarded by a `SafePointer` (editor can be destroyed at any moment). Not shown if the editor is below `kMinWidth/kMinHeight`.
- Help: "Show quick tour" button at the top of `HelpView` calls `editor.showTour (true)` (forces show, does not change flags until finished).
- Existing-file hooks: `PluginEditor.cpp` (construct model/overlay, register targets, one `callAsync`), `HelpView.{h,cpp}` (one button + callback; text through the Help registry once F0-1 lands).

## 4 Parameters & state
No APVTS parameters and nothing in the plugin state (a project must never carry the flag). Global settings keys above only. Reserved `fs09_` prefix unused. Neutral: no audio path involvement; with a missing or corrupt settings file behaviour is "do not show" (safe). The only audio-visible effect, "Load example", is user-initiated and undoable.

## 5 Real-time safety
Message thread only; the overlay reads no audio memory. "Load example" uses the existing `selectPreset` path (message thread, already used by the preset box). No timers faster than the editor's.

## 6 UI
- Text (final copy): Step 1 "Put GapFill on a drum bus or full mix and press play. The picture shows the gaps it finds." Step 2 "Raise Fill, or press SURPRISE for a new sound. Load example sets up a good starting point." Step 3 "Press AUDITION to hear a fill any time, without waiting for a gap." (fallback: "UNDO and the arrows next to the preset name let you try anything safely.")
- Accessibility: card has title "Quick tour, step N of 3"; each step change updates the title and moves focus to Next; Next/Back/Skip are named buttons; Esc closes; highlighted targets keep their own accessible names. Respects Reduced Motion (no fades, no pulsing outline). The overlay is reachable with the keyboard even if "keyboard navigation" is off, because it is an explicit overlay (matches the comment in `applyKeyboardNavigationSetting`).
- Narrow/min size: card width `min (360, editorWidth - 48)`, text wraps, never overlaps the highlighted target (placed on the side with more room). Colour is not the only cue: highlight is outline plus step counter.

## 7 MIDI/automation/CLAP/host impacts
None for audio/MIDI/automation. Hosts open and close editors constantly: the model is per-editor and cheap; the process-wide "shown" flag prevents repeats. Standalone shows it on first launch the same way. Some hosts open the editor hidden or at 0x0 first: auto-show waits for a valid size.

## 8 Tests (`Tests/features/TestTour.cpp`)
U-7: `shows_once_per_user` | `skip_sets_flag` | `help_reopens` | `overlay_accessible_names` | `not_shown_when_settings_unreadable_is_safe`. Added: `not_shown_when_settings_not_writable` | `not_shown_second_instance_same_process` | `dont_show_again_suppresses_future_versions` | `new_tour_version_shows_hint_not_overlay` | `step3_falls_back_without_audition` | `missing_target_centres_card` | `esc_counts_as_skip` | `load_example_is_single_undo_step` | `load_example_not_applied_on_skip` | `overlay_survives_resize_and_scale` | `not_shown_below_min_size` | `editor_destroyed_before_callasync_is_safe` | `keyboard_only_walkthrough` | `reduced_motion_no_animation` | screenshots at min/default/large size. Manual: fresh-profile run on each OS.

## 9 Dependencies & build order
Hard: F-S03 (`GlobalSettings` with `loadStatus/isWritable`), F0-9 (slots) and the Help registry. Soft: E-1 (step 3), F-S02 (status messages), F-S11 (preset arrows exist already in the header). Build after F-S03 and, ideally, after E-1 so the tour describes shipped features only.

## 10 Effort & risks
M. Risks: overlay hit-testing over host-embedded windows on Linux/X11; cut-out positions wrong after UI scale changes (recomputed on every `resized()`); tour text going stale as features change (steps are data, one file); settings write failures (handled by the writable check). Owner decision: whether "Load example" should exist at all (changes sound on click) or stay a pointer to the preset menu.

## 11 Acceptance checklist
- [ ] U-7 acceptance and all added tests green
- [ ] Appears once per user and once per process; never loops on unreadable or read-only settings
- [ ] Works fully by keyboard and with a screen reader; Reduced Motion honoured
- [ ] Highlights correct at minimum size and at 100/150/200 % UI scale
- [ ] Load example is one Undo step and only on click
- [ ] Help re-open works; "Don't show again" persists across versions
- [ ] No plugin-state, APVTS or audio changes; golden output unchanged
