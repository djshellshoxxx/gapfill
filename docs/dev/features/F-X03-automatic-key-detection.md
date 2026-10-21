# F-X03 Automatic key and pitch-class detection

Status: specified (this pass), not implemented. Size L. Depends on F0-1/5/6, Gate B, and **F-L18** (defines `ScaleMask`, manual root/scale and the key resolver this ticket feeds). Consumers: F-X01 (snap), F-M15 (resonator tuning), F-L18 users generally.

## 1. Summary and user-visible behaviour
GapFill listens to the incoming music and estimates its tonal centre (for example "A minor") with a confidence value. When **Auto key** is on and the estimate is confident, key-aware features use it instead of the manual root and scale from F-L18. The estimate is shown in the Key card and status line ("Key: A minor, 87 %"), or "Key: not detected" for drums, noise, silence or ambiguous music (it never guesses). **Hold** freezes the current estimate and stores it in the project so renders are reproducible. Manual selection always wins when Auto key is off. Nothing changes in the sound unless a key-aware feature is enabled.

## 2. Success / quality metric
| # | Metric | Target |
|---|---|---|
| M1 | 24 synthetic key fixtures x 3 timbres (sine-ish, 6-partial, 6-partial + detuned +30 cents): exact tonic and mode | >= 90 % exact; 100 % within {exact, relative, dominant/subdominant} |
| M2 | False positives: percussion loops, white/pink noise, silence, a single repeated pitch-less click: `valid` fraction of evaluations | < 5 % |
| M3 | Time to lock on a stable-key fixture (default window) | <= 8 s |
| M4 | Stability: stable key + noise bursts, 60 s | <= 1 key change |
| M5 | Modulation tracking: C major -> D major at t = 20 s | new key reported <= window + 3 s after the change |
| M6 | CPU | <= 0.3 % of one core at 48 kHz; worst single-hop cost <= 60 us |

## 3. Design
### Modules (`Source/features/tonal/`)
`ChromaTracker.h/.cpp` (front end, audio thread), `KeyEstimator.h/.cpp` (pure: profiles, correlation, hysteresis), `KeyProfiles.h` (constants), `AutoKey.h/.cpp` (glue + atomic publish), `Gui.*`, `Help.cpp`, `Info.cpp`. `ScaleMask`/`KeyResolver` come from F-L18 (`Source/features/tonal/Scales.h`). Shared `Decimator.h` (also used by F-X01; whichever ticket lands first creates `Source/features/common/Decimator.h`).

### Front end (hop-grid driven, block-size invariant)
1. Mono sum of the main input, pushed from the processor after `history.write` (one line). Not run in bypass; not run when `fx03_auto` is off and no one else needs a key (`setActive`).
2. Decimate by `N = round(sr / 12000)` power of two (4, 8, 16) with a 4th-order Butterworth low-pass at ~5 kHz => analysis rate 10.4-12 kHz.
3. STFT: Hann, 4096 points (bin ~ 2.9 Hz), hop 2048 decimated samples (~170 ms). FFT on the audio thread: 4096-point real FFT at this rate ~ 40-60 us, once per hop (`juce::dsp::FFT` prepared in `prepare`, no allocation).
4. **Peak-based chroma.** Search 65-2000 Hz. Local maxima above 3x the local median magnitude (51-bin median approximated by a running two-pass mean to avoid sorting; document constant). Parabolic interpolation gives `f`. `pc = (round(12 log2(f / 440)) + 9) mod 12` (C = 0). Accumulate `w = mag^0.7` into `chromaFrame[pc]`; tuning offset tracked as the weighted mean cents residual (clamped +-50, one-pole, used to correct `pc` rounding for detuned recordings; reported in debug overlay).
5. **Tonal gating.** `tonalness = sum(peak mag^2) / sum(all mag^2 in range)`. A frame contributes only if `tonalness >= 0.35` and frame energy > -55 dBFS. Else `gate = 0` (drum hits, noise, silence do not move the estimate).
6. **Leaky accumulation.** `lambda = exp(-hop_s / tau)`, `tau = fx03_window` (4-30 s). `C = lambda*C + (1-lambda)*gate*normalise(chromaFrame)`, `M = lambda*M + (1-lambda)*gate` (tonal mass, 0..1).

### Key decision (every 6th frame, ~1 s)
* Krumhansl-Kessler probe-tone profiles (Krumhansl 1990; Krumhansl-Schmuckler key-finding): major `{6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88}`, minor `{6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17}` (tonic first). For each of 24 keys `r_k = pearson(C, rotate(profile, tonic))`. `best`, `second` = top two.
* `confidence = clamp((r_best - r_second)/0.12, 0, 1) * clamp(M/0.4, 0, 1) * clamp(r_best/0.5, 0, 1)`. Relative-key pairs score close, so they correctly produce low confidence unless the evidence favours one.
* `valid = confidence >= fx03_minConf/100` and `r_best >= 0.35`.
* **Hysteresis.** The published key changes only if a new best has led the current key by >= 0.05 correlation for 3 consecutive evaluations. Profiles are a table (`KeyProfiles.h`); Temperley or Albrecht-Shanahan can replace them behind a unit-tested switch without touching callers.
* Output: `struct KeyEstimate { std::int8_t tonic; std::uint8_t mode, confidence; bool valid; }` packed in one `std::atomic<std::uint32_t>` (written once per evaluation by the audio thread, read by UI and by the scheduler/Gather). `Hold` copies the current valid estimate into `heldKey` and `KeyResolver` then returns it regardless of later evidence.
* Key to scale: major -> Ionian mask of the tonic, minor -> natural minor mask (F-L18 defines both; modes beyond major/minor are out of scope).

