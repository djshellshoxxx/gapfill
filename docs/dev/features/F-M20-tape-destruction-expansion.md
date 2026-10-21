# F-M20 Tape destruction expansion
Status: implementation spec, Gate C candidate (08). Category FX/FUN, value M, size M. Canonical context: `10-SPEC-LIVE-CREATIVE-EFFECTS.md` section 5.3 (controls, bounds, tests). **Exists already and is not changed:** `Algo::TapeStop` (`FillVoice::genTapeStop`: power-curve speed to zero, speed-dependent low-pass, fixed 5.5 Hz wow scaled by Chaos, weight `wTapeStop`). The speed envelope stays that algorithm's own business. This ticket adds a **Tape Damage** post-stage: wow, flutter, unstable speed, saturation, dropouts and splices, applicable to Tape Stop fills (default) or to any fill, with a UI that keeps the two apart (10 s5.3 "explicit UI distinction").

## 1 Summary & user-visible behaviour
- **TAPE DAMAGE** card (Effects section, off by default). One **Amount** macro scales seven sub-controls: Wow (slow pitch drift), Flutter (fast pitch shake), Instability (random speed wander), Saturation (tape drive and high-frequency loss), Dropout Density and Depth (short level and brightness dips), Splice (short skip-back repeats like a bad edit).
- **Target:** Tape Stop fills only (default) or All fills. Tape Stop remains the brake; Tape Damage is the worn tape.
- Everything is seeded per fill, so the same bar gives the same dropouts; no dropout or splice starts in the first 5 ms or last 10 ms of the fill, so nothing lands on a protected hit.
- Adds at most about 5 ms of onset smear (the modulated delay needs headroom) inside the gap; the tail is truncated at the fill's end, never extended. Dry audio is untouched.

## 2 Success/quality metric
- Pitch deviation accuracy (sine test, effect isolated): wow peak deviation `0.8 % x wow x amount` +/- 0.1 %-points, flutter `0.25 %` likewise, instability RMS-bounded to `1.2 % x instab x amount` peak 1.5x; total absolute delay excursion <= 5 ms.
- Event statistics over 1 000 seeded 400 ms fills at density 100: mean dropout count within 10 % of `10/s x T`, none overlapping, none inside the guard zones, capped at 24 (splice 12).
- Click bound: max |x[n]-x[n-1]| <= 1.5 x the undamaged render + 0.05 (extreme settings, all 15 algorithms). No DC: |mean| < 1e-3 over 1 s on a zero-mean input. Peak <= 1.5 for full-scale input.
- Neutral: off, amount 0, or all sub-controls 0 => bit-identical to golden A-I. CPU: on, 5 voices, scenario E: <= +6 %.

