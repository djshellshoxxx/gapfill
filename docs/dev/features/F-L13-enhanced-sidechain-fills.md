# F-L13 Enhanced sidechain-driven fills
Status: implementation spec, Gate C candidate (backlog `08` F-L13). Category VAL/LIVE, value H, size M-L. No full spec exists: this is the canonical spec. It extends the existing sidechain support (detector source Main/Sidechain/Hybrid, `protSC` protection, 3 s silence fallback, mono sum of the optional stereo sidechain bus). "Sidechain as the fill source" stays out of scope (parked in `05` backlog: new routing and latency semantics).

## 1 Summary & user-visible behaviour
- **Cleaner key signal**: high-pass and low-pass filters, a trim, and a channel choice (L+R, Left, Right) applied to the sidechain copy the detector listens to. Typical use: HP 80 Hz + LP 8 kHz so a kick key ignores rumble and hats.
- **Hybrid control**: Hybrid keeps today's *Strict* rule (gap confidence is the minimum of main and sidechain), or *Weighted* (a mix, 0-100 % sidechain). Hits from both lanes still protect the main material.
- **Rhythm align**: with Align (0-100 ms) above 0, when a gap opens and a sidechain hit is due within that window, the fill starts **on the sidechain hit** instead of immediately. The external rhythm shapes when fills begin. If the gap ends first the gap is reported as "Missed (align)".
- **Timeout**: how long the sidechain may be silent before GapFill falls back to the main input (1-10 s, default 3 s as today).
- Status line shows what is happening: "Sidechain: L+R, HP 80 Hz, trim 0 dB, hits 7/s" or "Sidechain silent: using main input".
- Defaults reproduce today's behaviour exactly.

## 2 Success/quality metric
- Align accuracy: on a 120 BPM four-on-the-floor sidechain with a sparse main input, with Align = 60 ms, >= 95 % of fills that had a sidechain hit inside the window start within one analysis hop (32 samples = 0.67 ms at 48 kHz) of that hit; with Align = 0, fill start positions are bit-identical to today.
- Filter efficacy: a sidechain with 40 Hz rumble at -10 dB plus 120 BPM clicks produces detection events only on the clicks with HP 80 Hz (event count equals click count +-1) while the unfiltered case shows extra events.
- Neutral: all defaults give bit-identical audio and identical event positions (golden C and the existing sidechain tests unchanged); timeout default unchanged.

## 3 Design
Files (`cmake/sources/sidechain.cmake`, `Tests/features/TestSidechain.cpp`; `Source/params/sidechain.{h,cpp}`):
- `Source/features/sidechain/KeyConditioner.{h,cpp}` (audio thread):
  ```cpp
  struct KeySettings { float hpHz, lpHz, trimDb; int chan; };   // chan 0 L+R, 1 L, 2 R
  class KeyConditioner {
  public:
      void prepare (double sr) noexcept;  void reset() noexcept;
      /** Reads the raw sidechain channels, writes the conditioned mono key into dst. Returns the pre-trim peak (for the "signal seen" test). */
      float process (const float* scL, const float* scR, float* dst, int n, const KeySettings&) noexcept;
  };
  ```
  Implementation: channel pick (`0.5*(l+r)` as today for L+R, else the single channel; mono sidechain bus duplicates). HP and LP use `SVF` (existing, Butterworth `q = 0.7071`): HP at `hpHz`, LP at `lpHz`, each bypassed entirely when at its neutral end (HP <= 20.5 Hz, LP >= 19999 Hz) so defaults perform zero filter arithmetic. Trim gain is skipped at exactly 0 dB. Filter state resets when the channel/HP/LP is bypassed -> engaged (no stale state) and coefficients update at most once per block. **Ordering fix:** the "signal seen" check (`> 1e-6`) and the silence timer use the raw channels (before trim/filters), so trim or a steep HP cannot make GapFill believe the sidechain went silent.
  Hook in `processChunk`: the existing loop that builds `monoSide` is replaced by one `keyConditioner.process (...)` call that writes `monoSide`; audition monitor and the detector both read the conditioned key (what you hear is what the detector hears).