### Resolver contract (provided by F-L18, extended here)
`KeyResolver::resolve (const KeySettings&, KeyEstimate, bool hold) -> { int rootPc (or -1 = none); ScaleMask mask; enum Source { None, Manual, Auto, Held } }`. Rules: `fx03_auto = false` -> Manual (or None if scale = Off); `fx03_auto = true` -> Held if hold and held valid; Auto if `valid`; else falls back to Manual, else None. Pure, unit-tested.

## 4. Parameters and state
Group `fx03`, file `Source/params/fx03.h`, lock `tools/param_ids/fx03.lock`, appended to `GAPFILL_PARAMS_EXT`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fx03_auto` | bool | off | - |
| `fx03_window` | float 4-30 | 12 | s |
| `fx03_minConf` | float 0-100 | 40 | % |
| `fx03_hold` | bool | off | - |
* State subtree `<Key>` (owner F-X03, <= 64 bytes): `tonic` 0-11, `mode` 0/1, `conf` 0-100, `valid`. Written from the atomic estimate by the processor's 15 Hz timer; applied on load as a **warm start** (initial `heldKey`/initial estimate) which the detector replaces as soon as `M >= 0.2` evidence exists, unless `fx03_hold` is on (then it is authoritative). Validate ranges; bad subtree is dropped and reset (F0-6 rule).
* Neutral proof: with `fx03_auto = off` the tracker is inactive (no FFT, zero CPU) and `KeyResolver` returns the manual key. No RNG use at all. Goldens A-F unchanged because no existing code reads the key.
* Reproducibility statement (Help text): Auto key adapts to the music and therefore depends on what has played; enable **Hold** when you need identical renders after seeking or from a mid-song start.

## 5. Real-time safety
All state fixed-size (rings 4096 floats, `C[12]`, `chromaFrame[12]`, 24 correlations). One FFT per hop. No allocation, locks, or logging. Resets: `reset()` / `releaseResources` clear accumulators (estimate invalid); sample-rate change re-prepares the decimator; bypass stops pushing and clears; transport jump does nothing (signal-driven); Capture hold: tracker follows the live input (not the latched history). Denormals: leaky accumulators are floored at 1e-12.

## 6. UI
Key card in the Creative tab Advanced area (shared with F-L18 manual root/scale), slot `CreativeExtra`: segmented **Manual / Auto**, 12-key note strip showing the active scale (filled = in scale, ring = root, text label below so colour is not the only cue), readout "Auto: A minor, 87 %" / "Auto: not detected - using manual C major". Hold toggle ("Lock key"), window and min-confidence under a disclosure. Footer status shows the key only while hovered/focused (priority below learn/export/preview). Accessible: strip announces "A minor, confidence 87 percent, automatic". Reduced Motion: no animation on the strip (it never animates). Help section "Key detection" order 1720: what it hears, why drums give no key, Hold and reproducibility.

## 7. MIDI / automation / CLAP / host
`fx03_auto`, `fx03_hold` automatable; MIDI-learnable booleans. No MIDI input needed. No latency. The estimate is not a host parameter. CLAP/AU/VST3 identical.

## 8. Tests (`Tests/features/TestKey.cpp`, group `Key`)
`profiles_sum_and_rotation_correct` | `pearson_known_values` | `chroma_pure_tones_land_in_right_pitch_class` (A4 -> pc 9, 12 notes) | `chroma_octave_folding` | `chroma_detuned_30_cents_still_correct` | `tonal_gate_rejects_noise_and_drums` | `key_24_keys_x_3_timbres_accuracy` (M1; fixtures synthesised from K-K duration weights, 25 s) | `percussion_noise_silence_invalid` (M2) | `time_to_lock` (M3) | `hysteresis_no_flipflop` (M4) | `modulation_tracks_after_window` (M5) | `relative_key_low_confidence_when_ambiguous` | `hold_freezes_and_roundtrips` | `warm_start_replaced_by_evidence` | `resolver_precedence_table` (manual/auto/held/none) | `block_size_invariance_estimates_identical` | `sample_rate_44_48_96_192` | `worst_hop_cost_under_60us` (reported) | `inactive_has_zero_cost_and_no_state_change` | `no_audio_thread_allocation` | `state_roundtrip_and_bad_subtree_dropped` | `old_state_fixture_loads_auto_off`. Golden/fixture assets are synthesised by `Tests/support`, no audio files.

## 9. Dependencies and build order
F-L18 first (scale types, resolver, manual UI), then F-X03. `Decimator.h` shared with F-X01. Consumers (F-X01 snap, F-M15) read `KeyResolver` only.

## 10. Effort and risks
~5 ideal days (front end 1.5, estimator + resolver 1, UI/state 1, tests 1.5). Risks: (a) most GapFill sources are drum buses with little pitch: result is often "not detected", which is the correct honest behaviour; (b) detuned or microtonal material: tuning tracker bounds error to +-50 cents but cannot fix non-12-TET; (c) modal/atonal music has no true key: confidence stays low. Stretch: Temperley profile option, tuning-reference output (A = 438-445), key-change detection feeding F-X04, bass-weighted chroma, chord labelling.

## 11. Acceptance checklist
- [ ] M1-M6 recorded in `docs/progress/F-X03.md`
- [ ] Inactive means zero cost; neutral goldens unchanged
- [ ] Resolver precedence table tested
- [ ] `<Key>` subtree validated, size-capped, old states load
- [ ] Block-size invariance; AllocGuard; 4 sample rates
- [ ] UI checklist and Help text (including reproducibility note)
- [ ] Unverified: real polyphonic material from a user's library (manual listening notes)
