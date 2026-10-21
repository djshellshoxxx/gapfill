# F-M21 Tension / release automation curves
Status: implementation spec, Gate C candidate (08). Category MUS/LIVE, value H, size L. Canonical context: `10-SPEC-LIVE-CREATIVE-EFFECTS.md` s4.4 "Tension/release curves" (small set of editable multi-bar curves for density, intensity, width and effect-send amount, ending at a defined resolution point; deterministic, transport-aligned, cancellable; bounded count and size; versioned state; parameter values stay in range and are smoothed). Related and **not duplicated:** C-6 Density Breathing (a repeating periodic wave on density only), C-7 Phrase Awareness (probability multiplier in the last bar of a phrase), C-4 intensity envelope (shape *within one fill*). This ticket is the user-drawn, multi-bar *arrangement* control that shapes several creative values across a build and its release.

## 1 Summary & user-visible behaviour
- Pick a curve length (2, 4, 8, 16 or 32 bars) and a shape (**Build**, **Build+Drop**, **Breakdown**, **Swell**, or **Custom** drawn by hand). Turn on **TENSION**: over those bars, Density, Intensity, Width and FX Send follow the curve, then the curve **resolves** as you choose: Loop, Hold the last value, or Release (return to your knob values).
- Each lane has a **depth** (0-100 %): 0 means that lane is not touched. The knobs still show your base values; the effective values move (C-0 rings show them).
- A playhead shows where the song is in the curve. Position comes from the host PPQ, so loops, seeks and offline renders land on the same curve value every time.
- Without a valid tempo/time-signature the feature is neutral and the status says "Tension waiting for transport".
- A "Start bar" offset places the build anywhere in the song (before it, nothing changes).

## 2 Success/quality metric
- Determinism: curve value at any PPQ equals an independent oracle to 1e-6, same through seeks, loops and block sizes {32 ... 8192}; decisions identical across block sizes.
- Range safety: every lane output stays in its legal range for any custom curve and any depth (fuzz 100 000 random curves).
- Width smoothness: width trajectory differs between block sizes 64 and 4096 by <= 0.01 (normalised) after the existing 20 ms smoother.
- Neutral: off, depth 0, or no valid transport => `EngineSettings` byte-identical (`memcmp`) and golden A-I unchanged. <= 1 microsecond added per gap decision; zero allocations.

## 3 Design
Module `Source/features/tension/` (`Curves.h/.cpp` pure model, `Tension.h/.cpp` evaluation + state, `Gui.*` editor popover, `Help.cpp`); reuses `Source/features/effective/BarMath.h` (F-M18/C-6).
```cpp
constexpr int kLanes = 4, kMaxPoints = 16;                         // Density, Intensity, Width, FxSend
struct TensionPoint { std::uint16_t x, y; };                       // per-mille of curve length / of lane range, 0..1000
struct TensionLane  { std::uint8_t numPoints = 2, interp = 1;      // interp: 0 linear, 1 smooth (per-segment smoothstep), 2 step
                      TensionPoint pt[kMaxPoints]; };              // x strictly increasing, pt[0].x = 0, pt[n-1].x = 1000
struct TensionCurves { TensionLane lane[kLanes]; };                // 4 x 66 B = 264 B, trivially copyable
struct TensionSettings { bool on; int bars, startBar, shape, end; float depth[kLanes]; };
bool  validate (const TensionCurves&) noexcept;
float evalLane (const TensionLane&, float phase01) noexcept;       // 0..1
float shapeValue (int shape, float phase01) noexcept;              // built-in shapes
struct TensionOut { float c[kLanes]; bool active; float phase; };  // c in [0,1], 0.5 = neutral for the first three lanes
TensionOut tensionAt (double ppq, const TransportInfo&, const TensionSettings&, const TensionCurves&) noexcept;
void applyTension (EngineSettings& sLocal, const TensionOut&, const TensionSettings&) noexcept;   // decision stage
float tensionWidthDelta (const TensionOut&, const TensionSettings&) noexcept;                     // processor, per chunk
```
**Position.** Valid only with `tr.valid`, `tsNum > 0`, `tsDen` a power of two. `barPos = ppq / beatsPerBar` (anchored at PPQ 0, like C-6/C-7, so loop/seek reproduce); `u = barPos - startBar`. `u < 0` => inactive (neutral). `phase = frac (u / bars)` when `end == Loop`; otherwise for `u >= bars`: `Hold` => `phase = 1`, `Release` => inactive. Built-in shapes (`c` in [0,1], 0.5 = no change):