## 3 Design
Uses the shared FX seam (F-M14 s3.1; chain order **tape** -> ratchet -> crush -> resonator; tape is first so later stages act on the damaged sound). Module `Source/features/tapedamage/` (`TapeDamage.h/.cpp`, `EventPlan.h/.cpp` pure, `Gui.*`, `Help.cpp`).
```cpp
struct DropEvent   { int start, len; float depth; };               // samples; depth 0..1
struct SpliceEvent { int start, hold, back; };                     // back = skip-back in samples (5..40 ms)
struct Fm20Plan { int numDrops, numSplices; DropEvent drop[24]; SpliceEvent splice[12];
                  float wowHz, flutterHz, wowPitch, flutterPitch, instabPitch; float drive, hfLossHz; float tauC; };
Fm20Plan planTapeDamage (const Fm20Settings&, std::uint64_t fxSeed, double sr, int lengthSamples) noexcept;   // pure, O(36)
class TapeDamageStage { void prepare (double sr, int channels);       // ring 2 x nextPow2 (ceil (0.064 sr)) = 16384 at 192 kHz
                        void start (const Fm20Plan&, const VoiceFxContext&) noexcept;
                        void process (float& l, float& r, int elapsed) noexcept; };
```
**Plan (all from `Rng r; r.seed (hashCombine (fxSeed, kSalt_Fm20))`, fixed draw order: wow phase/Hz, flutter phase/Hz, instability knots, drop events, splice events):** `amt = amount/100`. Pitch depths `Pw = 0.008 * wow * amt`, `Pf = 0.0025 * flutter * amt`, `Pi = 0.012 * instab * amt`; `wowHz = range (0.5, 1.5)`, `flutterHz = range (8, 14)`; delay amplitudes `Aw = Pw / (2 pi wowHz)`, `Af = Pf / (2 pi flutterHz)`, `Ai = Pi / (2 pi 1.0)` (seconds; pitch = d(delay)/dt). `tauC = Aw + Af + Ai + 0.0002` s so the delay never goes negative. Dropouts: rate `10 * density * amt` per second via exponential gaps (`-ln (1-u)/rate`), `len = range (5, 60) ms`, `depth = clamp (dropDepth * amt, 0, 0.95)`, rejected if within the guard zones or closer than 20 ms to the previous event; stop at 24. Splices: rate `5 * splice * amt` per s, `hold = range (20, 120) ms`, `back = range (5, 40) ms`, same rejection, stop at 12; `back + tauC + Aw + Af + Ai < 64 ms` always (ring margin, static_assert on constants).
**Per sample (both channels share the modulation):**
1. `tau = tauC + Aw sin (w_w t + phi_w) + Af sin (w_f t + phi_f) + Ai * walk (t)`, `walk` = linear interpolation of seeded knots every 5 ms in [-1, 1], then 1-pole low-pass 1 Hz. Read `x(tau)` from the ring with 4-point Hermite. Write the incoming sample first.
2. Splice: when inside `[start, start+hold)`: `c = ramp (2 ms)` in/out; `y = (1-c) x(tau) + c x(tau + back)` (linear cross-fade of correlated material; at the end the read jumps forward, i.e. the skipped material is dropped, also cross-faded).
3. Saturation (`sat = fm20_sat/100 * amt`): `d = 1 + 5 sat`; `y = lerp (y, tanh (d y)/tanh (d), min (1, 4 sat))`; then 1-pole HF loss `fc = lerp (18000, 4000, sat)`; a 10 Hz one-pole DC blocker follows only when `sat > 0`.
4. Dropout envelope `e` in [0,1] (raised cosine, ramp `min (len/3, 6 ms)` >= 2 ms): `gain = 1 - depth * e`; extra low-pass `fc = lerp (20000, 1500, depth * e)`.
5. Mix: `out = x + mixEff * (y - x)`, `mixEff = mix * fxSend01 * edge`, `edge` = 3 ms fade-in and the fill's `fadeOut` tail guard. The whole stage is skipped (`start` returns inactive) when `on` is false, `amt == 0`, or all seven sub-controls are 0, or the target does not match the algorithm (`TapeStopOnly` and `algo != TapeStop`).
Interaction: `genTapeStop` keeps its own wow (Chaos-driven read-position wobble); the Tape Damage wow modulates the output delay afterwards, so the two add. Help states this.

## 4 Parameters & state
Group `GAPFILL_PARAMS_FM20(X)` (`Source/params/fm20.h`, `tools/param_ids/fm20.lock`); `_` regex note in F-M13 s4. All in `performanceParameterIDs()`.

| ID | Type | Range | Default | Unit |
|---|---|---|---|---|
| `fm20_on` | bool | - | off | - |
| `fm20_target` | choice | Tape Stop fills, All fills | Tape Stop fills | - |
| `fm20_amount` | float | 0-100 | 50 | % macro |
| `fm20_wow` | float | 0-100 | 50 | % |
| `fm20_flutter` | float | 0-100 | 30 | % |
| `fm20_instab` | float | 0-100 | 30 | % |
| `fm20_sat` | float | 0-100 | 30 | % |
| `fm20_dropDensity` | float | 0-100 | 25 | % |
| `fm20_dropDepth` | float | 0-100 | 60 | % |
| `fm20_splice` | float | 0-100 | 10 | % |
| `fm20_mix` | float | 0-100 | 100 | % |
| `fm20_prob` | float | 0-100 | 100 | % of eligible fills |

No subtree. **Neutral proof:** default `fm20_on` off => zero setup, zero RNG draws, `process` early-out; `old_state_unchanged` and golden D (forced Tape Stop) unchanged. Old sessions: IDs absent, defaults.

