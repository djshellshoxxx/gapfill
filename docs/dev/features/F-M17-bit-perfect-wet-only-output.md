# F-M17 Bit-perfect wet-only output mode
Status: implementation spec, Gate C candidate (08). Category VAL/UX, value H, size M. The host-side Wet output bus is a **separate ticket with its own spec: F-L12** (not repeated here). Canonical context: `10-SPEC-LIVE-CREATIVE-EFFECTS.md` s2 ("effects process generated wet material only unless a clearly named global routing option says otherwise") and s4.5 wet-only export (U-2 records wet to a file; this ticket is the live/offline **render mode**, not the exporter). Nothing named "wet only" exists in the code today: output is `dry*d + wet*w` times Output gain (`PluginProcessor.cpp` mix loop), with `d`, `w`, `o` smoothed.

## 1 Summary & user-visible behaviour
- New global switch **OUTPUT: Normal / Wet only** (Mix panel). In *Wet only* the plugin outputs only what GapFill generated, exactly as the engine produced it after the existing wet processing (Low Protect, Center Protect, Width, Mono Wet), with unity gain: no dry, and the Dry / Wet / Output knobs have no effect (shown dimmed with the note "Wet only: level controls bypassed").
- "Bit-perfect" has a precise meaning: for finite samples within +/-16, output sample = wet-bus sample, float for float, with no extra gain, smoothing, dithering, limiting or format conversion; and `WetOnlyOutput + DryOnlyOutput` (a run with Wet = 0) equals the Normal output at Dry 100 / Wet 100 / Output 0 dB, sample for sample.
- Use: print only GapFill's contribution to a stem, run GapFill on a parallel bus, null-test against the source, feed the wet part to further processing.
- Switching modes crossfades over 5 ms (no click). Latency and PDC do not change: the wet is still aligned with the lookahead-delayed dry the host compensates for.
- **Bypass wins:** when the plugin or the host is bypassed, the plugin is transparent (dry passes) as today; the chip reads "WET ONLY (bypassed)".
- Monitoring aids that deliberately colour the output (Band Solo, Audition detector) still work and flag "monitor aid active - not bit-exact".

## 2 Success/quality metric
- Exactness: `memcmp` of the output channel(s) against the engine wet buffer after post-processing is 0 differences for 30 s of scenario A-I at every block size 32-8192 and rates 44.1-192 kHz (test accessor under `GAPFILL_UNIT_TESTS`).
- Null test: Normal (Dry 100, Wet 100, Out 0) minus (WetOnly + DryOnly render) = exactly 0 samples differ.
- Transition: max |delta| across a mode switch <= 0.02 on a -6 dBFS wet signal (5 ms linear).
- Normal mode with the feature present is bit-identical to the pre-feature render (golden A-I). Zero added latency, zero allocation.

## 3 Design
Module `Source/features/wetonly/` (`WetOnly.h/.cpp`, `Gui.*`, `Help.cpp`). Pure gain-law class, no audio buffers, so it is tested without a processor:
```cpp
class WetOnlyMix {
public:
    struct Gains { float dry, wet, out; };
    void prepare (double sampleRate) noexcept;                 // 5 ms linear smoother
    void setWetOnly (bool) noexcept;                           // once per chunk from the cached parameter
    Gains next (float d, float w, float o, float bypassMix) noexcept;   // once per sample, after the existing lerps
    bool isSettled() const noexcept;
};
```
`next`: `wo = smoother.getNextValue() * (1 - bypassMix)`; `if (wo <= 0) return {d, w, o}; if (wo >= 1) return {0.0f, 1.0f, 1.0f};` else `{d*(1-wo), lerp (w, 1, wo), lerp (o, 1, wo)}`. The two exact branches are what make steady-state output bit-perfect (a general lerp can leave 1 ulp). `juce::SmoothedValue` (linear) lands exactly on its target.

Hook in `processChunk` mix loop (3 lines): after computing `d, w, o` (bypass lerps unchanged) call `const auto g = wetOnlyMix.next (d, w, o, byp);` and use `g.dry/g.wet/g.out` in the existing `l = dryL[i]*g.dry + wetL[i]*g.wet; l = sanitise (l*g.out, 16)`. At `g = {0,1,1}` this evaluates `dry*0 + wet*1` then `*1`: dry is already sanitised (finite) so `0*dry = 0` and the sum is exactly `wet` (including -0 normalisation: `+0 + wet`, test covers `-0.0f`). `wetOnlyMix.setWetOnly (pb (params.fm17_mode))` beside the other `setTargetValue` calls; `prepare()` in `prepareToPlay`. Band Solo and Audition code after the loop is untouched (documented exclusion). Metering: `meterOut` reads the produced output, so it shows wet only; the "wet" meter is unchanged.

**Routing safety (foot-gun guard).** A hidden wet-only state would make a project go silent after loading a preset. So `fm17_mode` is *preserved* (not overwritten) by factory preset apply, Reset, Undo and `.gapfill` file load, and *restored* by DAW session load (`setStateInformation`) and host automation. Implementation: `routingParameterIDs()` (`{"fm17_mode"}`) in `Source/features/wetonly/Routing.h`; `resetToDefaults`, `applyPreset` and `applyParameterState(..., bool preserveRouting)` skip those IDs when asked (undo and file load pass true, session restore false). This edits `Presets.cpp` and `PluginProcessor.cpp` by a few lines; label the PR `hot-file-ok` and add tests below. Also excluded from Random/Surprise.

