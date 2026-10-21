# F-S05 Effective-value feedback
Status: implementation spec, Gate C candidate. Category UX, value H, size S. **Canonical spec: `04-SPEC-CREATIVE.md` C-0** (effective-value display on knobs) plus the two-stage pipeline in `02-FOUNDATIONS.md` F0-5. This file does not repeat C-0. It makes C-0 implementable **before** personalities and macros exist, because several base-vs-effective differences already exist today.

## 1 Summary & user-visible behaviour
C-0 behaviour stands: when a control's effective value differs from its base value, the knob draws a thin secondary arc plus a tick at the effective value, and the hover/focus readout shows `base -> effective`. The base knob position, the host-visible parameter value and saved state are never changed by this feature. Additions:
1. **Reason text** with every effective value: `Max Fill 400 ms -> 375 ms (synced to 1/4 at 160 BPM)`, `Fill 35 % -> 0 % (bypassed)`, `Fill 35 % -> 0 % (held by Panic)`, later `(Dub personality)`, `(Macro: Activity)`, `(Evolve)`.
2. **Existing producers shipped first:** (a) `maxFill` when `maxFillSync` is on (effective ms after `updateTempoInfo`, clamped 20-2000 ms, shows "tempo unavailable, using 120 BPM" when the host gives no tempo); (b) `fill` while plugin or host bypass is active (`engineSettings.fill01 = 0` in `processChunk`); (c) `fill` while F-S12 Held mode is active. Personality/macro/evolve producers plug in later through the same API.
3. **Stale protection:** if the audio thread has not processed a block for 500 ms (host stopped) effective markers are hidden rather than showing a frozen value.
4. Choice/toggle controls (stutter and delay division) show their effective length in the status line only (ms), no ring.

## 2 Success/quality metric
- For every producer, displayed effective value equals the value the engine consumed in the same chunk (test compares `EffectiveSettings` field vs published value, exact).
- Base parameter value, normalised value and undo stack are unchanged by display (0 writes to APVTS in a 10-minute soak test).
- Knob paint is pixel-identical to today when no effective value is set (screenshot test).

## 3 Design
New module `Source/features/effective/` (`cmake/sources/effective.cmake`):
```cpp
enum class EffReason : std::uint8_t { None, TempoSync, Bypass, PanicHeld, Personality, Macro, Evolve, Count };
class EffectiveValues {
public:
    static constexpr int kMaxSlots = 32;
    // startup (processor constructor, message thread): maps parameter id -> slot, fixed for the run
    int registerSlot (const juce::String& parameterId);
    int slotFor (const juce::String& parameterId) const noexcept;
    // audio thread, once per chunk, real units; NaN clears
    void publish (int slot, float realValue, EffReason reason) noexcept;
    void clearAll() noexcept;
    // message thread
    struct View { bool valid; float real; EffReason reason; };
    View get (int slot) const noexcept;
    std::uint32_t publishCount() const noexcept;      // stale detection
};
```
- Storage: `std::array<std::atomic<float>, 32>` values plus `std::array<std::atomic<std::uint8_t>, 32>` reasons plus one `std::atomic<std::uint32_t>` counter, all relaxed. Audio code uses precomputed slot ints (`EffSlot::MaxFill`, `EffSlot::Fill`), never strings.
- Producers (the only audio-thread edits, in `processChunk` near the existing `gatherEngineSettings`/`updateTempoInfo` block): `eff.publish (EffSlot::MaxFill, pb(maxFillSync) ? s.maxFillMs : NAN, TempoSync)`; `eff.publish (EffSlot::Fill, bypassed || panicHeld ? 0.0f : NAN, bypassed ? Bypass : PanicHeld)`. When F0-5 lands these two lines move into `applyBlockStage` unchanged in meaning.
- GUI: `FineSlider::setEffectiveValue (std::optional<double> v01)` and `setEffectiveReason (const juce::String&)` in `Knob.{h,cpp}`; `GapKnob` owns an `EffectiveBinding` that on the editor timer (24 Hz) converts real -> normalised through the parameter's `NormalisableRange` and calls the setters only when the value changed by >= 0.5 % of range (hysteresis, avoids flicker). Paint: extra arc drawn only when the optional is set; existing path untouched.
- Status/readout text built by `describeEffective (const juce::String& name, base, effective, reason)` in `Telemetry`-free pure code (`EffectiveText.cpp`) so it is unit-tested; the status line (F-S02) consumes it as a `StatusSource`.
- Hooks in existing files: `PluginProcessor.h/.cpp` (member + getter + two publish lines + `clearAll()` in `reset()/prepareToPlay`), `Knob.{h,cpp}` (setters, paint branch), `Panels.cpp` untouched (binding is created inside `GapKnob`).

