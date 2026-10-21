# F-M16 Rhythmic ratcheting
Status: implementation spec, Gate C candidate (08). Category FX/LIVE, value M, size M. Canonical context: `10-SPEC-LIVE-CREATIVE-EFFECTS.md` section 5.1 (controls, bounds, tests). **Exists already:** `Algo::Stutter` (slice repeat with geometric length/rate/gain steps, grid division from `stutterDiv`) and the Tape/Delay algorithms. Stutter repeats a slice cut from the capture; it does not re-trigger the fill's own sound on a grid and has no accelerate/decelerate shapes. The ratchet is a **post-stage on any fill** (except already-repeating ones) that re-triggers the fill's opening on grid-locked, accelerating or decelerating subdivisions.

## 1 Summary & user-visible behaviour
- **RATCHET** card (Effects section, off by default). A fill plays its opening hit, then repeats that hit in shorter and shorter (or longer and longer) gated bursts, e.g. 1/8, 1/8, 1/16, 1/16, 1/32, 1/32 ... until the fill ends.
- Hits land on the song's grid (absolute PPQ), wherever in the bar the fill happens to start. The first hit is the live fill sound and is never delayed.
- Shapes: Even, Accelerate, Decelerate, Accel-Decel. **Depth** (1-4) = how many halvings. **Gate** = how much of each interval sounds.
- No tempo from the host: uses **Free time** (ms) and shows the footer note "Ratchet: free time" (visible fallback, 10 s5.1). Never silently skips.
- Not applied to Stutter, Delay or Scramble fills (already repeating); a status tooltip says so.
- Hits are clipped before the next protected hit: no hit starts in the last 4 ms of the fill, and the fill's own fade-out still applies.

## 2 Success/quality metric
- Hit-position exactness: for any tempo in [40, 300] BPM, rates 44.1-192 kHz, any fill start PPQ and any block size 32-8192, every hit start (hits >= 1) is on the absolute grid of the finest unit within 1 sample.
- Block-size and loop invariance: identical output for block sizes {32, 64, 512, 4096, 8192} and for the same fill start PPQ after a loop wrap.
- Bounds: <= 32 hits per fill, <= 4 ms minimum interval, 0 allocations; `on=false` bit-identical to golden A-I. CPU: on, 5 voices, scenario E: <= +3 %.

## 3 Design
Shared seam: F-M14 s3.1 (`VoiceFxContext`, `VoiceFxChain`; chain order tape -> **ratchet** -> crush -> resonator). Per 10 s6 the ratchet is the first FX to build: if it lands first, this PR creates the seam header verbatim. Module `Source/features/ratchet/` (`Ratchet.h/.cpp`, `HitPlan.h/.cpp`, `Gui.*`, `Help.cpp`).

