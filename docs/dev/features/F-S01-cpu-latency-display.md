# F-S01 CPU and latency display
Status: implementation spec, Gate C candidate (not authorized before Gates A and B). Category UX/ESS, value H, size S.

Existing state: the header already shows `latency N smp` (`latencyLabel`, fed by `MeterState::latencySamples`). No CPU figure exists. This ticket extends the latency text and adds a measured DSP-load readout.

## 1 Summary & user-visible behaviour
- Header latency label becomes `latency 1920 smp / 40.0 ms`; at narrow widths it collapses to `40.0 ms`. A trailing `*` plus tooltip line "Change pending, host will be told shortly" shows while the reported PDC differs from the target (lookahead was just moved).
- Options page gets a **Diagnostics** group (slot `OptionsExtra`): `DSP load (this instance): 4.2 % avg, 7.9 % peak (10 s), overruns 0`, plus reported latency, tail length (2.5 s) and sample rate / block size in use.
- Footer shows a CPU message only when actionable (docs/dev/11 rule): avg load >= 70 % for 2 s, or any overrun in the last 10 s. Text states reason and next action: "High DSP load 78 % - raise the buffer size, shorten Lookahead or lower Complexity" / "Audio overload 3 s ago".
- All figures are labelled **measured**. There is no predicted-CPU number; the only derived value is "headroom" = 100 % - peak, labelled "est." in the tooltip because host scheduling is outside our control.

## 2 Success/quality metric
- Latency text equals `getLatencySamples()` and the host-reported PDC in 100 % of test cases at 44.1/48/88.2/96/192 kHz.
- Measured load within +/-2 percentage points of an external reference (synthetic busy-wait of known duration) in unit tests.
- Meter overhead < 0.1 % of a 512-sample block at 48 kHz (benchmark in `perf_baseline.json`); golden output byte-identical.

## 3 Design
New module `Source/features/telemetry/` (cmake fragments `cmake/sources/telemetry.cmake`, `cmake/tests/telemetry.cmake`):
- `LoadMeter.h` (header-only, no JUCE GUI types):
  ```cpp
  class LoadMeter {
  public:
      struct Snapshot { float avg = 0, peak = 0; std::uint32_t overruns = 0; std::int64_t msSinceLastOverrun = -1; bool active = false; };
      void prepare (double sampleRate) noexcept;                       // not audio thread
      void registerBlock (double busySeconds, int numSamples) noexcept; // audio thread, O(1)
      Snapshot read() const noexcept;                                  // any thread, relaxed atomics
      void reset() noexcept;
  };
  struct ScopedBlockTimer { explicit ScopedBlockTimer (LoadMeter&, int numSamples) noexcept; ~ScopedBlockTimer(); };
  ```
  Time source `juce::Time::getHighResolutionTicks()`; ticks-per-second cached in `prepare`. We do not use `juce::AudioProcessLoadMeasurer` because it takes a `SpinLock` in the audio path (juce_AudioProcessLoadMeasurer.h); an own ~40-line meter keeps AGENTS.md rule 6 trivially true.
- Algorithm: `load = busySeconds / (numSamples / sr)`. `avg` is a time-weighted EMA, tau = 1 s (`alpha = 1 - exp(-dt/tau)`, dt = block duration). `peak` is the max over two rolling 5 s buckets (O(1), covers 5-10 s). Overrun = `load >= 1.0`; counter saturates at 65535. The first 2 blocks after `prepare` are ignored. `active` goes false if no block registered for > 1 s (editor computes this from a block counter, not a clock on the audio thread).
- `Telemetry.{h,cpp}`: `struct TelemetryView { LoadMeter::Snapshot load; int reportedLatencySamples; int targetLatencySamples; double sampleRate; int blockSize; }` built on the message thread from the processor; `juce::String formatLatency (int samples, double sr, bool compact)`; `juce::String describeLoad (const Snapshot&)`; `std::optional<juce::String> loadWarning (const Snapshot&, WarningState&)` with 5 point hysteresis (raise at 70 %, clear at 60 %).
- `Gui.{h,cpp}`: `DiagnosticsRows` component (OptionsExtra slot) and `LoadWarningProvider` that feeds the status line (priority registered with U-9: learn > overload warning > export > preview > capture > gap outcome > hover).
- Existing-file hooks (minimal): `PluginProcessor.h`: `LoadMeter loadMeter; LoadMeter& getLoadMeter()`; `PluginProcessor.cpp`: one `ScopedBlockTimer` line at the top of both `processBlock` and `processBlockBypassed` (whole callback, not per sub-chunk), `loadMeter.prepare()` in `prepareToPlay`; `PluginEditor.cpp`: `latencyLabel` text now from `formatLatency`.
- Data flow: audio thread -> `LoadMeter` atomics -> editor 24 Hz timer -> `Telemetry` view -> widgets. Nothing flows back.