| Shape | c(phase) |
|---|---|
| Build | `phase` (rises 0 -> 1, then loop snaps to 0) |
| Build+Drop | `phase < 0.875 ? smoothstep (0, 0.875, phase) : 0.5` (peak, then instant return to neutral for the last 1/8 of the length: the "drop") |
| Breakdown | `phase < 0.75 ? 1 - phase/0.75 : 0.5 * (phase - 0.75)/0.25` (high to low, then back to neutral) |
| Swell | `sin (pi * phase)` |
| Custom | `evalLane (lane[i], phase)` per lane |
Built-in shapes drive all enabled lanes identically; Custom has one curve per lane (editor writes all four; untouched lanes default to a flat 0.5 line).
**Mapping (centred like C-6, swing +/-0.5 at depth 100 %), decision stage:** `density' = clamp01 (density + 0.5 * dDens * (2 c0 - 1))`; `intensity' = clamp01 (intensity + 0.5 * dInt * (2 c1 - 1))`; `fxSend01 = 1 - dFx * max (0, 1 - 2 c3)` (full effect at/above neutral, reduced below; 1.0 = neutral, consumed by F-M14/15/16/20 as `fxSend01`). Width (processor, per chunk at the chunk-mid PPQ): `widthTarget = clamp (width + 0.5 * dW * (2 c2 - 1), 0, 2)` fed to the existing `widthAmount` smoother (replaces one `setTargetValue` argument; macro-driven width, C-2, is applied first and Tension adds its delta). Lane with depth 0 or inactive output never writes.
**Order in `applyAtDecision`:** Battle (F-M18) -> Evolve (C-8) -> Breathing (C-6) -> **Tension** -> Phrase (C-7, probability multiplier last). Each stage clamps, so combined swings cannot leave [0, 1].
**Editor model.** `TensionCurves` is edited on the message thread and published to the audio thread through a single-writer seqlock snapshot (264 B; same `SeqSnapshot<T>` as F-M18, created by whichever lands first). Edit invariants enforced by `normalise()` after every edit: first/last point pinned at x = 0 / 1000, x strictly increasing with >= 8 per-mille spacing, y clamped, <= 16 points. The audio thread publishes `lastPhase`, `lastActive` (relaxed atomics) for the playhead.
Hooks: `EngineSettings::perf.tension` (neutral), 1 line in `applyAtDecision`, 1 line in `processChunk` (`tensionWidthDelta`), `gatherTension()` in `FeatureHooks.h`, subtree in `StateSubtrees`.

## 4 Parameters & state
Group `GAPFILL_PARAMS_FM21(X)` (`Source/params/fm21.h`, `tools/param_ids/fm21.lock`); `_` regex note in F-M13 s4. All in `performanceParameterIDs()`.

| ID | Type | Range | Default | Unit |
|---|---|---|---|---|
| `fm21_on` | bool | - | off | - |
| `fm21_bars` | choice | 2, 4, 8, 16, 32 | 8 | bars |
| `fm21_start` | int | 0-255 | 0 | bar index (0-based) |
| `fm21_shape` | choice | Build, Build+Drop, Breakdown, Swell, Custom | Build+Drop | - |
| `fm21_end` | choice | Loop, Hold, Release | Loop | - |
| `fm21_depthDensity` | float | 0-100 | 40 | % |
| `fm21_depthIntensity` | float | 0-100 | 30 | % |
| `fm21_depthWidth` | float | 0-100 | 0 | % |
| `fm21_depthFx` | float | 0-100 | 0 | % |

Subtree `<Tension version="1">` (<= 2 KiB): `<Lane i="0" interp="1" pts="0:500,1000:500"/>` x4. Validation (atomic per subtree, F0-6): lane index 0-3 once each, 2-16 points, `x` strictly increasing from 0 to 1000, `y <= 1000`, `interp <= 2`; any violation drops the subtree to flat defaults and the rest of state loads. Not stored in `.gapfill` parameter files (same limit as macros; Help says so). **Neutral proof:** `fm21_on` default off; all four `applyTension` writes are guarded by `active && depth > 0`; `memcmp` test. Old sessions: no IDs, no subtree => off.

## 5 Real-time safety
Decision stage: one `floor`/`frac`, up to 4 lane evaluations of <= 16-point piecewise functions (bounded search over <= 15 segments), no allocation. Block/chunk stage: one evaluation for width. Curve snapshot read via bounded seqlock (<= 2 attempts, else last good copy). No `Rng` use. Reset table (06 s1.6): `reset`, `releaseResources`, sample-rate change, bypass, transport jump: stateless apart from published atomics (recomputed at the next decision), so nothing to clear. Dropped-update counter not needed (state is a snapshot, not a queue).

