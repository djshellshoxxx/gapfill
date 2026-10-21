# F-L09 Groove inheritance
Status: implementation spec, Gate C candidate (backlog `08` F-L09). Category MUS/VAL, value H, size L. No full spec exists: this is the canonical spec. Requirements source: `10` section 4.4 "Groove inheritance" (stable onset offsets from a bounded recent window, confidence shown, 0-100 % amount, jitter/variable block sizes are not swing, only fill subdivisions move, reset/learn and bypass).

## 1 Summary & user-visible behaviour
- GapFill listens to the rhythm it already detects, measures how late or early the off-grid hits sit against the host's beat grid, and makes the **internal subdivisions of its fills** (stutter repeats, delay taps, scramble slices) follow that feel. A straight stutter over a swung hi-hat then swings too.
- Controls: **Groove** amount (0-100 %, default 0 = off), **Grid** (1/8 or 1/16 steps), **Hold** (freeze the learned groove), **Learn** (clear and measure for 8 bars, measure-only), **Reset**.
- Readout: `Groove: swing 62 %, confidence 74 %` or `Groove: unclear (straight)`. Below the confidence gate (35 %) the applied offsets are 0.
- Only wet subdivisions move. Fill start, gap detection, protection, dry audio and host timing never change. Requires valid transport; otherwise measuring pauses and offsets are 0 ("Groove: waiting for host tempo").

## 2 Success/quality metric
- Accuracy on synthetic swung hats (swing 50 %, 58 %, 62 %, 66.7 %; 90-150 BPM; 1/8 and 1/16 grids; with 0 and 40 ms lookahead latency): after 4 bars the estimated offset of every active slot is within +-0.05 step of truth and the displayed swing within +-3 points; straight input yields |shift| <= 0.02 step and confidence either < 35 % or offsets ~0.
- Robustness: no tracker update for events within 1 beat after a transport jump; host block size changes (32..8192) change estimates by < 0.01 step; random jitter (+-3 ms) on a straight pattern does not raise displayed swing above 52 %.
- Amount 0: audio bit-identical, zero tracker CPU.

