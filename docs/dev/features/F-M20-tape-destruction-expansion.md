# F-M20 Tape destruction expansion

Status: specified (this pass). Size L. Insert module on the F-FX0 seam (id 3). Implements `10` section 5.3. The existing Tape Stop algorithm (`genTapeStop`) is unchanged; this module can be layered on any algorithm, including Tape Stop.

## 1. Summary and user-visible behaviour
Controllable tape degradation on the fill: wow, flutter, saturation, dropouts, splices and unstable speed, each with its own amount and a safe overall mix. Default off.

## 2. Success / quality metric
Wow/flutter depth matches settings (measured pitch deviation within 10 %); dropout positions deterministic for a seed; no clicks at dropout/splice edges (>= 1 ms ramps, max-step bound); output DC < 1e-3; bounded gain (<= +3 dB over input at max saturation); CPU <= 0.6 % while active; Amount 0 neutral.

## 3. Design
`Source/features/rhythmfx/TapeDamage.{h,cpp}` (`IInsertModule`).
* **Modulated delay:** per-channel circular buffer (`kMaxDelayMs = 60` -> 11.5k samples at 192 kHz, 90 KB/channel) read through a 4-point Lagrange fractional delay. Read delay `d(t) = d0 + wow(t) + flutter(t) + drift(t) + splice(t)` in samples, `d0 = kMaxDelayMs/2`; output latency is constant `d0` only while active? No: the module delays the fill by `d0` unless compensated. **Latency rule:** the module starts reading at `d = d0` and the first output is zero until the delay line fills; to avoid shifting the fill, `d0 = 4 ms` (center) and the modulation range is bounded to +-3.5 ms; the voice start is therefore delayed by 4 ms (`latencySamples()`, scheduler subtracts it from `usable`).
* **Wow:** sine LFO 0.5-2 Hz (random phase from the sub-RNG), depth in cents: `d_wow = (depth/ (2 pi f)) * (2^(cents/1200) - 1)` converted to samples so the pitch deviation equals the setting.
* **Flutter:** 6-14 Hz filtered noise (seeded `Rng`, one-pole low-passed at 20 Hz, updated per sample), cents depth.
* **Drift (speed instability):** bounded random walk of speed ratio `+-instab * 0.5 %` with 100 ms time constant.
* **Saturation:** `y = tanh (drive * x) / tanh (drive)`, `drive = 1 + 5 * sat`; pre-emphasis one-pole at 3 kHz before and de-emphasis after (tape-like).
* **Dropouts:** Poisson events with rate `dropDensity` (0-10 per second) from the sub-RNG, each length 2-30 ms (exponential), depth `dropDepth` (gain to `1 - depth`), raised-cosine ramps >= 1 ms (ramps never shorter, even when overlapping: events merge).
* **Splices:** at rate `splice` (0-4 per second) the read position jumps by a random +-1..3.5 ms with a 1 ms equal-power crossfade between old and new read tap.
* All random events are pre-generated at `begin()` for the fill length into a preallocated event table (<= 64 events) from `hashCombine (seed, kSalt_Fm20)`, so processing is block-size invariant.
* **Mix and tone:** `fm20_mix`; final DC high-pass 10 Hz; `sanitise`.
* `minFillMs` 40 ms.

## 4. Parameters and state
Group `fm20`, file `Source/params/fm20.h`, lock `tools/param_ids/fm20.lock`, appended to `GAPFILL_PARAMS_EXT`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fm20_amount` | float 0-100 | 0 | % chance per fill |
| `fm20_wow` | float 0-100 (0-40 cents) | 30 | % |
| `fm20_flutter` | float 0-100 (0-15 cents) | 25 | % |
| `fm20_sat` | float 0-100 | 30 | % |
| `fm20_dropDensity` | float 0-100 (0-10 /s) | 20 | % |
| `fm20_dropDepth` | float 0-100 | 60 | % |
| `fm20_splice` | float 0-100 (0-4 /s) | 10 | % |
| `fm20_instab` | float 0-100 | 20 | % |
| `fm20_mix` | float 0-100 | 100 | % |
No subtree; Amount 0 neutral.

## 5. Real-time safety
Fixed buffers allocated in `prepare`; event table fixed; loops per sample bounded; fractional delay indices wrapped with masks (power-of-two buffer sizes). Resets: `reset()` clears delay and events; sample-rate change reallocates; panic fades 20 ms; transport jump kills the voice; denormals flushed in filters.

## 6. UI
`FxCard` "Tape": nine controls grouped as Motion (wow, flutter, instability), Damage (saturation, dropouts, splices), Mix; summary text "Wow 12 cents ...". Accessible names; keyboard; Reduced Motion (no animated tape reels). Help "Tape damage" order 1820.

## 7. MIDI / automation / CLAP / host
Automatable; no latency change at plugin level (the 4 ms is inside the fill and accounted for by the scheduler).

## 8. Tests (`Tests/features/TestTapeDamage.cpp`)
`wow_depth_matches_cents` | `flutter_depth_matches_cents` | `drift_bounded` | `saturation_gain_bounded` | `dropout_positions_deterministic_by_seed` | `dropout_ramps_min_1ms_click_free` | `splice_crossfade_click_free` | `events_block_size_invariant` | `dc_below_threshold` | `extreme_settings_finite_bounded` | `silence_and_noise_floor` | `latency_reported_and_subtracted` | `works_on_tape_stop_algorithm` | `click_free_engage_release` | `neutral_amount_zero_golden` | F-FX0 conformance battery.

## 9. Dependencies and build order
F-FX0, F-M16 (pattern for tests), scheduler support for `latencySamples()` from the seam.

## 10. Effort and risks
~5 ideal days. Risks: fractional-delay artefacts at extreme modulation (bounded range); the 4 ms start delay shortens fills slightly (documented); CPU with 4 voices (insert instances only for voices that use it).

## 11. Acceptance checklist
- [ ] Measured depths within tolerance; click tests green
- [ ] Golden `X20`; conformance green; CPU recorded
- [ ] UI checklist; Help text
