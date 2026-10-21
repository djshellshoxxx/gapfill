# F-M16 Rhythmic ratcheting

Status: specified (this pass). Size M. Insert module on the F-FX0 seam. Implements `10` section 5.1.

## 1. Summary and user-visible behaviour
Within a fill, the opening moment of the fill material is repeated as a roll: a grid-aware ratchet that can accelerate or decelerate, with a gate length. Example: a 1/16 roll that speeds up into the next hit. Default off.

## 2. Success / quality metric
Repeat onsets land on the computed grid within 1 sample at tempo 40-300 BPM and all block sizes; the roll never extends past the fill length (hence never into the protected transient); max repeats and min interval respected; accel curve monotonic; deterministic; neutral when Amount = 0; CPU <= 0.3 %.

## 3. Design
`Source/features/rhythmfx/Ratchet.{h,cpp}` (`IInsertModule`, id 1).
* **Seed segment:** the first `S` samples of the voice output (S = first interval, <= 500 ms: `kMaxSliceMs`) are recorded into a preallocated mono-pair buffer (`500 ms x 2 ch` at 192 kHz = 768 KB per voice insert; shared pool of 2 buffers for the 4 voices keeps memory at 1.5 MB; claim/release like `SpectralSmear`).
* **Intervals:** `interval_k = I0 * accelFactor^k`, `I0` = division in samples (`divisionInBeats * samplesPerBeat`) or free ms; `accelFactor = 2^(accel/100 * 0.5)` for accel in [-100,100] (positive accelerates: factor < 1 per step uses reciprocal), `interval_k >= kMinIntervalMs = 8`; `repeats = min (repeatsParam, kMaxRepeats = 16)`; the roll ends when the cumulative time reaches `lengthSamples` or repeats are done. Each repeat plays the seed from sample 0 for `gate * interval_k` samples with 1 ms (or 10 % if shorter) fade in/out; overlapping tails are not summed (the previous repeat is faded by the new onset), so peak level is bounded by the seed level.
* **After the roll:** the remaining fill (if any) plays the voice output as normal, crossfaded 2 ms.
* **No valid transport:** division choice falls back to the free-time ms parameter and sets `MeterState::ratchetFallback` so the UI shows "No tempo - using free time".
* Output level `fm16_level` dB applied to the repeated part.
* `minFillMs = 2 * I0` or 60 ms, whichever is larger; shorter fills skip the module silently.

## 4. Parameters and state
Group `fm16`, file `Source/params/fm16.h`, lock `tools/param_ids/fm16.lock`, appended to `GAPFILL_PARAMS_EXT`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fm16_amount` | float 0-100 | 0 | % chance per fill |
| `fm16_division` | choice: Division list (1/32 .. 1/2, T and D) | 1/16 | - |
| `fm16_free` | float 20-500 | 125 | ms |
| `fm16_repeats` | int 2-16 | 6 | repeats |
| `fm16_accel` | float -100..100 | 0 | % |
| `fm16_gate` | float 10-100 | 70 | % |
| `fm16_level` | float -12..0 | 0 | dB |
No subtree. Old sessions: Amount 0. Neutral proof: Amount 0 -> module never selected, no buffer claimed, goldens identical.

## 5. Real-time safety
Buffers allocated in `prepare` (max 192 kHz); `process` loops are bounded by block size; position math in `int64`; all fades computed from integer sample indices (deterministic, no float accumulation drift). Resets: `reset()` clears the seed; `panic()` fades the roll in 20 ms; sample-rate change re-prepares; bypass via voice kill; transport jump: voices are killed by the engine; Capture: unaffected.

## 6. UI
`FxCard` "Ratchet" (Creative tab, rhythmic group), controls hidden until Amount > 0; Division combo shows the effective ms next to it (reuse E-2 display) and the fallback badge. Tooltips from ParamInfo; accessible names; keyboard; Reduced Motion (static preview). Help "Ratchet" order 1800.

## 7. MIDI / automation / CLAP / host
All parameters automatable; tempo changes mid-fill do not retime a running roll (like E-2 finding 7).

## 8. Tests (`Tests/features/TestRatchet.cpp`)
`onsets_on_grid_tempos_40_to_300` | `onsets_block_size_invariant` | `accel_positive_negative_monotonic` | `min_interval_floor` | `repeats_capped_16` | `never_exceeds_fill_length` | `gate_and_fades_click_free` | `peak_not_above_seed` | `no_transport_fallback_flag` | `short_fill_skips_module` | `seed_buffer_pool_claim_release` | `deterministic_seed_ppq` | `neutral_amount_zero_golden` | plus the FX conformance battery (F-FX0).

## 9. Dependencies and build order
F-FX0, F-S12 interface. First effect in the `10` section 6 order.

## 10. Effort and risks
~3.5 ideal days. Risk: audible clicks from short repeats (fades scaled to interval); memory of the seed buffer (pool of 2).

## 11. Acceptance checklist
- [ ] Grid accuracy and conformance battery green on Linux and Windows
- [ ] Golden `X16` (ratchet on, fixed seed) added via `[golden]` PR
- [ ] UI checklist; Help text