## 5 Real-time safety
Ring `2 x 16384` floats per voice (max rate 192 kHz; 5 voices ~ 655 KB) allocated in `prepare()`, cleared in `start()`. Event arrays are inline (`24 + 12` entries); the plan costs O(36) once per fill. Per sample: 1 Hermite read per active tap (two during splices), 4 one-poles, 2 sines (phase accumulators, no `std::sin` table needed; `std::sin` allowed, cheap) and a bounded event-pointer advance. No `Rng` draws in the sample loop (knots precomputed in the plan). Excursion limits are `constexpr` and clamped on read (`tau` in `[1, ringSize-4]`). Resets: all five events end the fill; `start()` rebuilds state; sample-rate change re-prepares.

## 6 UI
Slot: Effects section (11), group "Tape / time", card "TAPE DAMAGE" next to (not inside) the Fill Type control, with the heading line "Adds wear to Tape Stop fills. Tape Stop itself is chosen in Fill Type." Controls: TARGET segment, AMOUNT large knob, a 7-knob row (WOW, FLUTTER, DRIFT, SAT, DROPOUT DENS., DROPOUT DEPTH, SPLICE), MIX, PROB under "more". Sub-controls hidden until the card is enabled. Tooltips: AMOUNT "Scales every wear control at once."; WOW "Slow pitch drift, about 1 Hz."; FLUTTER "Fast pitch shake, 8 to 14 Hz."; DROPOUT "Short dips in level and brightness."; SPLICE "Short skip-back repeats, like a clumsy tape edit." Help "Tape destruction" (order 1200): difference between Tape Stop and Tape Damage, event guard zones, 5 ms onset note. Accessible titles per knob; card header toggle by Space. Reduced motion: no animated reel graphic; a static glyph. Narrow: the seven knobs wrap to two rows only when expanded; collapsed at minimum size.

## 7 MIDI/automation/CLAP/host impacts
12 automatable, MIDI-learnable params; `fm20_amount` is the natural performance control (F-M23 macro target). No latency/PDC change (the 5 ms is inside the fill, not reported). Deterministic offline render for a fixed seed/PPQ. Changes apply to the next fill.

## 8 Tests (`Tests/features/TestTapeDamage.cpp`)
`off_amount0_allzero_bit_identical_golden_A_to_I_and_D` | `target_tape_stop_only_skips_other_algos` | `wow_pitch_deviation_matches` | `flutter_pitch_deviation_and_rate` | `instability_bounded_and_seeded` | `delay_excursion_under_5ms_and_nonnegative` | `plan_deterministic_same_key` | `plan_differs_other_key` | `drop_events_stats_1000_fills` | `events_respect_guard_zones_and_spacing` | `splice_back_within_ring_margin` | `splice_crossfade_click_bound` | `dropout_ramps_min_2ms` | `saturation_symmetric_no_dc` | `extreme_all_100_peak_and_finite_all_algos` | `silence_and_noise_floor` | `nan_inf_recovers` | `engage_release_click_free` | `stereo_modulation_shared` | `sample_rate_change_resets` | `cpu_ceiling` | `no_audio_thread_allocation` | `state_roundtrip_and_old_state`. Golden `FM20-D` (scenario D with damage on) per platform, `[golden]`.

## 9 Dependencies & build order
F0-1/2/3/5/6/7/9 (Gate B); seam from F-M14/F-M16 (build order per 10 s6: ratchet, bitcrush, **tape**, resonator). Soft: F-M21 `fxSend01`, F-M23 target `fm20_amount`. No dependency on E-2.

## 10 Effort & risks
M. Risks: modulated-delay pitch artefacts at high rates (Hermite interpolation chosen; test measures aliasing at flutter 14 Hz); perceived "double wow" with Tape Stop's own wow (documented, additive); 12 parameters is a lot of surface (collapsed by default, Amount is the main control); `FillRecipe` does not record damage events (accepted, same as other FX).

## 11 Acceptance checklist
- [ ] Neutral cases bit-identical; goldens A-I and D unchanged when off
- [ ] Pitch deviation, excursion and event statistics tests green
- [ ] No click at events, engage or release; no DC; peak bound holds at extreme settings
- [ ] Guard zones respected; fill never extended
- [ ] No audio-thread allocation; CPU within ceiling
- [ ] UI distinction from Tape Stop clear; minimum-size screenshot and keyboard review done; Help merged
