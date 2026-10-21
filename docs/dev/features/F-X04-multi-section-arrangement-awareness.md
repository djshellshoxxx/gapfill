# F-X04 Multi-section arrangement awareness

Status: specified (this pass), not implemented. Size XL, split X04a section tracker, X04b decision modulators, X04c UI. Depends on **F-L22** (frame features, energy class, hysteresis), F0-5 (`applyAtDecision`), Gate B. Feeds F-M03 timeline and F-M21 curves. Complements C-6 Breathing and C-7 Phrase (they are periodic in bars; this follows the actual song structure).

## 1. Summary and user-visible behaviour
GapFill learns the structure of what it hears (intro, verse, chorus, breakdown) from the audio itself and lets the fill behaviour follow it:
* **Calm**: in sections that are quiet relative to the rest of the song (breakdowns), fills thin out so the break is respected.
* **Lift**: just before an expected section change, and during a build, fills become more likely and may run longer, marking the transition.
* **Settle**: right after a new section starts, fills hold back briefly so the downbeat lands cleanly.
It starts in a visible **Learning** state (neutral) and only acts when it has enough evidence. Off by default. It never forces a fill into a protected or ineligible gap, never changes dry audio, and never touches the legacy RNG.

## 2. Success / quality metric
| # | Metric | Target |
|---|---|---|
| M1 | Boundary detection on 10 synthetic arrangements (sections 8-32 s, 3-6 sections, differing density/level/spectrum): precision and recall with a +-2 s tolerance | both >= 0.8 |
| M2 | Stationary 8-bar drum loop for 5 minutes | <= 1 false boundary per minute |
| M3 | Energy class ordering on the synthetic songs | chorus > verse > intro in 100 % of runs |
| M4 | Boundary prediction (second and later boundaries of equal-length sections) | within +-1 frame (0.5 s) in >= 80 % of cases |
| M5 | Calm effect: fills in low-energy sections with `fx04_calm = 100` vs `0`, same seed, 40 trials | >= 40 % fewer, never more |
| M6 | CPU | <= 0.2 % of one core; worst frame update <= 30 us; memory <= 80 KB |

## 3. Design
### Modules (`Source/features/arrangement/`)
`SectionTracker.h/.cpp` (self-similarity, novelty, boundary logic: pure), `ArrangementState.h` (POD snapshot), `Modulators.h/.cpp` (pure decision functions), `Gather.cpp`, `Gui.*`, `Help.cpp`, `Info.cpp`. Frame features come from `Source/features/energy/EnergyTracker.h` (F-L22).

### Frame features (from F-L22, one vector per 0.5 s of audio, sample-count driven)
`f = [loudnessDb (short-term RMS, -60..0 mapped to 0..1), onsetDensity (positive flux events/s, 0..12 mapped to 0..1), lowShare, midShare, highShare (three-band energy shares from a private 2 x one-pole split at 180 Hz and 3.5 kHz), brightness (energy-weighted centroid proxy)]`, each normalised by a running min/max over the last 128 frames (with a floor so silence does not stretch the range). Frames are cut every `round(0.5 * sr)` samples regardless of block size, so features are bit-identical for any block size.

### Self-similarity and novelty (Foote 2000)
* Ring of the last `F = 128` frames (64 s). Similarity `S(i,j) = 1 - ||f_i - f_j||_2 / sqrt(6)` clamped to [0,1], stored in a `float S[128][128]` ring-indexed (64 KB). Each new frame computes one new row and column: `128 * 6` MACs.
* Gaussian-tapered checkerboard kernel half-width `L = 4` frames (2 s): `K(a,b) = sgn(a) sgn(b) exp(-(a^2 + b^2) / (2 (L/2)^2))` for `a,b in [-L+1, L]` excluding 0 handled by using half-integer offsets. Novelty `n(t) = sum_{a,b} K(a,b) S(t+a, t+b)` evaluated at `t = newest - L` (a delay of 2 s, which is inherent).
* Peak pick on `n` (3-frame moving average): local maximum over +-2 frames, `n > mean(n[-64..]) + k * std(n[-64..])`, `k = 1.5 - fx04_sensitivity/100` (0.5-1.5), refractory distance `max(12 frames, 2 bars if tempo known)` (6 s).
* On a boundary: `sectionIndex++`, `sectionStartFrame = t`, section feature mean restarts. Boundary time is reported as `t` (the detection lags by 2 s; the lag is applied when predicting).