## 6 UI
Compact controls on the Creative tab Advanced area: TENSION toggle, SHAPE combo, BARS combo, END combo, four depth knobs (grouped "Tension"). "EDIT CURVES..." opens a popover (Performance tab hosts it permanently when that tab exists, 11): lane tabs Density / Intensity / Width / FX Send, a 480 x 160 px canvas with a 1/16 grid, draggable points, double-click adds, Delete removes, buttons for the four shapes (copy into Custom), Reset lane. Playhead from `lastPhase` at 15 Hz. Status line: "Bar 5 of 8, rising" or "waiting for transport". Tooltips: DEPTH "How far this lane moves from your knob value at the curve's peak and trough. 0 = lane untouched."; END "What happens after the last bar: repeat, keep the last value, or return to your knobs." Help "Tension & release" (order 1210): shape table, start-bar meaning, meter assumption (constant time signature from PPQ 0), not saved in `.gapfill` files. Accessible: canvas is a custom `AccessibilityHandler` list of points; Tab moves between points, arrows move 1/64 length horizontally and 1 % vertically (Shift = 10x), Insert adds, Delete removes, announcement "Point 3, bar 4.5, 72 percent". Reduced motion: playhead jumps in steps with no trail, popover opens instantly. Narrow: canvas scales to the popover width (>= 320 px), lane tabs become a combo.

## 7 MIDI/automation/CLAP/host impacts
Nine automatable, MIDI-learnable params (depths and shape are natural macro targets for F-M23; `fm21_start` automation is allowed but discouraged). Curve points are state, not automation. Requires host PPQ and time signature (VST3/CLAP provide; some hosts report none while stopped: neutral). Meter changes mid-song are not tracked (assumption documented in C-6/C-7). No latency change.

## 8 Tests (`Tests/features/TestTension.cpp`)
`off_depth0_no_transport_is_identity` (`memcmp`) | `shape_values_at_quarter_points_all_shapes` | `eval_lane_linear_smooth_step` | `phase_matches_oracle_fuzz` | `loop_seek_playthrough_equal` | `end_loop_hold_release` | `before_start_neutral` | `time_signatures_6_8_7_4` | `mapping_ranges_all_lanes_fuzz_100k_curves` | `fx_send_neutral_at_or_above_half` | `width_delta_added_to_macro_width_and_clamped` | `width_block_size_tolerance` | `order_with_battle_evolve_breathing_phrase` | `decisions_block_size_invariant` | `rng_not_consumed` | `normalise_keeps_invariants_under_random_edits` | `validate_rejects_bad_curves` | `subtree_roundtrip_and_invalid_dropped_rest_loads` | `old_state_loads_off` | `seqlock_stress` (TSan) | `no_audio_thread_allocation`. Golden `FM21-A`: scenario A with Build+Drop 4 bars, `[golden]`. UI test: point editing via keyboard produces the same curve as mouse.

## 9 Dependencies & build order
Needs F0-5 (decision stage, `TransportInfo` with time signature), F0-6, F0-9; C-6/C-7 for `BarMath` ordering (soft; this PR creates the header otherwise); after F-M18 or with the shared `SeqSnapshot<T>`. Consumers: F-M14/15/16/20 read `fxSend01` (no effect until they land); F-M23 targets; U-5 Advanced disclosure; Performance tab (11) for permanent placement. Build order among mine: F-M17 (independent) ... F-M18 -> F-M21 -> F-M23.

## 10 Effort & risks
L (editor UI is most of it). Risks: users expect it to follow arrangement markers (not available, bar index from PPQ 0 only); constant-meter assumption on odd-meter songs; "Release" vs hard "drop" semantic confusion (Help table); width evaluated per chunk (documented tolerance); a second periodic density source (Breathing) can double-modulate (order fixed, both clamp).

## 11 Acceptance checklist
- [ ] Neutral cases `memcmp` identical; golden A-I unchanged
- [ ] Curve value matches oracle through seeks, loops, block sizes
- [ ] Fuzz: all lanes within legal ranges; invalid state dropped safely
- [ ] Order with Battle/Evolve/Breathing/Phrase tested
- [ ] Keyboard-only curve editing and screen-reader announcements verified; reduced-motion behaviour checked
- [ ] Minimum-size screenshots (popover and compact controls), Help and tooltips merged
- [ ] Manual DAW check on a looped region in one host per OS; unverified hosts listed
