# F-S02 Parameter information / status line
Status: implementation spec, Gate C candidate. Category UX, value H, size M. **Canonical spec: `05-SPEC-USABILITY-VALUE.md` U-9.** This file does not restate U-9; it lists gaps and the additions needed to make U-9 implementation-complete. Read U-9 first.

## 1 Summary & user-visible behaviour
U-9 gives: a `ParamInfoRegistry` (id, shortName, oneLine, typicalRange, unit), registry-generated tooltips, and a `FooterStatus` line showing `Name value (base -> effective)` with fixed priority. Additions in this ticket:
1. **Focus as well as hover.** The status line updates when a control gains keyboard focus (needed for keyboard navigation and screen readers), not only on mouse hover/drag.
2. **Plain-language range.** Format: `<Name>: <value> | range <lo>-<hi> <unit> | typical <a>-<b>` e.g. `Density: 65 % | range 0-100 % | typical 40-70 %`. Choice parameters list the current option and the count (`Fill Type: Auto (1 of 16)`).
3. **Effective state ("inactive because...").** A control that currently has no audible effect shows a reason suffix, driven by a dependency table (section 3), e.g. `Max Fill: 400 ms | inactive: Sync Max Fill is on, length follows Max Fill Division`.
4. **Effective value** is shown only if F-S05 (C-0) supplies one; otherwise the base value alone. No placeholder arrow.
5. Status messages from other features register through one `StatusSource` interface so no feature edits the processor timer string (today built inline in `PluginEditor::timerCallback`).

## 2 Success/quality metric
- 100 % of IDs in `allParameterIDs()` (currently 66, after F-S0x work more) have an info entry (`every_parameter_has_info`), `oneLine` <= 90 chars, no tooltip text differs from the table.
- Status text updates within 100 ms of hover/focus and at most 20 updates/s during drags.
- Manual a11y pass: a screen reader announces name, value, range and inactive reason for 10 sampled controls on Windows (NVDA) and macOS (VoiceOver).

## 3 Design
New module `Source/features/info/` per U-9: `ParamInfo.{h,cpp}`, `CoreInfo.cpp` (the existing parameters), `StatusLine.{h,cpp}`, `Gui.cpp`.
```cpp
struct ParamInfo { juce::String id, shortName, oneLine, typicalRange, unit; };
struct ParamRelation { juce::String id; std::function<bool (const ValueSource&)> inactive; const char* reason; };
class ParamInfoRegistry { public:
    static void add (ParamInfo); static void addRelation (ParamRelation);
    static const ParamInfo* find (const juce::String& id) noexcept;   // message thread only
    static std::vector<juce::String> missingIds (const juce::StringArray& all);
};
struct StatusMessage { enum class Priority { Learn, Warning, Export, Preview, Capture, Outcome, Hover }; Priority p; juce::String text; juce::uint32 expiresMs; };
class StatusSource { public: virtual ~StatusSource() = default; virtual std::optional<StatusMessage> current() const = 0; };
class StatusLine { public: void addSource (StatusSource&); juce::String resolve (juce::uint32 nowMs) const; };
```
- Relations table for existing parameters (in `CoreInfo.cpp`; verified against `Panels.cpp`): `maxFill` inactive when `maxFillSync` on; `maxFillDiv` inactive when `maxFillSync` off; `stutterDiv`, `delayDiv` inactive when `syncEnable` off (fixed 62.5 ms / 125 ms used, see `updateTempoInfo`); `w*` weights inactive when `fillType != Auto` (only the chosen algorithm plays); `bandXAmt/Int/Prob` inactive when `bandXOn` off; `xoverLM/MH` inactive unless `detMode == Spectral`; `pingPong` inactive unless `fillType` in {Auto, Delay}; `detSource`-dependent `protSC` inactive when `detSource == Main`; `maxFillDiv`/sync entries tolerate missing tempo ("using 120 BPM fallback").
- `ValueSource` is a tiny read-only adapter over `AudioProcessorValueTreeState::getRawParameterValue` so relations are unit-testable without an editor.
- Hooks: `GapKnob`/`GapCombo`/`GapToggle` constructors (Knob.cpp ~l.355/480/513) call `ParamInfoRegistry::find(id)` to fill `setTooltip/setDescription/setHelpText` (current strings seed the table so wording does not regress); `onHoverChanged` and a new `focusGained/Lost` forward to `StatusLine::setHover(id)`. `PluginEditor::timerCallback` replaces its inline status string by `statusLine.resolve()`; the existing learn/sidechain/capture texts become `StatusSource`s (behaviour-preserving, characterized by a test first per AGENTS.md rule 5).
- Feature coverage test: Phase-0 allow-list `Tests/param_info_allowlist.txt` shrinks to empty when core info lands; all new `fsNN_` IDs must ship with an `Info.cpp`.