### Section energy and trend
`sectionEnergy01` = percentile rank of the current section's mean (loudness + onset density) among all section means seen in the ring (needs >= 2 completed sections else uses the F-L22 class). `trend` = least-squares slope of the loudness over the last 16 frames, scaled to [-1, 1] (build > 0, drop < 0). Hysteresis from F-L22 (class changes need 2 consecutive frames beyond +-0.05 of the threshold).

### Prediction
Keep the lengths of the last three completed sections. If the last two agree within +-1 frame, `expectedBoundaryIn = len - framesIntoSection` (frames, may be negative = overdue, clamped to 0). If transport and time signature are valid, snap the predicted boundary time to the nearest bar line (`TransportInfo` from F0-5) and prefer multiples of `phraseBars` if C-7 is set. Else unknown (-1).

### State published to the scheduler
```cpp
struct ArrangementState {                 // POD, written by audio thread once per frame, read at decision time
    std::uint32_t sectionIndex; std::uint16_t framesInto; std::int16_t expectedIn;   // frames, -1 unknown
    std::uint8_t energy01_q8, trend_q8, confidence_q8, learning;                     // quantised 0..255 (trend signed)
};
```
Stored in a seqlock-free pair of `std::atomic<std::uint64_t>` (two packed words with a generation check) or simply written/read on the audio thread (the scheduler and tracker both run there); only the UI reads it cross-thread via a relaxed copy, so a torn read costs a wrong label for one tick, never audio.
**Confidence** = `clamp((framesObserved - 16)/48, 0, 1) * clamp(noveltyContrast, 0, 1)` where `noveltyContrast = (meanPeak - meanTrough)/ (stdN * 3)`. Modulators scale their strength by `confidence`; `learning = framesObserved < 16`.

### Modulators (decision stage, after Breathing and Phrase)
`applyArrangement (EngineSettings& sLocal, const ArrangementState&, const Fx04Settings&, const TransportInfo&)`; identity when `!on`, `learning`, or `confidence < 0.3` (memcmp test):
* `calmFactor = 1 - calm01 * 0.8 * smoothstep(0.35, 0.0, energy01) * confidence`
* `settle = 1 - settle01 * (1 - smoothstep(0, settleFrames, framesInto))`, `settleFrames = 2 bars if tempo known else 4 frames`
* `lift = lift01 * confidence * max(proximity, buildTerm)`, `proximity = smoothstep(4 bars or 8 frames, 0, expectedIn)`, `buildTerm = max(0, trend) * 0.5`
* `density01 = clamp(density01 * calmFactor * settle + 0.5 * lift, 0, 1)`; `maxFillMs *= 1 + 0.5 * lift` (clamped to the legal max-fill range; the usable gap still bounds the real length).
No RNG draws; every factor is a pure function of the state and settings.

### Hooks
One call at the end of `Effective::applyAtDecision`; one `tracker.push (monoMain, n)` per block in `processChunk` beside the other analysis pushes; `noteTransportJump()` also calls `tracker.reset()` (history is no longer contiguous) and the state shows **Learning** again.