```cpp
// HitPlan.h - pure, no JUCE audio types, fully unit-testable
struct HitPlan { int numHits; int start[32]; int gate[32]; bool gridLocked; bool freeFallback; bool depthClamped; };
enum class RatchetShape : std::uint8_t { Even, Accel, Decel, AccelDecel };
HitPlan planHits (RatchetShape, int depth, double baseBeats /*or <0 free*/, double freeUnitSamples,
                  double samplesPerBeat, bool transportValid, double ppqAtFillStart,
                  int lengthSamples, int fadeOutSamples, double sr, float gate01) noexcept;
class RatchetStage { void prepare (double sr, int ch); void start (const Fm16Setup&, const VoiceFxContext&) noexcept;
                     void process (float& l, float& r, int elapsed) noexcept; };
```
**planHits (integer arithmetic on a unit grid):**
1. Base interval `B` = `baseBeats` (sync, transport valid) else `freeUnitSamples`. While `B` in samples > `0.2 * sr`, halve it (stay in family), set `depthClamped`.
2. Effective depth `D = max d <= depth` with `B / 2^d >= 4 ms`. Finest unit `u = B / 2^D` (beats or samples). Interval list in units: level `l` has length `2^(D-l)` units, `kHitsPerLevel = 2` hits per level; Even: all `2^D`; Accel: levels 0..D (last level repeats); Decel: levels D..0; AccelDecel: Accel then Decel.
3. Positions in units from the absolute grid: `p0 = ppqAtFillStart / u` (sync) or 0 (free). `m = ceil (p0 - 1e-9)`; if `(m - p0) < 0.5 * L[0]` then `m += L[0]`. Boundaries `b_1 = m`, `b_{k+1} = b_k + L[k]` (all integers, so on the grid by construction). Hit `k >= 1` starts at `start[k] = llround ((b_k - p0) * u * spb)` (sync) or `llround ((b_k - p0) * u)` (free); `start[0] = 0` (live). Strictly increasing by >= 4 ms, and `start[k] <= length - max (4 ms, 1 sample)`; stop at 32 hits or when the rule fails (the last hit then sustains to the fill end).
4. `gate[k] = clamp (gate01 * interval_k, 1 ms, 0.5 * length)` samples; the last hit gets `length - start[last]`.
**RatchetStage::process** (per sample, both channels):
- Records the stage input into a preallocated slice buffer while `elapsed < S`, `S = min (start[1], 0.2 s)` (`start[1]` can reach 1.5 x the base interval after snapping; if `numHits == 1` the stage is a no-op).
- Output `y`: hit 0: `x * w(elapsed, gate[0])`; hit `k >= 1`: `slice[elapsed - start[k]] * w(.., gate[k])` while inside its gate and `< S` (a gate longer than `S` is cut at `S`); else 0 (rest). `w` = 1 ms linear attack (or `1/4` of the gate if shorter), raised-cosine release over `min (2 ms, 0.3 * gate)`, so no hit is hard-edged.
- Mix: `out = x + mixEff * (y - x)`, `mixEff = mix * fxSend01`. Level: gated bursts are quieter on average; no make-up gain (users have Mix and the output stage).
- Compatibility table `constexpr bool ratchetApplicable[numAlgos]`: false for Stutter, Delay, Scramble; tested against every `Algo`.
Hooks: `decideRatchet (...)` called from the seam decider (`on`, `prob` sub-RNG `kSalt_Fm16`), `HitPlan` built in `start()` (O(32)); `MeterState::fxFallbackFlags` (atomic, bit0 = ratchet free time) written once per fill, read by `FooterStatus` (F0-9).

## 4 Parameters & state
Group `GAPFILL_PARAMS_FM16(X)` (`Source/params/fm16.h`, `tools/param_ids/fm16.lock`); `_` regex note in F-M13 s4. All in `performanceParameterIDs()`.

| ID | Type | Range | Default | Unit |
|---|---|---|---|---|
| `fm16_on` | bool | - | off | - |
| `fm16_sync` | bool | - | on | - |
| `fm16_div` | choice (`divisionNames()`) | 1/1 ... 1/16. | 1/8 | - |
| `fm16_freeMs` | float, skew centre 60 | 15-200 | 60 | ms |
| `fm16_shape` | choice | Even, Accelerate, Decelerate, Accel-Decel | Accelerate | - |
| `fm16_depth` | int | 1-4 | 2 | halvings |
| `fm16_gate` | float | 10-100 | 70 | % of interval |
| `fm16_mix` | float | 0-100 | 100 | % |
| `fm16_prob` | float | 0-100 | 100 | % of fills |

No subtree. Neutral proof: `fm16_on` off => `decideRatchet` returns zero setup, no RNG draw, stage early-out. Old sessions load defaults, unchanged sound.

## 5 Real-time safety
`planHits` is O(32) integer/double arithmetic run once per fill in `start()` (no allocation; arrays inline). Slice buffer `2 x 38 400 floats` per voice (0.2 s at 192 kHz; 5 voices ~ 1.5 MB) allocated in `prepare()`, written only for the first `S` samples. Per sample: compare against `hitIndex`'s next start (monotone index, no search). Mid-fill transport change cannot move hits (plan is in samples from the fill start), so loop/seek mid-fill never produces a stale or duplicate hit; tempo change mid-fill is ignored until the next fill (as E-2 finding 7). Resets: all five events end the fill; `start()` rebuilds the plan.