## 4 Parameters & state
- No APVTS parameters (diagnostic, must never be automatable). Reserved prefix `fs01_` unused.
- Persistent preference `telemetry.showFooterWarnings` (bool, default true) in the global settings file (F-S03). Nothing in plugin state, so old sessions and presets are untouched and the feature is neutral by construction (no audio path change; proof = golden test).

## 5 Real-time safety
Audio thread: two tick reads and ~12 float ops plus 4 relaxed atomic stores per callback. No allocation, locks, IO, or logging. `Snapshot` read uses relaxed loads of independent atomics (tearing between fields is acceptable and documented). Bounded: constant work independent of block size.

## 6 UI
- Placement: latency stays in header (existing); diagnostics in Options (`OptionsExtra`); warning in `FooterStatus`. No new permanent header control (docs/dev/11).
- Tooltip (latency): "Delay GapFill reports to your DAW for lookahead (plug-in delay compensation). Change it with Lookahead." Tooltip (load): "Measured: time GapFill spent in its own audio callbacks divided by the time available. Does not include your DAW, other plug-ins or drivers, so your DAW's meter will differ."
- Help text: new Help section "Latency and load" registered at order 1010 (use `HelpRegistry::add` once F0-1 lands; until then one entry block in `HelpView.cpp`).
- Accessibility: label `setTitle("Reported latency")`, description includes ms; Options rows are `Label`s with `setTitle` and `setDescription`; warning changes announced via status label accessibility title. Keyboard: rows are not focusable except the Options close button (read-only text).
- Reduced motion: no animation; numbers update at <= 2 Hz in Options (coalesced) to avoid screen-reader chatter.
- Narrow size (1020 px min): compact latency form; footer warning truncates with ellipsis, full text in tooltip.

## 7 MIDI/automation/CLAP/host impacts
None for MIDI or automation. PDC value shown is exactly what `setLatencySamples` last published, so it is correct in CLAP (latency extension) and AU/VST3. Some hosts apply latency changes late (offline render); the pending marker covers this. Standalone has no PDC: text still shows the internal delay.

## 8 Tests (`Tests/features/TestTelemetry.cpp`)
`load_meter_ema_converges` | `load_meter_peak_expires_after_10s` | `load_meter_overrun_counts_and_saturates` | `load_meter_block_size_invariant` (64 vs 4096 with synthetic busy times) | `load_meter_inactive_after_1s_idle` | `load_meter_ignores_warmup_blocks` | `format_latency_all_sample_rates` (0 smp, 1 smp, 192 kHz max) | `latency_pending_marker` | `warning_hysteresis_no_flapping` | `telemetry_golden_output_unchanged` | `telemetry_no_audio_thread_allocation` (alloc guard from F0-7) | `latency_label_matches_get_latency_samples`. Edge cases: `numSamples == 0`, NaN busy time rejected, sample rate change mid-run resets meter.

## 9 Dependencies & build order
Needs F-S03 (settings file) for the preference only (can ship without it, default on). Footer warning integrates with F-S02/U-9 status priorities; ship after F-S02 or register priority locally. No other dependencies. Gate B items B4/B5 (slots) required.

F-X07 (adaptive quality governor) must read `LoadMeter::read()` instead of adding its own per-block clock reads, so the audio thread is timed once.

## 10 Effort & risks
S. Risks: users read the figure as total CPU (mitigated by wording); first-callback jitter (warm-up skip); high-resolution timer cost on exotic platforms (benchmark on Windows/Linux/macOS). Existing tests that read the old `latency N smp` text, if any, must be updated in the same PR.

## 11 Acceptance checklist
- [ ] Latency text shows samples and ms, matches host PDC at all test rates
- [ ] Pending marker appears and clears after lookahead change
- [ ] Load meter unit tests pass; benchmark overhead recorded
- [ ] Footer warning appears only above threshold and never flaps
- [ ] Golden output and allocation-guard tests unchanged/green on all platforms
- [ ] Help section, tooltips, accessible names present; screenshot at minimum size
- [ ] No new APVTS IDs; old sessions load unchanged