- `AnalysisEngine` Hybrid weighting: `AnalysisSettings` gains `int hybridMode` (0 Strict, 1 Weighted) and `float hybridW01`. In `writeFrame` Hybrid branch: Strict unchanged; Weighted sets `frame.conf = lerp (mainLane.conf, scLane.conf, w)` and band confidences likewise; `frame.transient` and event ORs unchanged (both lanes still raise events). `w = 1` is equivalent to Sidechain mode for confidence while main events remain protected.
- **Align hold** in `FillEngine::controlUpdate`: today the gap state machine sets `gapEvaluated = true` and calls `trySchedule` once at gap entry. New (only when `s.sidechainActive && s.perf.scAlignMs > 0`): at gap entry look up `nextSidechainEvent (outputPos + 1)`; if it exists and lies within `alignSamples = round (scAlignMs*0.001*sr)`, set `evalHoldUntil = thatPos` and do **not** set `gapEvaluated`; on each later hop, when `outputPos >= evalHoldUntil` call `trySchedule` (which recomputes `available/usable` from the current position, so protection and min-gap logic is unchanged) and set `gapEvaluated`; if `inGap` turns false first, record `RejectReason::ScAlignMissed` (new enum value appended before `Count` in F0-2's list, with E-3 text "The gap ended before the sidechain hit") and clear the hold. Because the decision key uses `outputPos`/PPQ at the delayed decision, determinism holds and results are independent of host block size (the hop grid is unchanged). One hold at a time, no queue growth. The sidechain hit used is the earliest one inside the window; hits beyond it are ignored for this gap.
  Deliberately not included: starting fills on sidechain hits when no gap exists, and ending fills on a sidechain hit (the existing `protSC`/next-event fade already ends fills before a protected sidechain hit).
- Timeout: `kSidechainSilenceSeconds` constant becomes `scTimeoutSamples = fl13_timeout * sr` read per block from the `ParamCache`; `sidechainSilentSamples` clamp expression uses it.
- Status/meters: `MeterState` gains relaxed atomics `scHitsPerSec` (EMA of sidechain events per second), `scKeyPeak` (post-conditioning peak); the editor timer composes the status text. Existing `sidechainConnected/Requested` flags are reused.
- `Source/features/sidechain/Gui.{h,cpp}`, `Help.cpp`, `Info.cpp`.
Minimal hooks in existing files: `processChunk` sidechain block (replace one loop), `gatherAnalysisSettings` (two fields), `AnalysisEngine::writeFrame` (one branch), `FillEngine::controlUpdate` (hold logic, ~15 lines, guarded by `scAlignMs > 0`), timeout constant, `RejectReason` enum value.

## 4 Parameters & state
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fl13_sc_hp` | float 20-2000, log skew | 20 (off) | Hz |
| `fl13_sc_lp` | float 1000-20000, log skew | 20000 (off) | Hz |
| `fl13_sc_trim` | float -24 to +24, step 0.1 | 0 | dB |
| `fl13_sc_chan` | choice: L+R, Left, Right | L+R | - |
| `fl13_hybrid` | choice: Strict, Weighted | Strict | - |
| `fl13_hybrid_w` | float 0-100, step 1 | 50 | % (used only when Weighted) |
| `fl13_align_ms` | float 0-100, step 1 | 0 (off) | ms |
| `fl13_timeout` | int 1-10 | 3 | s |
Appended in `params/sidechain.h`, locked in `tools/param_ids/sidechain.lock`. Detector-related controls are **not** randomised, so all eight go in `performanceParameterIDs()` (they are routing/detector settings; Random/Surprise already leave the detector untouched). They are scene-excluded (F-L07 excludes detector settings). `valueFromText` must parse "80", "80 Hz", "1.2 kHz" for the two Hz controls (the 0.0.1 kHz parsing bug noted in `06`). State subtree: none. Old sessions: IDs absent -> defaults -> unchanged. Neutral proof: filters/trim skipped at neutral values, Strict hybrid path is the current code, `scAlignMs == 0` skips the hold branch, timeout default equals the old constant; golden C and sidechain tests bit-identical.

## 5 Real-time safety
Audio: two SVF steps per sample only when engaged; one extra branch in `writeFrame`; hold logic is O(1) per hop. No allocation or locks; all coefficients computed from parameter atomics per block; `std::tan` per coefficient update only when a frequency changes. Edge cases: sidechain bus disabled mid-session -> existing `sidechainBusEnabled` path zeros `monoSide` and the hold is dropped (`s.sidechainActive` false); sidechain loss mid-hold -> hold cleared and the gap evaluates immediately; sample-rate change resets filters; transport jump clears the hold; denormals handled by `ScopedNoDenormals` and the SVF finite checks.

## 6 UI
Per `11` this extends the existing detector panel's Sidechain row, in an expandable "Key" disclosure (no new tab, header untouched): HP, LP, Trim, Channel, Hybrid combo (+ weight knob when Weighted), Align, Timeout in an advanced section. The key meter (post-conditioning peak) and the status text sit in the footer status slot; the existing Audition/solo monitor lets the user hear the conditioned key. Tooltips: "High-pass the sidechain so low rumble does not trigger fills." "Align: if a sidechain hit arrives within this time after a gap opens, start the fill on the hit." Accessible names per control; keyboard operable; Reduced Motion removes meter smoothing animation (numeric peak value stays). Narrow window: Key disclosure becomes a popover. Help section "Sidechain" (order 1220) with a recipe: "kick key: HP 40, LP 500, Align 30 ms".

## 7 MIDI/automation/CLAP/host impacts
Eight automatable parameters (`fl13_sc_chan`, `fl13_hybrid`, `fl13_timeout` stepped: CLAP stepped-hint check). No bus layout change (the sidechain input remains input bus 1; F-L12's Wet output bus is output bus 1). Host sidechain routing differs per DAW (REAPER pin routing, Ableton sidechain selector, FL Studio sidechain routing); document steps in Help. A host that connects the bus but sends silence behaves as today (timeout fallback). No latency/PDC change: filters are IIR and not included in latency; the key is detected from the same hop grid. Offline render: deterministic.

## 8 Tests
Group `Sidechain`: `sc_defaults_bit_identical_golden_c` | `key_conditioner_neutral_skips_filters` | `hp_removes_rumble_events` | `lp_ignores_hats` | `trim_scales_detection_not_silence_timer` | `channel_pick_left_right_mono_bus` | `raw_signal_seen_ignores_trim_and_filters` | `hybrid_strict_unchanged` | `hybrid_weighted_extremes_equal_main_and_sidechain_confidence` | `hybrid_weighted_midpoint_is_lerp` | `align_starts_on_sc_hit_within_one_hop` | `align_zero_identical_starts` | `align_missed_reports_reason_and_does_not_fill` | `align_hold_cleared_on_sc_loss_jump_reset` | `align_block_size_invariant` (32..8192) | `align_respects_min_gap_and_protection` | `timeout_param_controls_fallback` (1 s, 10 s) | `sidechain_bus_disabled_midsession` | `filter_state_reset_on_engage` | `valueFromText_hz_khz` | `sc_params_roundtrip_old_state` | `sc_no_audio_thread_allocation`. Fixtures: `TestSignals` click track + rumble + sparse main; existing sidechain scenario C.

## 9 Dependencies on other tickets
Hard: B3 (stages, `RejectReason`), B2 (parameter groups). Soft: E-3 (reason text for `ScAlignMissed`), F-L09 (groove can follow the sidechain rhythm because it listens to detection events), F-L11 (sidechain hits as MIDI source), F-L12 (bus index coexistence), F-S02 (status line). Build order: independent after Gate B; can ship before F-L07..F-L12.

## 10 Effort & risks
Effort M-L (filters and hybrid are small; the align hold touches the gap state machine). Risks: align hold changes *when* the first evaluation happens, so gaps that closed during the hold are lost (reported, user can raise tolerance or disable); interactions with Phrase/Breathing multipliers (they read PPQ at the delayed decision: acceptable, tested); users expecting "fill source = sidechain" (explicitly not provided; Help states it); host sidechain routing confusion.

## 11 Acceptance checklist
- [ ] Eight IDs appended, locked, classified; neutral defaults prove bit-identical output
- [ ] Silence timer unaffected by trim and filters; timeout configurable
- [ ] Weighted hybrid math verified at 0, 50, 100 %
- [ ] Align accuracy within one hop; block-size invariant; reason `ScAlignMissed` visible
- [ ] Status/meter shows key activity; conditioned key audible in the monitor mode
- [ ] Hz/kHz text parsing correct; stepped parameters behave in CLAP
- [ ] AllocGuard green; existing sidechain tests unchanged
- [ ] Manual: sidechain routing verified in REAPER (Windows and Linux) and one more host