## 3 Design
Files (`cmake/sources/groove.cmake`, `Tests/features/TestGroove.cpp`; `Source/params/groove.{h,cpp}`):
- `Source/features/groove/GrooveTracker.{h,cpp}` (audio thread, plain data, ~300 bytes):
  ```cpp
  struct GrooveSnapshot { float offsetSteps[8]; float confidence; float swingPercent; bool valid; };   // offsets in steps, +late
  class GrooveTracker {
  public:
      void prepare (double sr) noexcept;  void reset() noexcept;
      void setGrid (int gridSel) noexcept;                                    // 0: 1/8 (0.5 beat), 1: 1/16 (0.25 beat)
      void onEvent (double eventPpq, float strength01) noexcept;              // detection event, PPQ of the INPUT time of the hit
      void onTransportJump() noexcept;                                        // clears, arms a 1-beat settle
      void tick (double ppqNow, bool transportValid) noexcept;                // per hop; handles settle and idle decay
      GrooveSnapshot snapshot() const noexcept;
      void load (const GrooveSnapshot&) noexcept;                             // Hold restore
  };
  ```
  Model: grid step `g` beats (0.5 or 0.25). For an event at `ppq`: `u = ppq / g`, `k = round (u)`, `x = u - k`; accept only `|x| <= 0.40` (ghost/triplet-or-off-grid notes are ignored, not clamped). Slot `s = k mod 8` (8 slots = 4 beats at 1/8, 2 beats at 1/16; use a positive modulo). Per slot keep `mean`, `var`, `n` (saturating 255): `w = 0.1 + 0.9 * strength01`, `mean += a*w*(x-mean)`, `var += a*w*((x-mean)^2 - var)`, `a = 0.25` (about the last 4 hits in that slot, bounded memory). A slot untouched for 16 bars halves `n` (idle decay in `tick`) so stale feel fades. Confidence: `c_s = min (1, n_s/4) * (1 - clamp (sqrt (var_s)/0.20, 0, 1))` for slots with `n_s >= 3`; `confidence = mean(c_s over those slots) * min (1, activeSlots/3)`. Swing % = `50 + 50 * mean of mean_s over odd-parity slots` (off-beat steps), i.e. a triplet swing (off-beat 0.333 step late) reads 66.7 %; ranges clipped to 40-75.
  **Latency-correct event time (important):** the engine evaluates detection events when `outputPos == eventPos`, but the decision-key PPQ expression `ppqAtBlockStart + (outputPos - blockStartPos)/samplesPerBeat` refers to the output timeline, which lags the input by the lookahead. The hit occurred in the input `L` samples earlier, so `eventPpq = ppqAtDecision (eventPos) - L / samplesPerBeat` with `L = round (lookaheadMs * 0.001 * sr)` (new neutral field `EngineSettings::perf.latencySamples`, filled from the processor's `targetLatency`). A unit test with `L = 40 ms` proves recovery of known offsets; without the correction the result is wrong by L.
  Event source: the detection lane (`nextDetectionEvent`), so with `detSource = Sidechain` the groove follows the external rhythm (links to F-L13); strength = `frame.transient`. Only fed when `fl09_amount > 0` or Learn is armed and not `fl09_hold`.
- `Source/features/groove/GrooveWarp.{h,cpp}` (pure): `float warpBeats (const GrooveSnapshot&, int grid, double ppq)` returns the shift `A(ppq)` in beats: piecewise-linear periodic interpolation through the slot points `A(k*g) = offsetSteps[k mod 8] * g * amount01`, clamped to +-0.35 step. Max slope is `0.7 < 1`, so the warp is strictly monotonic and never reorders subdivisions. This interpolation is why fills that start off-grid (every fill starts where its gap starts) still inherit feel: each repeat boundary at musical position `b` is moved by `A(b)`.
  `void makeShifts (const GrooveSnapshot&, int grid, double fillStartPpq, double periodBeats, double samplesPerBeat, float amount01, GrooveShifts& out)`: `out.shift[k] = round (A (start + k*period) * spb)` for `k < 64`, then clamp each shift to +-0.35 of the period in samples and enforce `t_k + shift_k` increasing by >= 0.3 period. Called once at fill start (bounded loop of 64).
- `VoiceSetup` gets `GrooveShifts groove` (POD: `bool on; int count; int shift[64];` neutral `on=false`). `FillVoice` hooks (three places, each guarded by `groove.on`): Stutter repeat length for repeat `k` becomes `period + shift[k+1] - shift[k]` in `genStutter`; Delay tap `k` offset becomes `k*delaySamples + shift[k]` where taps are set up; Scramble slice `k` start uses `sliceLen*k + shift[k]`. Algorithms with no subdivision grid (Reverse, Freeze, Pitch*, Filter, Texture, Crush, Distort, Spectral, Comb, TapeStop, Granular) ignore it. Only used when Sync is on (a division exists); with Sync off nothing shifts.
- State persistence: `<Groove v="1" hold="0|1" conf="0.74" o="0.00,0.31,..."/>` (8 offsets), saved only when `fl09_hold` is on so a held feel survives reload; validated ranges (finite, |o| <= 0.4, conf 0..1); bad -> empty, tracker restarts.
- `Source/features/groove/Gui.{h,cpp}`, `Help.cpp`, `Info.cpp`: amount knob, grid combo, Hold toggle, Learn/Reset buttons, readout plus an 8-point mini plot of offsets (text values in tooltip).
Data flow: hop loop in `FillEngine::controlUpdate` -> `tracker.onEvent` -> snapshot atomically copied (seqlock-free: audio thread is the only reader/writer; UI gets a relaxed-atomic copy of `confidence`, `swing`, `offsetSteps` after each beat) -> `makeShifts` at `trySchedule` -> voice.
Minimal hooks in existing files: three lines in `controlUpdate`, one in `trySchedule`/`toVoiceSetup`, three guarded blocks in `FillVoice.cpp`, `noteTransportJump` forwards to the tracker.
Research basis: swing/microtiming estimated from onset positions relative to a beat grid; swing ratio estimation methods are surveyed in the DAFx literature (e.g. Swing Ratio Estimation, DAFx-15: https://dafx.de/paper-archive/details/dAxTOO3pHjYwTaMRKMs8PQ). This design deliberately uses the host's PPQ grid instead of beat tracking, so tempo-detection errors cannot contaminate the result.

## 4 Parameters & state
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fl09_amount` | float 0-100, step 1 | 0 | % |
| `fl09_grid` | choice: 1/8, 1/16 | 1/16 | - |
| `fl09_hold` | bool | off | - |
All three in `performanceParameterIDs()`; appended in `params/groove.h`, locked in `tools/param_ids/groove.lock`. Old sessions: absent -> amount 0 -> off. Neutral proof: tracker is not fed and `makeShifts` not called when `amount==0`; `VoiceSetup::groove.on=false` leaves voice code on the existing path (no float change); golden A-F bit-identical; zero RNG draws. Learn/Reset are commands.

## 5 Real-time safety
Audio: per detection event ~20 flops; per hop `tick` O(8); per fill start a 64-iteration loop; no allocation, locks, logging. All arrays fixed. Resets: `reset()`, sample-rate change, transport jump and bypass clear the tracker unless Hold is on (then `load`ed snapshot stays). Settle window: events within 1 beat after any jump are ignored (prevents a false groove from a seek landing mid-bar).

## 6 UI
Slot `CreativeExtra` advanced disclosure (11 guardrail: not on the header). Tooltips: "Groove: make stutter, delay and scramble subdivisions follow the swing of your music. Needs host tempo." "Learn: measure the next 8 bars." Mini plot has a text equivalent in the accessible description ("swing 62 percent, confidence 74 percent"); confidence shown as number, not only colour. Learn shows a numeric bar counter; Reduced Motion removes sweeps. Narrow window: plot hidden, readout kept. Help section "Groove" (order 1180) explains that it needs rhythmic input and Sync on.

## 7 MIDI/automation/CLAP/host impacts
Three parameters (amount automatable and MIDI-learnable). Needs `getPpqPosition` and tempo; Standalone has none -> inactive (message shown). Hosts with loop/seek: tracker clears via the existing jump detection (`abs(ppq - expected) > 0.25` rule; see issue #10 for variable block sizes: this ticket is gated on that fix). Latency: a lookahead change alters `L`, and the correction follows the processor's `targetLatency`. No PDC change.

## 8 Tests
Group `Groove`: `groove_off_is_bit_identical` | `straight_input_zero_offsets` | `swing_58_62_667_recovered_1_8_and_1_16` | `latency_correction_recovers_offsets_at_40ms` | `latency_without_correction_fails` (guard test documenting the bug) | `offgrid_notes_ignored` | `jitter_not_treated_as_swing` | `block_size_invariance_32_to_8192` | `transport_jump_clears_and_settles` | `seek_landing_midbar_no_false_groove` | `tempo_ramp_ppq_domain_stable` | `hold_freezes_and_roundtrips` | `confidence_gate_makes_offsets_zero` | `warp_monotonic_max_slope_below_1` | `shifts_clamped_and_increasing` | `only_wet_subdivisions_move` (dry unchanged, fill start unchanged) | `sync_off_no_shift` | `stutter_delay_scramble_follow_warp` (measured repeat times vs expected) | `nonsubdivision_algorithms_ignore_groove` | `groove_state_validation_and_old_state` | `groove_no_audio_thread_allocation`. Fixtures: `Tests/fixtures/groove/swung_hat_{58,62,67}_{100,120,140}bpm.f32` generated by `TestSignals` (synthetic, redistributable).

## 9 Dependencies on other tickets
Hard: B3 (stages, `VoiceSetup`), F0-5 (`TransportInfo`), transport correctness (#10 / Gate A W-03), E-2 (division/tempo plumbing; Sync must produce a division). Soft: F-L08 (shared PPQ/meter helpers), F-L13 (external rhythm source), F-L10. Build order: after F-L08 and E-2.

## 10 Effort & risks
Effort L. Risks: confusing UX if input is not rhythmic (confidence gate and readout); `FillVoice.cpp` changes in three algorithms (hot file; keep each hook <= 10 lines and golden-protected); tracker accuracy on live, non-quantized material; hosts reporting wrong PPQ while looping; phase of the lookahead correction when latency changes mid-measurement (tracker clears on latency change > 1 ms).

## 11 Acceptance checklist
- [ ] Three IDs appended, locked, classified
- [ ] Latency-corrected event time verified with 0 and 40 ms lookahead
- [ ] Accuracy, jitter, block-size, jump and seek tests green
- [ ] Only wet subdivisions move; dry, start time and protection unchanged
- [ ] Confidence gate and "straight" fallback visible; Hold persists and validates
- [ ] Voice hooks guarded; golden and old-state fixtures unchanged
- [ ] AllocGuard green; Help, tooltips, accessible description done
- [ ] Manual: swung loop in REAPER (Windows) audibly follows the groove at 62 %