## 4 Parameters & state
Group `GAPFILL_PARAMS_FM17(X)` (`Source/params/fm17.h`, `tools/param_ids/fm17.lock`); `_` regex note in F-M13 s4.

| ID | Type | Range | Default | Unit |
|---|---|---|---|---|
| `fm17_mode` | choice | Normal, Wet only | Normal | - |

Automatable (stepped), so a host can render a wet-only pass in a chosen region; automation toggling is click-free (5 ms). No subtree. **Neutral proof:** default Normal => `wo = 0` => `{d,w,o}` returned unchanged (branch, not arithmetic), so Normal mode output is bit-identical (`normal_mode_golden_unchanged`). Old sessions lack the ID and load Normal.

## 5 Real-time safety
Per sample: one smoother tick and two comparisons; no allocation, locks or IO. The smoother is reset in `prepareToPlay` and `reset()` (current = target, so a restored state starts settled, no 5 ms ramp on load). Sample-rate change re-prepares. NaN/Inf: dry is sanitised on entry and the wet bus is sanitised in the wet post-processing, so `0*dry` cannot produce NaN; the final `sanitise (.., 16)` is unchanged and a no-op for in-range finite values.

## 6 UI
Slot: Mix panel (`MixPanel`), a two-segment selector "OUTPUT: NORMAL | WET ONLY" under the Dry/Wet/Output knobs (not the header, 11). In Wet only: the three knobs dimmed (still readable, still hold their values) with a footer chip "WET ONLY"; chip text includes "(bypassed)" when bypassed and "monitor aid active" when Band Solo/Audition is on. Tooltip: "Output only what GapFill generates, at unity, with no dry signal. Dry / Wet / Output are ignored. Use it for stems and parallel setups." Help "Wet-only output" (order 1170): the exact guarantees, bypass behaviour, why presets do not change this switch, how to null-test. Accessible: group title "Output mode", segments as radio buttons announcing "Wet only, dry, wet and output level controls bypassed". Keyboard: arrows switch segments, Space applies. Reduced motion: dimming is instant. Narrow: selector keeps a 28 px height and wraps below the knobs at minimum width.

## 7 MIDI/automation/CLAP/host impacts
Automatable and MIDI-learnable choice. **Host multi-output buses are F-L12, not this ticket.** F-L12 adds an optional second output bus "Wet" (disabled by default) and `fl12_main_mode` (Dry + Wet / Dry only), including the sidechain/aux channel-aliasing rules, `isBusesLayoutSupported` changes and per-format validation. Interaction rules when both exist: (1) `fm17_mode = Wet only` is a *main output* mode and takes precedence over `fl12_main_mode`: main output is the wet signal at unity, the Wet bus (if enabled) still carries the same wet signal at its own level trim; (2) with `fm17_mode = Normal`, F-L12 behaves exactly as its spec; (3) the shared tap is the post-width `wetBuffer`, which neither ticket modifies; (4) bypass wins over both. Tests `wetonly_overrides_fl12_dry_only` and `wet_bus_unaffected_by_fm17` belong to whichever ticket lands second.

## 8 Tests (`Tests/features/TestWetOnly.cpp`)
`normal_mode_golden_unchanged` | `wetonly_equals_wet_bus_bit_exact` (scenarios A-I, all block sizes, 5 rates) | `null_test_wetonly_plus_dryonly_equals_normal` | `knobs_ignored_in_wetonly` (Dry/Wet/Out at extremes, output identical) | `dry_not_present` (silence input after the last fill ends => output exactly 0) | `transition_5ms_bounded_click` | `steady_state_exact_after_settle` | `bypass_wins_and_is_transparent` | `host_bypass_path_processBlockBypassed` | `minus_zero_normalised` | `nan_dry_input_no_nan_output` | `latency_unchanged_when_toggled` | `solo_and_audition_flagged_not_exact` | `preset_apply_preserves_mode` | `reset_preserves_mode` | `undo_preserves_mode` | `gapfill_file_load_preserves_mode` | `session_restore_applies_mode` | `old_state_loads_normal` | `no_audio_thread_allocation`. Add accessor `getLastWetBufferForTests()` under `GAPFILL_UNIT_TESTS`.

## 9 Dependencies & build order
None required beyond Gate B (B2 parameter registry, B6 state, B7 tests). Independent of every other F-M ticket; build early (small, high value). Shares the wet tap with F-L12 (Wet bus) and F-L04 / U-2 (wet export); no code dependency either way, precedence rules in s7. F-S06 (A/B level match) reads the same bus.

## 10 Effort & risks
M. Risks: users forgetting the switch (chip, preserved-on-preset rule documented in Help, owner may prefer to treat it like `bypass` and not preserve: decision flagged); hot-file edits in `Presets.cpp`/`PluginProcessor.cpp`; "bit-perfect" claim must be scoped (Band Solo, Audition, float-to-host conversion by the host are out of GapFill's control; wording in Help).

## 11 Acceptance checklist
- [ ] Wet-only output memcmp-equal to the post-processed wet bus (all tested block sizes/rates)
- [ ] Null test passes exactly; Normal mode golden unchanged
- [ ] Bypass behaviour, latency/PDC and mode-switch click verified
- [ ] Presets/Reset/Undo/file load preserve the mode; session load restores it
- [ ] Chip, tooltip, Help, accessible names, minimum-size screenshot reviewed
- [ ] Real-host check: REAPER offline render of a wet-only pass, plus one more host per OS, recorded; unverified hosts listed
- [ ] Precedence with F-L12 (`fl12_main_mode`) implemented or tested by the second ticket to land
