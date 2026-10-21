# F-L22 Musical energy tracking

Status: specified (this pass). Size M. Depends on F0-5, C-11 family table. Foundation for F-X04 (section awareness) and F-M21 (curves).

## 1. Summary and user-visible behaviour
GapFill estimates how energetic the music is right now relative to the rest of the song (Low / Mid / High) with hysteresis, and can let that steer the fills: busier music gets denser, harder fills; sparse passages get airier, more textural ones. Controls: **Follow density** and **Follow style**, both 0 by default. A small badge shows the current class. Off (0 %) changes nothing.

## 2. Success / quality metric
| # | Metric | Target |
|---|---|---|
| M1 | Synthetic ladder (hats only -> +kick -> +snare/bass -> +pad, 12 s each) | class goes Low -> Mid -> High, once per step |
| M2 | Class flicker on a stationary loop with +-3 dB level noise, 5 min | <= 2 changes |
| M3 | Density follow 100 %: fills per bar in High vs Low sections | >= 1.5x more in High |
| M4 | Style follow 100 %: share of rhythmic-family algorithms in High vs Low | higher in High by >= 20 points |
| M5 | CPU | <= 0.1 % |

## 3. Design (`Source/features/energy/`)
`EnergyTracker.h/.cpp` (pure, sample-count framed), `EnergyModulators.h/.cpp`, `Gather.cpp`, `Gui.*`, `Help.cpp`, `Info.cpp`.
* **Frames**: every `round(0.5 * sr)` input samples (sample-count driven, block-size invariant) compute `loudness` (RMS of the frame, dBFS, floor -60), `onsetRate` (positive spectral-flux peaks per second from a private 3-band split: one-pole at 180 Hz and 3.5 kHz; onset = band-energy increase > 3 dB over the previous frame, summed over bands, capped), `bandShare[3]`, `brightness`. These are the features F-X04 reuses (`FrameFeatures`).
* **Energy index**: `e = 0.6 * norm(loudness) + 0.4 * norm(onsetRate)` where `norm` maps through a slowly adapting range `[lo, hi]` per feature: `lo = min (lo + 0.002*(mean-lo), x)` style tracking with a 64-frame memory and a minimum span (6 dB, 2 onsets/s) so noise cannot stretch the scale. Then one-pole smoothing, `tau = 3 s`.
* **Class with hysteresis**: Low `e < 0.33`, High `e > 0.66`, else Mid; switching to a higher class requires `e > threshold + 0.05` for 2 consecutive frames, to a lower class `e < threshold - 0.05` for 2 frames; minimum dwell 4 s.
* **Modulators** (decision stage, F0-5 `applyAtDecision`, before F-X04):
  * density: `density01 *= lerp (1 - 0.5*f, 1 + 0.3*f, e)` clamped to [0,1], `f = fl22_followDensity/100`.
  * style: multiplies algorithm weights by family: Low -> textural x`(1+0.5 f_s)`, rhythmic x`(1-0.3 f_s)`; High -> rhythmic and destructive x`(1+0.5 f_s)`, textural x`(1-0.3 f_s)`; Mid unchanged; interpolated by `e` continuously (not by class) so there is no audible step. Uses the C-11 family table; zero weights stay zero; draw count unchanged.
* Learning state: first 8 s neutral. Transport jump resets the tracker like F-X04.

## 4. Parameters and state
Group `fl22`, file `Source/params/fl22.h`, lock `tools/param_ids/fl22.lock`, appended to `GAPFILL_PARAMS_EXT`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fl22_followDensity` | float 0-100 | 0 | % |
| `fl22_followStyle` | float 0-100 | 0 | % |
No subtree (runtime-derived). Neutral proof: both 0 -> tracker inactive (no per-sample work), modulators return before touching settings (`memcmp` identity), goldens A-F unchanged. Reproducibility: depends on history, relearns after seeks (stated in Help).

## 5. Real-time safety
~200 bytes of state; per-sample 3 one-poles + accumulators only when active; per-frame O(1). No allocation or locks. Reset table: `reset()`, `releaseResources`, sample-rate change (frame length), bypass (stop+reset), transport jump (reset), Capture (follows live input).

## 6. UI
Two knobs in the Creative Advanced "Energy" card (`CreativeExtra`), badge "Energy: High" (text + bar height, not colour only) visible when either knob > 0. Tooltips from ParamInfo. Accessible names, keyboard, Reduced Motion (no animation), Help "Energy follow" order 1725.

## 7. MIDI / automation / CLAP / host
Both knobs automatable and learnable. No latency. Works without host transport.

## 8. Tests (`Tests/features/TestEnergy.cpp`)
`frames_block_size_invariant` | `ladder_low_mid_high` (M1) | `hysteresis_no_flicker` (M2) | `min_dwell_respected` | `range_floor_prevents_noise_stretch` | `silence_stays_low_no_nan` | `density_follow_effect` (M3) | `style_follow_effect` (M4) | `zero_weights_stay_zero_draw_count_unchanged` | `identity_when_zero` | `transport_jump_resets` | `sample_rate_sweep` | `no_audio_thread_allocation` | `off_golden_A_to_F`.

## 9. Dependencies and build order
C-11 family table (or a local copy until it lands), F0-5. Before F-X04 and F-M21.

## 10. Effort and risks
~3 ideal days. Risk: loudness-based energy mislabels sparse but loud passages; the onset term and relative normalisation reduce it; documented as an estimate. Stretch: user-set energy override (manual class), tempo-weighted onset rate.

## 11. Acceptance checklist
- [ ] M1-M5 recorded in `docs/progress/F-L22.md`
- [ ] Identity when both knobs are 0 (zero CPU)
- [ ] UI checklist; Help text explains it adapts to what has played
