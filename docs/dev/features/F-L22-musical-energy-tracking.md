# F-L22 Musical energy tracking

Status: specified, not approved. Size M. Category MUS/VAL, value H.
Overlap: `04` C-6 (Density Breathing) varies density on a fixed periodic curve and C-7 (Phrase) on bar position; neither listens to the music. F-X04 (multi-section arrangement awareness, XL) is the long-term, structure-aware version; this ticket is the small, predictable first step and must not grow into section detection. Also uses the algorithm-family table from `04` C-11.

## 1 Summary and user-visible behaviour
GapFill measures how energetic the incoming music is (loudness relative to the song's own range, plus how busy the onsets are) and classifies it as Low, Mid or High with hysteresis and a minimum dwell time so it never flaps. Mode Match: more and more rhythmic fills when the music is intense, sparser and more atmospheric fills when it is calm. Mode Contrast: the opposite (fill the quiet, hold back at the peak). Amount scales the effect. A footer indicator shows "Energy: Mid (0.54)". Off by default; does not touch parameters or protection.

## 2 Success / quality metric
- Mode Off or Amount 0: `EngineSettings` identical to baseline (memcmp, as F0-5 requires); no state advance side effects on decisions.
- Hysteresis: for a synthetic input whose energy ramps up and down slowly with 0.04 noise, the number of state changes equals the number of true threshold crossings (+-0), and no state lasts less than the dwell time.
- Block-size invariance: decisions and states are identical for host blocks 32, 64, 512, 4096, 8192 (tracker advances on the 32-sample analysis hop grid).
- Determinism: same audio in gives the same states on every run and platform (float-for-float within 1e-6 on the estimator output).
- Steady-state shift: Match on a 16-bar "quiet 8 bars, loud 8 bars" test loop makes the share of rhythmic fills in the loud half at least 1.5x the quiet half (seeded, 200 loops).

## 3 Design
Files: `Source/features/energy/EnergyTracker.{h,cpp}` (pure, audio thread, no JUCE UI), `EnergyEffect.{h,cpp}` (decision-stage function), `Gui.{h,cpp}` (footer item), `Help.cpp`. Shared `Source/dsp/AlgoFamily.h` (constexpr table from C-11; whichever ticket lands first creates it).

### 3.1 Estimator (per analysis hop, `AnalysisEngine::hopSize = 32` samples)
Inputs from `AnalysisFrame`: `energy` (absolute level, (dB+60)/60), `mainTransient`. Fields are already produced; no analysis change.
1. Level in dB: `L = 60 * energy - 60`. Fast level: one-pole with tau = window/4. Floor/ceiling trackers (song's own range): `floor` falls with tau 0.2 s and rises with tau 60 s; `ceil` rises with tau 0.2 s and falls with tau 60 s; both clamped `ceil - floor >= 12 dB` (so silence or a flat drone does not map to extremes). `loud01 = clamp ((Lfast - floor) / (ceil - floor), 0, 1)`.
2. Onset density: rising crossing of `mainTransient` through 0.5 with 60 ms refractory counts one onset. Onsets per second smoothed with a one-pole of tau = window/2 (counts decay as an exponentially weighted rate `r += (1/h)*(hit) ...` implemented as `r = r*a + hit*(1-a)/h`). `dens01 = clamp (r / 8.0, 0, 1)` (8 onsets/s = busy).
3. `energy01 = 0.6 * loud01 + 0.4 * dens01 + bias`, `bias = fl22_bias / 100` (range +-0.5), clamped 0..1. Output smoothed once more with tau = window/8.
4. Warm-up: output stays Mid (0.5) for the first `window` seconds after `reset()`/prepare, so a cold start changes nothing.
Window `fl22_window` in {2, 4, 8, 16} s. All constants above are `constexpr` and documented in the header.

### 3.2 State machine with hysteresis
States 0 Low, 1 Mid, 2 High. Thresholds (before bias already applied): Low->Mid `>= 0.38`, Mid->Low `< 0.28`, Mid->High `>= 0.72`, High->Mid `< 0.60`. A change is accepted only if `dwellSamples >= max (2 s, window/2)` since the last change; counters tick on the hop grid. Transport jumps do not reset (energy is a property of the audio, not of position); `reset()`/prepare/sample-rate change do. A Hold command freezes the state (runtime only).

### 3.3 Effect (decision stage, after Evolve, before Breathing; see F-L16 precedence)
`EnergyEffect::apply (EngineSettings& sLocal, const EnergySettings&, int state)`:
`level = (mode == Match) ? state : 2 - state` (Contrast flips). Table (at Amount 100): density multiplier `{0.60, 1.00, 1.35}[level]`; algorithm-class multiplier for "atmospheric" (textural + pitch/tape families of C-11) `{1.5, 1.0, 0.7}`, for "active" (rhythmic + destructive) `{0.7, 1.0, 1.5}`. Each multiplier `m' = 1 + (m - 1) * amount01`. `sLocal.density01 = clamp (density01 * m', 0, 1)`; `sLocal.weights[a] *= classMult (a)` when `fl22_style` is on. Weights of 0 stay 0. Only the decision-time copy is changed; eligibility, protection, min gap and seeds are untouched, and RNG draw counts are unchanged (weights/probabilities only).
The tracker runs in `FillEngine::controlUpdate` (hop grid, same place the analysis frame is consumed): one call `energy.update (frame)`; the decision stage reads `energy.state()` at the time of the decision.

### 3.4 UI publication
Once per block, relaxed atomics `energy01`, `state` in `MeterState` (two lines). Footer item shows text only when mode != Off.

## 4 Parameters and state
Group `FL22`, `tools/param_ids/fl22.lock`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fl22_mode` | choice Off, Match, Contrast | Off | |
| `fl22_amount` | float 0..100 | 50 | % |
| `fl22_window` | choice 2 s, 4 s, 8 s, 16 s | 8 s | |
| `fl22_bias` | float -50..+50 | 0 | % (shifts energy up/down) |
| `fl22_style` | bool | on | also bias algorithm classes |
No subtree; tracker state and Hold are runtime only (never serialised). Neutral proof: mode Off => `apply` returns before any write; the tracker may still run for the indicator but must not influence decisions (test: engine output bit-identical with tracker running and mode Off). Not randomised.

## 5 Real-time safety
Tracker: about 20 floats of state, O(1) per hop, no allocation, no logging. The frame already carries a dB-mapped level, so the only per-hop math is multiply-add (one-pole coefficients computed at prepare). NaN guard: non-finite inputs are treated as 0. Apply function: a 15-element loop on a local copy. No locks.

## 6 UI
Slot `FooterStatus` for the indicator; controls in the Creative tab advanced section (slot `CreativeExtra`): Mode segmented (Off/Match/Contrast), Amount, Window combo, Bias, Style toggle, Hold button. Tooltips: Mode "Match: more rhythmic fills when the music is intense. Contrast: more fills when it is calm." Bias "If GapFill judges your track too calm or too busy, nudge it here." Help section order 1220 "Energy" (what is measured: level relative to the song and how many hits per second; it does not know sections or key). Footer text includes level name and value, never colour only. Accessible: indicator is a labelled live region updated only on state change. Reduced motion: no animated bar. Narrow: indicator shortens to "Energy Mid".

## 7 MIDI / automation / CLAP / host
Parameters automatable/learnable (mode switching live is a valid performance gesture). Hold bindable via F-L24. Works with or without transport (no PPQ dependence). Sidechain: tracker follows the main input (documented); sidechain-driven energy is a later option. No latency change.

## 8 Tests (`Tests/features/TestEnergy.cpp`)
`mode_off_identity_memcmp` | `tracker_running_does_not_change_off_output` | `warmup_reports_mid` | `silence_stays_low_and_stable_range_floor_12db` | `ramp_up_down_hysteresis_no_flapping` | `dwell_time_enforced` | `block_size_invariant_states` (32..8192) | `onset_density_counts_known_pattern` | `bias_shifts_thresholds` | `match_vs_contrast_tables` | `amount0_identity` | `weights_zero_stay_zero` | `density_clamped` | `rng_draw_count_unchanged` | `fill_share_shifts_with_energy` (200 seeded loops) | `reset_on_prepare_not_on_transport_jump` | `hold_freezes_state` | `nan_input_safe` | `state_roundtrip_old_session` | `no_audio_thread_allocation`. Fixtures: synthetic quiet/loud loops generated in `TestSignals`, no copyrighted audio.

## 9 Dependencies and order
F0-5 (decision stage), F0-9 (footer slot), C-11 family table (or `AlgoFamily.h` created here), C-8/F-L16 precedence. Independent of F-L17. F-X04 may later replace the estimator behind the same `EnergyTracker` interface.

## 10 Effort and risks
M. Risks: users expect it to know song sections (Help is explicit); adaptation to a quiet intro could classify everything as High relative to a drone (range floor of 12 dB and warm-up mitigate); interaction with Breathing/Phrase multipliers can stack (cap total density multiplier at 1.5 in `apply`, tested).

## 11 Acceptance checklist
- [ ] Off and Amount 0 identical to baseline
- [ ] Hysteresis, dwell and block-size invariance tests pass
- [ ] Fill-share shift verified on seeded loops
- [ ] Indicator and Help describe exactly what is measured
- [ ] UI reviewed (footer slot at min size, keyboard, no colour-only cues)
- [ ] IDs locked; legacy sessions load unchanged