## 4 Parameters & state
No APVTS parameters, no saved state. Setting `status.showRangeHints` (bool, default true) in the global settings file (F-S03). Neutral: UI text only; golden audio untouched.

## 5 Real-time safety
Nothing runs on the audio thread. Registry is populated by static registration at load and then read-only; `find` is message-thread only (documented, asserted with `MessageManager::existsAndIsCurrentThread` in debug). Status resolution is O(sources) per timer tick (< 20 sources) with no allocation in steady state (reused `juce::String` buffers).

## 6 UI
- Slot: `FooterStatus` (U-9). Nothing is added to the header.
- Tooltip text pattern (generated): `<shortName> - <oneLine>. Typical: <range>.` Example: "Density - How often an eligible gap gets filled. Typical: 40-70 %".
- Help: section "Reading the status line" (order 1020) listing priority order and meaning of `inactive:`.
- Accessible names: each control `setTitle(shortName)`, `setDescription(oneLine + range)`, and the status label is a live region equivalent via `setTitle` update; inactive controls stay enabled and operable (never greyed out or disabled by this feature, only annotated).
- Keyboard: focus changes drive the line; `F1` while a control is focused opens Help at its section (only when keyboard navigation setting is on).
- Reduced motion: text swap with no fade. Narrow: single line, ellipsis at width, full text in tooltip of the status label.

## 7 MIDI/automation/CLAP/host impacts
While MIDI learn is armed the learn message wins (existing behaviour preserved). Host automation moving a parameter shows its value in the status line only if that control is hovered/focused (no flicker from automation). Host-provided parameter names are untouched (`shortName` is UI-only). No CLAP/VST3/AU parameter metadata changes.

## 8 Tests (`Tests/features/TestInfo.cpp`)
`every_parameter_has_info` | `oneline_max_90_chars` | `tooltip_matches_table` | `status_priority_order` (learn > warning > export > preview > capture > outcome > hover) | `status_updates_rate_limited` (<= 20/s) | `relation_max_fill_sync` | `relation_weights_inactive_when_fixed_type` | `relation_band_off` | `range_text_for_choice_and_int` | `focus_updates_status` | `learn_text_preserved_verbatim` (characterization of current learn string) | `missing_entry_for_new_id_fails_ci`. Edge: parameter with `valueFromString` that rejects text; unicode unit labels.

## 9 Dependencies & build order
Needs F0-9 (FooterStatus slot) and F0-1 registries (Gate B B5). Soft: F-S05 for effective value; F-S01 and F-S04/F-S12 register `StatusSource`s afterwards. Build first among F-S UI tickets after F-S03; it unblocks status needs of F-S01, F-S04, F-S06, F-S07, F-S08, F-S12.

## 10 Effort & risks
M. Risks: registry static-init order (use explicit `order` ints like HelpRegistry); text drift between tooltip and Help (single source); announcing too often to screen readers (coalesce at 2 Hz for focus-only changes); wrong relation rules mislead users, so each relation has a test with the DSP behaviour it claims.

## 11 Acceptance checklist
- [ ] U-9 acceptance met (registry, tooltips from table, status priority, rate limit)
- [ ] Focus-driven status and `inactive:` reasons implemented and tested
- [ ] Every current and new parameter has an entry; allow-list empty
- [ ] Existing status strings unchanged (characterization test green)
- [ ] Screen reader pass on Windows and macOS recorded
- [ ] Layout screenshot at minimum size with long status text
- [ ] No APVTS or state changes; golden output unchanged
