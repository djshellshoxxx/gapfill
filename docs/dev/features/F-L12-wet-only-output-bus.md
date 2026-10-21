# F-L12 Flexible wet-only output bus
Status: implementation spec, Gate C candidate (backlog `08` F-L12; related F-M17 "bit-perfect wet-only render mode", which is a separate in-plugin switch). Category VAL, value H, size M-L. No full spec exists: this is the canonical spec. Requirements: `08` ("optional host-side separate wet bus alongside dry/stereo output; changes bus layout and latency/preset testing"), `10` section 4.5 (wet contribution only, no latency/PDC change).

## 1 Summary & user-visible behaviour
- GapFill gains an optional second output bus named **Wet** (mono or stereo, **disabled by default**). When the host enables it, that bus carries only what GapFill generated: post Low/Centre Protect and width, level-trimmed, with no dry signal. Use it to process fills separately (reverb, EQ, parallel compression) or print them to their own track.
- **Main output mode** (`fl12_main_mode`): *Dry + Wet* (today's behaviour, default) or *Dry only*. Dry only removes the fills from the main output so the user can rebuild the mix from the two buses. It takes effect **only while the Wet bus is enabled**; if the bus is off the main output always contains the wet signal, so the fills can never silently disappear.
- **Wet bus level** (`fl12_level`, -24 to +12 dB, default 0). The Wet knob does not affect the Wet bus (it controls the main-output mix only); the Dry knob and Output trim also leave it alone. Bypass silences the Wet bus (click-free).
- The existing stereo sidechain input stays as bus 1 of the inputs. Existing sessions and presets load unchanged, and the plugin opens with exactly the same buses as today.

## 2 Success/quality metric
- Equivalence: in Mix mode with the Wet bus disabled, output is bit-identical to today (golden A-F). With the Wet bus enabled at 0 dB, Wet-bus samples equal the internal wet buffer after width/protect exactly (max abs error 0) and equal the F-L04 export tap for the same segment.
- Dry only: main output equals the dry path (`dry gain * delayed input * output trim`) within 1e-7 over a 60 s render; Mix mode main equals `main(DryOnly) + wetGain * wetBus / levelGain` within 1e-6.
- No clicks on toggling mode, bypass or bus enable (max sample delta in the 50 ms after the change <= 1.5x the steady-state max delta), no reported latency change, pluginval level 10 passes with the bus enabled and disabled.

## 3 Design
Files (`cmake/sources/wetbus.cmake`, `Tests/features/TestWetBus.cpp`; `Source/params/wetbus.{h,cpp}`):
- `Source/features/wetbus/WetBus.{h,cpp}`:
  ```cpp
  class WetBusStage {
  public:
      void prepare (double sr) noexcept;  void reset() noexcept;
      /** wetL/wetR: engine wet buffer after protect/width. out: the Wet bus view (0 channels = disabled). */
      void render (const float* wetL, const float* wetR, int n, juce::AudioBuffer<float>& outBus, int outStart,
                   float levelDb, float bypassMix01, bool silentDiagnostics) noexcept;   // always overwrites every channel/sample, writes zeros when silent
      float mainWetScale (bool busEnabled, int mainMode) noexcept;                      // smoothed 1 -> 0 over 20 ms when Dry only AND busEnabled
  };
  ```
  Level and mode changes use `juce::SmoothedValue` (20 ms linear). Mono Wet bus: `0.5 * (L + R)`; stereo: L, R. A mono Main with stereo Wet bus writes the stereo wet buffer (width keeps its meaning; `wetMono` still applies earlier).
- Bus declaration (`PluginProcessor.cpp` constructor): append `.withOutput ("Wet", juce::AudioChannelSet::stereo(), false)` after the main output. Input order is unchanged (Input, Sidechain), so existing sidechain handling and saved host routing are unaffected.
- `isBusesLayoutSupported` addition (existing checks for main in/out and the sidechain stay): `if (layouts.outputBuses.size() > 2) return false; if (layouts.outputBuses.size() > 1) { const auto wet = layouts.getChannelSet (false, 1); if (! wet.isDisabled() && wet != mono && wet != stereo) return false; }`. Main in must still equal main out. JUCE default layouts keep the Wet bus disabled.
- `processChunk`: after the width loop and before the mix loop, `const bool wetBusOn = getBusCount (false) > 1 && getBus (false, 1) != nullptr && getBus (false, 1)->isEnabled();`. If `wetBusOn`: `auto wetOut = getBusBuffer (buffer, false, 1); wetStage.render (...)`. The mix loop multiplies the wet term by `wetStage.mainWetScale (wetBusOn, mode)`. **Channel aliasing:** JUCE hands plugins one in-place buffer with `max (inputChannels, outputChannels)` channels, so Wet bus channels 2-3 share storage with the sidechain input channels 2-3. The sidechain is copied to `monoSide` at the top of `processChunk` before any output is written, which keeps this safe; a regression test locks that order, and `render` overwrites all of its samples so no sidechain data leaks to the Wet bus. Silent states (Audition monitor mode, band-solo diagnostics) write zeros to the Wet bus.
- `processBlockBypassed` already routes through `processChunk (..., true)`; the Wet bus level is multiplied by `(1 - bypassMix)` so it fades with the existing `bypassMix` smoother.
- F-L04 shares the same tap point (the post-width `wetBuffer`); neither stage modifies it.
- `Gui.{h,cpp}` (Options group "Outputs"), `Help.cpp`, `Info.cpp`.
Research: JUCE supports additional buses declared in `BusesProperties` and disabled by default; hosts differ in how they expose them (AU/VST3/Reaper/Logic behaviours discussed in https://forum.juce.com/t/getting-multiple-buses-working-for-au-and-vst3/60078, bus-layout tutorial https://docs.juce.com/master/tutorial_audio_bus_layouts.html). Advice from that thread: do not override `canAddBus`/`canRemoveBus` for Logic compatibility; this design does not.
Minimal hooks in existing files: constructor `BusesProperties`, `isBusesLayoutSupported`, three lines in `processChunk`, mix-loop wet scale.

## 4 Parameters & state
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fl12_main_mode` | choice: Dry + Wet, Dry only | Dry + Wet | - |
| `fl12_level` | float -24 to +12, step 0.1 | 0 | dB |
Appended in `params/wetbus.h`, locked in `tools/param_ids/wetbus.lock`. `fl12_main_mode` is in `performanceParameterIDs()` (never randomised, never in Undo); `fl12_level` too. Bus enable state is owned by the host and saved in the host project, not in plugin state; GapFill records nothing about it. Old sessions: IDs absent -> defaults -> identical sound; hosts that saved only the original bus layout keep the Wet bus disabled. Neutral proof: Mix mode with the bus disabled executes exactly today's mix expressions (`mainWetScale` returns the constant 1.0f and the loop is untouched); golden A-F bit-identical; latency unchanged (verified via `getLatencySamples`).
Presets: factory presets do not set `fl12_*` (preset pass is the only place that edits them). `.gapfill` files containing `fl12_main_mode = Dry only` load, and the UI shows a hint when the bus is off ("Wet bus is off: fills stay in the main output").

## 5 Real-time safety
Audio: per sample two smoothed gains and two stores; no allocation (no new buffers; the host buffer is used in place), no locks. Bus enable can change between `processBlock` calls, only after `prepareToPlay` in JUCE; the code reads `isEnabled()` per chunk without caching across blocks. The Wet bus is fully overwritten every chunk (never left with sidechain residue). Resets: `reset()` zeroes the smoothers to the current targets; sample-rate change recomputes ramp lengths; bypass uses the shared smoother; denormals handled by `ScopedNoDenormals` in `processBlock`.

## 6 UI
Placement per `11`: Options page > "Outputs" group (a setting, not a performance control). Controls: "Main output" combo and "Wet bus level" slider with dB readout; a status line "Wet bus: enabled by host (stereo)" / "Wet bus: off - enable it in your DAW's plugin output routing" driven by `isEnabled()` polled by the editor timer. Tooltips: "Dry only: remove fills from the main output. Only applies while the Wet bus is enabled in your DAW." Accessible names "Main output mode", "Wet bus level". Keyboard operable. Reduced Motion: no effect. Narrow: group collapses with the Options page. Help section "Separate wet output" (order 1210) with per-host routing steps (REAPER pin mapping, Bitwig, Cubase, etc.) filled from QA.

## 7 MIDI/automation/CLAP/host impacts
Both parameters automatable. This changes the plugin's **bus layout** metadata, so every format needs re-validation: VST3 (pluginval level 10, bus arrangement negotiation), AU (`auval -v aufx Gpfl Gfau`; multi-output AU behaviour, Logic is expected to expose extra outputs for instruments only and may not for effects, verify), CLAP (clap-validator audio-ports; confirm clap-juce-extensions maps the extra bus to a CLAP audio port and that the port is optional), Standalone (device channel count; the Wet bus stays disabled). Host expectations to verify and record: REAPER (4-channel track pins), Bitwig, Cubase, FL Studio, Ableton Live (VST3 effects with extra outputs may be unusable; document). Hosts that do not support multi-output simply leave the bus disabled and nothing changes. Latency/PDC identical on both buses. Project save/reopen with the bus enabled must restore routing (host) and parameters (GapFill); tested manually.

## 8 Tests
Group `WetBus`: `wetbus_disabled_mix_is_bit_identical` (golden A-F) | `layout_accepts_mono_stereo_disabled_wet` | `layout_rejects_three_output_buses_and_surround` | `layout_sidechain_unchanged` | `wet_bus_equals_internal_wet_buffer` | `wet_bus_contains_no_dry` | `dry_only_main_equals_dry_path` | `dry_only_ignored_when_bus_disabled` | `mix_main_plus_wet_consistency` | `level_db_applied_and_smoothed` | `wet_knob_does_not_scale_wet_bus` | `bypass_fades_wet_bus_silent` | `mono_main_mono_wet_and_stereo_wet` | `wet_bus_overwrites_sidechain_aliased_channels` (sidechain signal present, assert no leakage and sidechain still detected) | `audition_and_solo_silence_wet_bus` | `no_click_on_mode_toggle` | `latency_unchanged_with_bus` | `block_size_invariance_with_bus` | `wetbus_params_roundtrip_old_state` | `wetbus_no_audio_thread_allocation`. Validators: pluginval 10 with bus enabled/disabled, clap-validator, auval. Manual host matrix per section 7.

## 9 Dependencies on other tickets
Hard: Gate A (validated baseline formats; this alters bus metadata), B1/B2 (registration, parameter groups). Shares the tap point with F-L04 (no code dependency). Related: F-M17 (in-plugin wet-only render switch; independent). Soft: F-L13 (sidechain bus indexing must stay bus 1). Build order: after Gate B; independent of the other F-L tickets.

## 10 Effort & risks
Effort M-L (small DSP, large validation matrix). Risks: host variance in multi-output handling for effect plugins (largest; ship with documented per-host table); channel aliasing between sidechain input and Wet output (explicit tests); `pluginval`/`auval`/`clap-validator` surprises with optional output buses; users enabling Dry only and forgetting the Wet bus is silent in the DAW (mitigated: the mode is inert while the bus is disabled, plus a status line); mono-main layouts. No owner decision is blocking, but the Logic/Ableton limitation should be accepted in writing.

## 11 Acceptance checklist
- [ ] Wet bus declared disabled by default; layout checker updated; opens exactly like today
- [ ] Bit-identical main output with the bus disabled; exact wet equivalence with it enabled
- [ ] Sidechain input and Wet output aliasing safe and tested
- [ ] Dry only inert while the bus is disabled; bypass and mode toggles click-free
- [ ] Two IDs appended, locked, classified; old states load unchanged
- [ ] pluginval 10, auval, clap-validator green with bus on and off on every OS
- [ ] Host routing steps recorded for REAPER and one more host; unsupported hosts documented
- [ ] Options UI, status line, tooltips, accessibility done