## 6 UI
Effects card "RATCHET" (11). Controls: SYNC toggle, DIVISION combo (or FREE ms knob when sync off), SHAPE combo, DEPTH stepper, GATE and MIX knobs. A 120 x 24 px pattern preview draws the hit bars for the current settings at the current tempo (`planHits` with a nominal fill length), static in reduced-motion mode; it carries a text equivalent for screen readers ("6 hits: 1/8 1/8 1/16 1/16 1/32 1/32"). Tooltips: SHAPE "Accelerate speeds the repeats up through the fill; Decelerate slows them."; GATE "How much of each repeat sounds. Lower = choppier."; sync off "Uses the Free time instead of the song tempo." Help "Rhythmic ratchet" (order 1160): grid locking, why Stutter/Delay/Scramble skip, free-time fallback. Footer shows "Ratchet: free time" only when active. Keyboard: Tab order card header, controls; arrows on stepper. Narrow: card collapsed at minimum size; preview hidden below 480 px card width, text description kept.

## 7 MIDI/automation/CLAP/host impacts
9 automatable, MIDI-learnable params. No latency change. Hosts that report tempo but not PPQ (valid BPM, invalid PPQ): treated as `transportValid=false` (free time). Tempo-ramp project: grid positions use the tempo at fill start; the plan is fixed for that fill (documented in Help: "follows tempo from the start of each fill").

## 8 Tests (`Tests/features/TestRatchet.cpp`)
`off_is_bit_identical_golden_A_to_I` | `plan_on_grid_matrix` (BPM x rates x 200 random PPQ starts, <= 1 sample error) | `plan_even_accel_decel_accel_decel_sequences` | `plan_depth_clamped_by_min_interval` | `plan_base_halved_when_over_200ms` | `plan_first_boundary_rule` (>= half interval) | `plan_never_starts_in_last_4ms` | `plan_max_32_hits` | `loop_wrap_same_plan` | `block_size_invariant_output` | `free_time_fallback_flag_set` | `gate_envelope_no_click` | `slice_capture_bounded_0p2s` | `applicable_table_all_algos` | `protection_clip_with_next_event` (voice ends before the next protected hit) | `mix_zero_identity` | `tempo_change_midfill_ignored` | `no_audio_thread_allocation` | `state_roundtrip_and_old_state`. Golden `FM16-A` (accelerate, 1/8, depth 3) per platform, `[golden]`.

## 9 Dependencies & build order
F0-1/2/3/5/6/7/9; E-2 (`lastKnownBpm`, soft). Builds the seam first (10 s6: ratchet, bitcrush, tape, resonator). Consumers: F-M21 (`fxSend01`), F-M23 (`fm16_gate`, `fm16_mix` targets), F-S07 (stutter storm) is a separate momentary effect and does not use this stage.

## 10 Effort & risks
M. Risks: repeats of a long reverse/pitch sweep sound like a stutter of its first 200 ms only (by design, in Help); grid snapping on an extreme tempo ramp; users expecting to ratchet Stutter fills (explained by the skip message); memory for slice buffers (reduce to 0.15 s if the budget in `06` s5 is tight, a constant).

## 11 Acceptance checklist
- [ ] Hit positions within 1 sample of the absolute grid across the matrix
- [ ] Output identical for all tested block sizes and after loop wrap
- [ ] Golden A-I unchanged when off; old states unchanged
- [ ] Free-time fallback visible; no silent skip
- [ ] <= 32 hits, >= 4 ms interval, no allocation, CPU within ceiling
- [ ] UI reviewed at minimum size; preview has a text equivalent; Help merged
- [ ] Seam header, lock file, cmake fragments, progress note done
