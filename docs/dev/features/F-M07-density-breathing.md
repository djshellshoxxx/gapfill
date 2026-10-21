# F-M07 Density Breathing (completion spec over C-6)

Status: canonical rules are `04-SPEC-CREATIVE.md` C-6 (curves, formula, tests) and the decision-stage design in "Two-stage effective settings" + `02` F0-5. This file closes gaps and replaces the IDs.

## 1 Summary & user-visible behaviour
Density slowly swells and recedes over a 1/2/4/8-bar cycle locked to host PPQ, so a section does not feel like constant fill activity. Only the fill *dice* change: at depth 100% density swings +/-0.5 around the knob value, shifting the probability factor `(0.4 + 0.6*density)` between about 0.49 and 1.0 at the default 65% density. Fills that already started, lengths, algorithms, protection and dry audio are unaffected. Off or depth 0 or no usable transport = exactly today's behaviour. Gaps in C-6 closed:
1. Anchor and limits. The cycle is anchored at PPQ 0 with the *current* meter (as C-6), so after a mid-song meter change the cycle no longer lines up with bars; this is deterministic and documented, not a bug. Negative PPQ (pre-roll) uses `p - floor(p)`.
2. Loop continuity holds only when the loop length is a whole multiple of the cycle (`bars * beatsPerBar` beats). Otherwise the curve jumps at the wrap by design; stateless, so no stale state. (C-6's `loop_wrap_continuity` test is restricted accordingly.)
3. Offset. Add `fm07_offset` (whole bars, 0-7) so a user whose section starts mid-cycle can align the "in-breath" to it: `phase = frac ((ppq/beatsPerBar - offsetBars) / cycleBars)`; offset >= cycle length wraps modulo.
4. Time signature must be valid: `tsNum` in 1..32 and `tsDen` in {1,2,4,8,16,32}; otherwise neutral and the UI says why. No silent 4/4 assumption (10 section 4.4 rule). Hosts differ: JUCE `AudioPlayHead::PositionInfo::getTimeSignature()` is optional and many hosts and all standalone use give none ([JUCE PositionInfo docs](https://docs.juce.com/master/classAudioPlayHead_1_1PositionInfo.html)).
5. Order with siblings: Evolve -> Breathing -> Phrase, each clamping `density01` 0..1 after its step.
6. Live feedback is computed per block by the same pure function used at decision time, so the LED animates even when gaps are rare.

## 2 Success / quality metric
- Neutrality: golden A-F bit-identical with defaults and with depth = 0.
- Effect size: on the drum loop with 4-bar sine, depth 100, 60 s of fills, the per-bar fill count has a peak-to-trough ratio >= 2.0 and its dominant period (autocorrelation) equals the 4-bar cycle.
- Determinism: same PPQ -> same offset (to float exactness) regardless of block size 32..4096; decisions identical across block sizes.
- User: in a 3 min test arrangement, 4 of 5 listeners report "varying activity" with breathing on vs flat when blind (manual).

## 3 Design
- Files: `Source/features/breathing/{Breathing.h,Breathing.cpp,Gui.h,Gui.cpp,Help.cpp}`, `Tests/features/TestBreathing.cpp`, `Source/params/fm07.{h,cpp}`, `tools/param_ids/fm07.lock`, cmake fragments.
  ```cpp
  struct BreathSettings { bool on=false; int bars=4; float depth01=0.f; int curve=0; int offsetBars=0; };
  float breathCurve (int curve, float phase01) noexcept;                       // [-1,1]
  bool  breathPhase (const BreathSettings&, double ppq, const TransportInfo&, float& phase01) noexcept;  // false => neutral
  float breathingOffset (const BreathSettings&, double ppq, const TransportInfo&) noexcept;              // 0 if neutral; = 0.5*depth*curve
  void  applyBreathing (EngineSettings& sLocal, double ppq, const TransportInfo&) noexcept;              // density01 += offset, clamp
  ```
  Curves exactly as C-6 (Sine, RiseFall triangle, Pulse). `applyBreathing` is called from `Effective::applyAtDecision` (F0-5) with the PPQ of the decision sample; it returns immediately when `!on || depth01 <= 0`.
- Meter for UI: in `processChunk`, after `updateTempoInfo`, `meters.breathValue.store (breathingOffset (...)*2/ max(depth,eps))` (normalised curve value) and `meters.breathValid`; two relaxed atomics, once per chunk. `TransportInfo` extends `updateTempoInfo` to read `getTimeSignature()` (F0-5).
- No RNG, no salt. No change to the decision key.

## 4 Parameters & state
Replaces unreleased `densityBreath*` IDs of the 02 registry (BREATHING group slot, same position). Group file `Source/params/fm07.{h,cpp}`, lock `tools/param_ids/fm07.lock`, appended only, version hint 1:

| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fm07_enable` | bool | off | n/a |
| `fm07_bars` | choice: 1, 2, 4, 8 | 4 | bars |
| `fm07_depth` | float 0-100, step 1 | 0 | % |
| `fm07_curve` | choice: Sine, RiseFall, Pulse | Sine | n/a |
| `fm07_offset` | int 0-7 | 0 | bars |

- Not randomised (not in allowlists). State: parameters only. Old sessions: absent -> off/0 -> identical. Validator regex change as in F-M05 (D1).
- Neutral proof: `breath_neutral_when_disabled_depth0_or_no_transport` on `memcmp` of `sLocal` vs input, plus golden.

## 5 Real-time safety
Decision stage: a handful of float ops and at most one `sin` per evaluated gap. Block stage: one pure call and two atomic stores per chunk (also one `sin`). No allocation/locks/RNG; no state, so nothing to reset on `reset()`, bypass, sample-rate change or transport jump (stateless in PPQ). Garbage PPQ (NaN/Inf/huge) -> `breathPhase` returns false (`std::isfinite` and `abs(ppq) < 1e9`).

## 6 UI
- Placement: small "Breathing" group in the Creative tab Advanced section (`CreativeExtra`, Advanced only per F-M04): checkbox, bars combo, depth knob, curve combo, offset spinner; a 48x16 px curve preview with a moving cursor (the LED) shown only when valid.
- Status text next to the group when enabled but inactive: "Breathing paused - host gives no tempo or time signature" / "Breathing paused - transport stopped". No green check marks (docs/dev/11 rule 6).
- Tooltips: enable "Slowly raises and lowers how often fills happen, in time with your project." Bars "Length of one breath." Depth "How far density swings. 0 = off." Curve "Shape of the swing." Offset "Shift the cycle by whole bars so the swell starts where your section starts."
- Help (order 1600): includes the meter-change and loop limitations in plain words.
- Accessibility: all controls named; cursor is decorative; the status text is a live label. Keyboard under the preference. Reduced Motion: cursor jumps in 250 ms steps instead of smooth motion.
- Narrow: curve preview hidden below 1100 px; status text kept.

## 7 MIDI / automation / CLAP / host
Automatable and MIDI-learnable; changes affect the next decision. Needs host PPQ + time signature: works in REAPER, Ableton, Bitwig, FL (verify per host in E-7 script; some hosts omit time signature in some contexts); standalone has no playhead so it stays neutral and says so. Loop and seek behave statelessly. CLAP: plain parameters. No latency impact.

## 8 Tests (`Tests/features/TestBreathing.cpp`)
C-6 list plus: `negative_ppq_phase_wraps_correctly` | `offset_bars_shifts_phase_and_wraps` | `invalid_time_signature_is_neutral` (0/0, 4/3, 40/4) | `loop_continuity_only_for_whole_cycle_loops` | `meter_change_does_not_crash_or_nan` | `nan_inf_ppq_neutral` | `ui_value_matches_decision_value` | `evolve_breath_phrase_order_clamped` | `ids_locked_old_state_off` | `no_audio_thread_allocation` | `fill_count_swing_on_drum_loop` (metric in section 2). Fixtures: 4/4, 6/8, 7/4, 3/4 transports from `TestRig` with explicit `TransportInfo`.

## 9 Dependencies & build order
F0-1, F0-5 (decision stage + time signature). Related: F-L08 phrase (shares `TransportInfo`), F-L16 Evolve (earlier in order). Independent of F-M01..F-M06; build after F0-5 and the E-2 sync audit is accepted (shared PPQ expression).

## 10 Effort & risks
Effort M. Risks: host time-signature reliability (graceful neutral); user expectation of bar alignment after meter change (documented); interplay with Phrase/Evolve double-clamping (order fixed); the decision key uses PPQ ticks at 96/beat so fills near cycle edges stay stable.

## 11 Acceptance checklist
- [ ] Off/depth 0/invalid transport bit-identical; golden unchanged
- [ ] Curve, phase, offset tests green incl. 6/8, 7/4, negative PPQ
- [ ] Block-size invariant decisions (32..4096)
- [ ] Status text explains every inactive case
- [ ] `fm07_*` IDs locked; old fixtures load; not randomised
- [ ] Verified in REAPER + one more host (loop on/off)
- [ ] Help and tooltips registered; min-size layout reviewed