## 4. Parameters and state
Group `fx04`, file `Source/params/fx04.h`, lock `tools/param_ids/fx04.lock`, appended to `GAPFILL_PARAMS_EXT`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fx04_on` | bool | off | - |
| `fx04_calm` | float 0-100 | 50 | % |
| `fx04_lift` | float 0-100 | 50 | % |
| `fx04_settle` | float 0-100 | 30 | % |
| `fx04_sensitivity` | float 0-100 | 50 | % |
* No subtree; learned structure is runtime only (it is derived from audio, never serialised). `stateVersion` unchanged; old sessions load with `fx04_on` off.
* Neutral proof: with `fx04_on = off`, `push` returns immediately (zero cost), `applyArrangement` is the identity (`memcmp`), goldens A-F unchanged, no RNG interaction.
* `fx04_on` is in `performanceParameterIDs()`. Reproducibility note in Help: the result depends on what has played; after a seek or loop restart it relearns (visible as Learning).

## 5. Real-time safety
Fixed memory (SSM 64 KB, features 4 KB, novelty ring 512 B), allocated in `prepare`. Per-frame work is bounded (128 distance rows, 64-term kernel). Per-sample work is 3 one-pole filters and an accumulator. No locks or allocation. Resets: `reset()`, `releaseResources`, sample-rate change (frame length recomputed in `prepare`), bypass (stop feeding, reset), transport jump (reset, show Learning), Capture hold (tracker follows the live input).

## 6. UI
Placement per docs/dev/11: Creative tab, Advanced view, foldable **Arrangement** card in the effects group (`CreativeExtra`); the section markers and energy bar appear as an optional layer in the expandable visualiser detail (F-M03), never as a permanent meter. Card: on/off, Calm/Lift/Settle/Sensitivity knobs, readout "Learning (14 s of 8 needed)" or "Section 3 - high energy - next change in ~4 bars" (text; icon shapes differ per energy class: bar height 1/2/3 so colour is not the only cue). Footer status shows the same line only when hovered/focused. Reduced Motion: no scrolling animation, markers appear instantly. Accessible names per control; Help section "Arrangement" order 1730.

## 7. MIDI / automation / CLAP / host
Parameters automatable (bool and floats). Needs optional host tempo/time signature only for bar snapping; works without transport using time-based frames. Looping hosts: relearn after the loop point (because of `noteTransportJump`). No latency, no MIDI.

## 8. Tests (`Tests/features/TestArrangement.cpp`, group `Arrangement`)
Synthetic song builder in `Tests/support` (`makeArrangement (sections[])`: intro hats, verse kick+hat, chorus kick+snare+bass+pad, breakdown pad only) | `frame_features_block_size_invariant` | `similarity_matrix_symmetric_and_bounded` | `kernel_zero_mean` | `novelty_peaks_at_known_boundaries` | `boundary_precision_recall_10_songs` (M1) | `stationary_loop_no_false_boundaries` (M2) | `energy_class_ordering` (M3) | `prediction_equal_length_sections` (M4) | `no_prediction_for_irregular_lengths` | `bar_snapping_with_valid_transport` | `learning_state_is_neutral_identity` | `modulators_identity_when_off_or_low_confidence` (memcmp) | `calm_reduces_fills_in_low_sections` (M5) | `settle_holds_back_after_boundary` | `lift_raises_density_near_expected_boundary` | `density_always_clamped_0_1` | `transport_jump_resets_and_relearns` | `silence_no_nan_no_boundary` | `sample_rate_44_48_96_192` | `worst_frame_cost_reported` | `no_audio_thread_allocation` | `off_is_bit_identical_golden_A_to_F` | `old_state_loads_off`. Golden `X4` (synthetic arrangement, all three modulators at 100) added in a `[golden]` PR.

## 9. Dependencies and build order
F-L22 (frame features + class) -> X04a tracker (pure, testable without hooks) -> X04b modulators + hook -> X04c UI. Needs F0-5 `applyAtDecision` and TransportInfo with time signature (C-6/C-7 add the read). F-M03/F-M21 consume the state later.

## 10. Effort and risks
~9 ideal days (tracker 3, features/energy shared with F-L22 counted there, modulators 1.5, UI 1.5, tests 3). Risks: (a) generic features cannot separate sections that differ only in harmony or arrangement of a single instrument: expect misses, hence the confidence gate and the Learning state; (b) a 2-second detection delay is inherent to the kernel; prediction and `Settle` are designed around it; (c) users on a single drum bus see little structure: the card says "Not enough change to find sections" when contrast is low; (d) behaviour depends on history: documented, and Hold-style reproducibility is not offered (stretch: `Freeze structure` that snapshots section boundaries into the project). Stretch: chroma-based harmony novelty (reuses F-X03 front end), repeat recognition ("chorus returned"), per-section fill personality, structure export for F-M21 curves.

## 11. Acceptance checklist
- [ ] Off: identity (memcmp) and goldens A-F unchanged; zero cost when off
- [ ] M1-M6 recorded in `docs/progress/F-X04.md`
- [ ] Block-size invariance, transport-jump relearn, loop handling tested
- [ ] AllocGuard test; sample-rate sweep
- [ ] Learning state visible and neutral
- [ ] UI checklist (tooltips, names, keyboard, Reduced Motion, min size, Help)
- [ ] Unverified: real full-mix songs from two genres (listening notes) and a real DAW loop region