## 4 Parameters & state
No APVTS parameters, no saved state (runtime display only). Reserved `fs05_` prefix unused. Neutral proof: with sync off and no bypass, all slots hold NaN, the optional stays empty, paint path and audio are unchanged (golden + screenshot). Old sessions load identically.

## 5 Real-time safety
Audio thread: <= 3 relaxed atomic stores per chunk (floats and a byte), no strings, no allocation, no locks. Bounded array of 32; `publish` with an out-of-range slot is ignored (debug assert). Tearing between value and reason is possible for one editor tick and harmless (both are display-only).

## 6 UI
- Visual: secondary arc thinner than the value arc, plus a 2 px tick, so colour is not the only cue; accent colour unchanged; shown only while valid. Respects Reduced Motion (no ring animation; instant jump).
- Readout format `Name base -> effective` in the knob readout (hover) and status line (focus/hover, F-S02). Tooltip addition: "A thin ring shows the value GapFill is actually using right now. Your setting is not changed."
- Help: entry "Effective value ring" (order 1060) listing the reasons. Accessibility: `setDescription` on the slider updated at <= 2 Hz to "base 400 milliseconds, effective 375 milliseconds, synced"; keyboard focus shows the same text in the status line.
- Narrow size: nothing added to layout; ring lives inside the existing knob bounds.

## 7 MIDI/automation/CLAP/host impacts
None on parameters: host automation and MIDI CC continue to move the base value; display follows. Hosts never see the effective value (no extra parameter). Tempo changes in the host update the ring within one editor tick. Offline/render: no editor, nothing published to the UI (cost is the stores only).

## 8 Tests (`Tests/features/TestEffective.cpp`)
`slot_registration_stable_and_bounded` | `publish_nan_clears` | `max_fill_effective_matches_engine_value` (several BPM, clamp at 20 and 2000 ms) | `max_fill_tempo_missing_reason` | `fill_effective_zero_when_bypassed` | `fill_effective_zero_when_panic_held` | `base_parameter_unchanged_by_display` | `no_undo_entries_created` | `hysteresis_half_percent` | `stale_after_500ms_hidden` | `knob_paint_identical_when_null` (screenshot hash) | `reduced_motion_no_animation` | `describe_effective_text_formats` | `accessibility_description_updates_rate_limited` | `no_audio_thread_allocation`. Edge: log/skewed ranges (`minGap`, `maxFill` use centre-skew) convert correctly; sample-rate change clears slots.

## 9 Dependencies & build order
Needs F0-9/Knob changes only. Soft: F-S02 (status), F-S12 (Panic reason). Prerequisite of C-1/C-2 (they add producers, not UI). Build after F-S02 and F-S12; before C-1.

## 10 Effort & risks
S. Risk: with only sync and bypass producers the visible benefit today is small (decision for owner: ship now as infrastructure with these two producers, or hold until C-1 so the ring debuts with personalities). Risk of users thinking the ring is automation; the tooltip and Help address this.

## 11 Acceptance checklist
- [ ] C-0 acceptance met; reason text shown for each producer
- [ ] Engine-consumed value equals displayed value in tests
- [ ] Knob paint pixel-identical when no effective value; Reduced Motion honoured
- [ ] Base values, undo stack and host automation unaffected (soak test)
- [ ] Stale hiding verified with transport stopped
- [ ] Accessible description and Help entry present; screenshot at minimum size
- [ ] No new APVTS IDs or state; golden output unchanged
