# 04 - Creative features

Tickets: **C-0** effective-value display, **C-1** Fill Personalities, **C-2**
Performance Macros, **C-3** Controlled Mistakes, **C-4** Fill Intensity Envelope,
**C-5** Fill Lock, **C-6** Density Breathing, **C-7** Musical Phrase Awareness,
**C-8** Evolve, **C-9** Per-band fill type, **C-10** Capture v2, **C-11** More Like That.

These refine the behaviour in the creative spec (§4, §8). Where this file
disagrees with it, the revision table in `02-FOUNDATIONS.md` explains why.

## Two-stage effective settings (read first)

The creative spec orders the pipeline base -> personality -> macros ->
breathing/phrase -> decision. Implementing that as one per-block function
would make decisions depend on **where the host cut the block**, breaking the
project's block-size-invariance guarantee (decisions today are made on the
analysis hop grid, independent of block size). So the pipeline has two stages:

| Stage | Runs | Inputs | Features | Output |
|---|---|---|---|---|
| **Block stage** | once per `processBlock`, in `gatherEngineSettings()` | parameters only (no PPQ) | Personality (C-1), Macros (C-2) | modified `EngineSettings` (fill, intensity, complexity, chaos, density, weights, width) |
| **Decision stage** | once per evaluated gap, inside the scheduler, with the PPQ **at that sample** | `EngineSettings` + `perf` structs + ppq/time-signature | Evolve (C-8), Breathing (C-6), Phrase (C-7) | a local copy `sLocal` used for that decision only |

`FillEngine::trySchedule` creates `EngineSettings sLocal = s;` (about 300 bytes,
no allocation) and calls `Effective::applyAtDecision (sLocal, ppqAtDecision, tsInfo)`
before `checkEligibility`. This replaces the `applyBreathing/applyPhrase/applyEvolve`
block-level stubs sketched in `02` F0-5: those become decision-stage functions with
the signature above, and `PerformanceSettings` carries `BreathSettings`,
`PhraseSettings`, `EvolveSettings` (all neutral by default). `ppqAtDecision` is the
same expression the decision key already uses (`ppqAtBlockStart + (outputPos - blockStartPos)/samplesPerBeat`).

Per-fill stages (Like, Lock, Mistake, Envelope) are applied to the recipe / voice
after the decision, as in the creative spec.

Every feature below MUST be neutral at its default (golden test unchanged).

---

## C-0 Effective-value display on knobs   (size S, prerequisite of C-1/C-2)

When a control's *effective* value differs from its base (personality, macro, evolve),
the knob draws a thin secondary arc/marker at the effective value, and the hover
readout shows "base -> effective" (e.g. "Density 70% -> 84%").

* `FineSlider`/`Knob` (`Source/gui/Knob.{h,cpp}`) gain
  `void setEffectiveValue (std::optional<double> v01)`; null = hide. The default
  paint path is untouched when null (visual regression: screenshot test unchanged).
* Source of truth: `Processor::getEffectiveCreative()` returns a small POD
  (`std::array<float, N>` of the macro-targetable controls) written by the audio
  thread once per block into relaxed atomics (the block stage output) and read by
  the editor timer. Decision-stage (PPQ) values are shown only as a "breathing" LED
  and are not drawn on the knobs (they change per gap).
* Respects Reduced Motion (no animation), colour is not the only cue (also a tick
  mark), has an accessibility description "effective value 84 percent".

---

## C-1 Fill Personalities   (size M, depends F0-5, C-0)

### Behaviour
Parameter `personality` (Off, Subtle, Percussive, Glitchy, Psychedelic, Dub,
Aggressive, Ambient, Unhinged). A personality is a **block-stage overlay** that
multiplies the algorithm weights and four scalars. It never writes parameters,
never touches protection, detector, mix, latency, or the Fill Type.

### Overlay definition (final numbers; tune only in C-1's own PR)
`weights[i] = clamp (userWeight[i] * wMul[i], 0, 100)`; scalars are
`density01 *= dMul` etc. clamped to 0..1. `1.0` = unchanged. A user weight of 0
**stays 0** (the user's exclusions are respected). If every effective weight is 0,
fall back to the user's weights for that block (guards against "cannot generate").
Fill Type forced (`fillTypeIndex > 0`) bypasses the weight overlay (scalars still apply).

| Algo \ Personality | Subtle | Percussive | Glitchy | Psychedelic | Dub | Aggressive | Ambient |
|---|---|---|---|---|---|---|---|
| Reverse | 0.6 | 1.2 | 0.9 | 1.0 | 1.2 | 0.8 | 1.1 |
| Stutter | 0.2 | 1.6 | 1.5 | 0.4 | 0.5 | 1.4 | 0.1 |
| Delay | 1.0 | 0.8 | 0.7 | 1.2 | 2.0 | 0.6 | 1.2 |
| Granular | 1.2 | 0.6 | 1.3 | 1.6 | 0.6 | 0.7 | 1.4 |
| Freeze | 0.6 | 0.3 | 0.6 | 1.5 | 0.8 | 0.3 | 1.8 |
| PitchRise | 0.6 | 0.8 | 0.6 | 1.2 | 0.8 | 0.9 | 0.8 |
| PitchFall | 0.6 | 0.8 | 0.6 | 1.2 | 0.8 | 1.2 | 0.8 |
| Filter | 1.2 | 0.8 | 0.8 | 1.2 | 1.6 | 0.8 | 1.2 |
| Texture | 1.3 | 0.3 | 0.3 | 1.4 | 0.8 | 0.2 | 2.0 |
| Crush | 0.2 | 0.8 | 1.5 | 0.3 | 0.2 | 1.6 | 0.1 |
| Distort | 0.2 | 0.6 | 1.0 | 0.3 | 0.4 | 1.8 | 0.1 |
| Scramble | 0.2 | 1.4 | 1.6 | 0.5 | 0.3 | 1.2 | 0.2 |
| Spectral | 0.6 | 0.2 | 0.6 | 1.8 | 0.4 | 0.3 | 1.6 |
| Comb | 0.6 | 0.4 | 0.6 | 1.2 | 1.2 | 0.6 | 0.8 |
| TapeStop | 0.6 | 0.8 | 0.8 | 0.8 | 1.2 | 0.8 | 0.6 |
| **density x** | 0.80 | 1.10 | 1.15 | 0.90 | 0.85 | 1.20 | 0.60 |
| **complexity x** | 0.70 | 1.00 | 1.20 | 1.20 | 0.80 | 1.10 | 0.70 |
| **chaos x** | 0.60 | 0.90 | 1.20 | 1.10 | 0.60 | 1.10 | 0.50 |
| **intensity x** | 0.70 | 1.10 | 1.00 | 0.90 | 0.90 | 1.30 | 0.50 |

**Unhinged**: `weights[i] = lerp (userWeight[i], meanNonZero (userWeights), 0.7)`
for `userWeight[i] > 0` (flatten = maximum variety); density x1.2, complexity x1.3,
chaos x1.5, intensity x1.1; and once C-3 exists the *effective* `mistakes01 =
max (mistakes01, 0.35)`. All limits (max fill, protection, min gap) are unaffected.

### Code
`Source/features/personality/{Personality.h,.cpp,Gui.*,Help.cpp}`;
`Effective::applyPersonality (EngineSettings&, int personality)` table-driven
(`constexpr float kOverlay[8][19]`). UI: a combo box in the `CreativeExtra`
slot; changing it does NOT push an Undo snapshot (it is a normal parameter).

### Tests
`personality_off_is_identity` | `personality_does_not_write_parameters` |
`personality_respects_zero_weights` | `personality_all_zero_falls_back` |
`personality_modes_are_distinct` (pairwise L2 distance of normalised effective
weight vectors > 0.15) | `personality_histogram_matches_intent` (500 seeded gaps per
mode on the drum loop; e.g. Dub: Delay share is the largest; Percussive: Stutter+Scramble
> 35%; Ambient: Texture+Freeze+Spectral > 40%) | `personality_forced_fill_type_wins` |
`personality_deterministic_same_seed` | `personality_state_roundtrip_and_old_state_off` |
`personality_scalars_clamped`.

---

## C-2 Performance Macros   (size XL: split 2a engine, 2b UI, 2c editor)

### Model
Four macros: **Activity, Chaos, Space, Destruction** (renamable <=16 chars).
Each has a value (parameters `macroActivity`..`macroDestruction`, 0-100) and up to
8 target mappings. `macrosOn` (default Off, revision R1) enables the whole system.

```cpp
struct MacroMapping { std::uint8_t target; float min01, max01; std::uint8_t curve; };   // curve: 0 lin, 1 ease-in (x^2), 2 ease-out (1-(1-x)^2), 3 S-curve (smoothstep)
struct MacroState   { bool on; float value01[4]; char name[4][17]; std::uint8_t count[4]; MacroMapping map[4][8]; };
```

* `target` indexes the fixed table `macroTargets()` (stable order = persisted):
  `fill, intensity, complexity, chaos, density, width, wReverse ... wTapeStop` (21
  entries). Detector, protection, mix, latency, routing, bypass, seed, Fill Type and
  all choice/bool/command parameters are **not** assignable.
* `min01/max01` are in the **normalised range of the target parameter**
  (0..1 of its `NormalisableRange`), not raw units, so mappings survive range
  tweaks. The stage converts to the `EngineSettings` unit.
* Effective value of a mapped target: `lerp (min01, max01, curve (value01))`
  -> replaces the base value for that block. "Owns the effective value until
  unassigned" (creative spec): base parameter is untouched and visible.
* A target may belong to only one macro. When two macros claim a target the
  **lower-index macro wins** and the UI refuses the second assignment (load: second
  dropped with a log line in debug builds only).
* `width` is applied after the engine, so `applyMacros` also fills
  `MacroOutputs { bool hasWidth; float width; }`; the processor uses it in place of
  `pv(params.width)` only when `hasWidth`.

### Default mappings (when the user presses "Reset macro" or a new instance
is created; same as the creative spec, with `Space` using weights only because
`width` needs the extra output - width mapping added in 2a)

| Macro (default value) | Targets (min-max in % of the target's range) |
|---|---|
| Activity (50) | Density 25-90, Fill 20-75 |
| Chaos (25) | Chaos 0-75, Complexity 20-85, wScramble 10-70 |
| Space (25) | Width 60-150 (as % of its own range; clamp to range), wDelay 20-75, wSpectral 10-55 |
| Destruction (15) | Intensity 20-90, wCrush 0-65, wDistort 0-65 |

### Smoothing and zipper noise
Macro values feed scheduler decisions (fill, density, weights, complexity, chaos,
intensity), which are sampled once per gap; they cannot zipper. `width` reaches the
existing `widthAmount` smoother. The macro value itself is read once per block like
every other parameter. Nothing else is required.

### Persistence (`<Macros>` subtree, <= 2 KiB)
Names (UTF-8, printable, trimmed, 1-16 bytes), counts <= 8, `target < 21`,
`0 <= min,max <= 1` finite, `curve <= 3`, duplicate targets dropped. Any violation
drops the **whole subtree** to defaults (atomic per subtree), the rest of the
state still loads (F0-6). Absent subtree => default mappings, `macrosOn` as stored.

### UI (slot `PerformanceStrip`)
Four knobs with the macro names under them, `MACROS` toggle LED, an "Edit" button
opening a popover: one tab per macro, a table (target combo, min, max, curve
combo, remove) with "Add target", rename text field, "Reset macro". Mapped
controls elsewhere show the C-0 effective ring and a small macro badge in their
tooltip. MIDI Learn/automation of the four values works unchanged (they are
parameters). Keyboard operable; every control has an accessibility title.

### Tests
`macros_off_is_identity` | `macro_value_zero_and_max_hit_min_max` | `each_curve_monotonic_and_bounded` |
`macro_never_exceeds_target_range` | `macro_does_not_write_base_parameters` |
`lower_index_macro_wins_shared_target` | `invalid_subtree_dropped_atomically` (also: rest of
state still applied) | `macros_roundtrip_state` | `old_state_loads_with_macros_off` |
`macro_width_output_applied` | `macro_midi_learn_drives_value` | `macro_effective_values_published_for_ui` |
`macro_names_sanitised` | `default_mappings_match_table`.

---

## C-3 Controlled Mistakes   (size L, depends F0-3; best after C-5 so recipes carry it)

### Bounds (from the creative spec §8, restated as requirements)
* `mistakes` = 0 -> no roll, **zero draws of any RNG**, identical to today.
* Per-fill probability `p = 0.20 * mistakes01` (linear to 20 % at 100 %), decided with
  `Rng sub; sub.seed (hashCombine (key, kSalt_Mistake))`.
* Exactly one type per fill, from the types **applicable** to the chosen algorithm:

| Type (id) | What it does | Applicable algorithms | Skip when |
|---|---|---|---|
| Wobble (1) | Output-side modulated delay (vibrato), depth giving at most +-3 % rate (~+-0.5 semitone), LFO 1.5-5 Hz | all | never skipped |
| OctaveDisplace (2) | Source read rate x2 or x0.5 (one octave) for the whole fill | Reverse, Stutter, Granular, Freeze, PitchRise, PitchFall | source length at x2 would read past captured material |
| MissedRepeat (3) | One stutter slice / delay tap / scramble slice muted with 2 ms ramps | Stutter, Delay, Scramble | fewer than 3 repeats |
| Brake (4) | Last 25 % of the fill: read rate ramps 1 -> 0.15 with matching gain fade | Reverse, Stutter, Granular, Freeze | fill shorter than 120 ms |
| StereoBounce (5) | 2-3 fast equal-power pan swings | all except mono output | main bus mono, or capture L/R nearly identical (< -60 dB difference), or fill < 80 ms |

* If the chosen type is not applicable/skippable, re-roll the **type** from the
  remaining applicable ones (same sub-RNG); if none, no mistake (`mistakeType = 0`).
* Hard limits (tests assert each): never extends the fill, never crosses the
  gap/protected boundary, no click (all gain changes have >= 2 ms ramps), output
  finite and inside the existing bounded sanitiser, mono stays mono.

### Code
`Source/features/mistakes/{Mistakes.h,.cpp}`: `class MistakeStage` owned by
`FillVoice` (member + 3 hook lines: `start()` configure from recipe, `render` per-sample
`process (l, r, t01)`, and `rateMultiplier (t01)` consulted where voices advance read
positions). Types 2 and 4 use `rateMultiplier` only for algorithms that expose a
`playRate`; the per-algorithm applicability table is `constexpr` and unit-tested
against every `Algo`. `FillRecipe::mistakeType/mistakeAmt` are set by `adjustRecipe`
(F0-2 hook) so History/Lock see them. With Lock on, the locked recipe's mistake field
is ignored; a fresh roll happens per fill.

### Tests
`mistakes_zero_is_bit_identical` (golden + RNG untouched) | `probability_converges_to_20pct`
(10k seeded rolls at 100 % -> 0.20 +-0.02) | `one_type_only` | `type_applicability_table_complete` |
`each_type_finite_and_bounded_all_algorithms` | `no_click_each_type` (max |dx| of output
derivative bounded vs no-mistake render + threshold) | `octave_skipped_without_source` |
`brake_skipped_short_fill` | `bounce_not_applied_mono` | `mistake_deterministic_same_seed_and_ppq` |
`mistake_never_crosses_boundary`.

---

## C-4 Fill Intensity Envelope   (size M, depends F0-3)

Parameters `fillShape` (Flat, Rise, Fall, RiseFall, RandomWalk), `fillShapeAmount` 0-100.

### Definition
`age01 = elapsed / totalLength` (voice age, not wall time). A shape `s(age01)` in
`[0,1]`:
`Flat: 1` | `Rise: age01` | `Fall: 1 - age01` | `RiseFall: sin (pi * age01)` |
`RandomWalk`: 5 knots `k0..k4 in [0,1]` from `Rng` seeded `hashCombine (setup.rngSeed, kSalt_Envelope)`,
smoothstep-interpolated.
With `a = fillShapeAmount/100`:

* `level = 1 - a * (1 - s)`  (range `[1-a, 1]`; `a = 0` or `Flat` -> exactly 1)
* applied as: voice output gain x `level` (before the existing fade/duck), and a
  one-pole lowpass whose cutoff is `fc = 20000 * 0.1^(a * (1 - s))` (20 kHz at
  `level` 1 down to 2 kHz at `a = 1, s = 0`). **When `a == 0` or `Flat` the filter
  and gain are skipped entirely** (no float ops, so bit-identical).
* `level`/`fc` are smoothed with a 5 ms one-pole so shape changes cannot click.
* Does not touch protection, duck, fade lengths, or `totalLength`.

Deferred (document, do not build): grain density and modulation-depth targets.

Code: `Source/features/envelope/Envelope.{h,cpp}` (`float shapeAt (shape, age01, const float knots[5])`
pure; `class EnvelopeStage` in the voice: `start (setup)`, `process (l, r, age01)`).
UI: shape combo + amount knob in `CreativeExtra`; a tiny preview curve drawing the
selected shape.

Tests: `envelope_shape_values_at_0_50_100` (all five) | `random_walk_repeatable_by_seed_and_varies_by_seed` |
`flat_or_zero_amount_bit_identical` | `level_within_1_minus_a_and_1` | `cutoff_bounds` |
`works_with_short_and_long_fills` | `no_click` | `unchanged_protection_duck`.

---

## C-5 Fill Lock   (size L, depends F0-2, F0-3, F0-4, F0-6)

### Behaviour
Parameter `fillLock` (bool). While on, every *eligible, probability-passed* gap
is filled with the locked **recipe**, not a fresh algorithm choice. Lock pins
character (algo, band, division, pitch, modifiers, stereo, length class); it does not
force a fill in every gap, change the roll, replay audio, or relax protection.
A fresh `seed` per fill keeps repeats from being literal loops.

* **Engage**: toggling Lock on stores the latest completed *natural* recipe
  (preview recipes excluded). If none exists yet, Lock is *armed*: the next natural
  fill is stored and becomes the lock. Toggling Lock on in History (U-1) with a
  selected row locks that row instead.
* **Disengage**: toggling off returns to normal selection immediately; the recipe is
  kept (UI "Unlock" greyed, "Locked: ..." text cleared) and re-engaging uses the
  *latest* fill again.
* **Fit rules** (explicit safe fallback): in `buildRecipe` with lock active,
  `lengthSamples = min (lockedLength, usable)`. If that is shorter than
  `0.6 * minMs` of the locked algorithm -> **skip the gap**, reason
  `LockSkipped` (new `RejectReason` value, add to F0-2's enum before `Count`).
  If the locked band is disabled or `bandProb == 0` in spectral mode -> choose the
  nearest enabled band, else skip with `NoBand`. If the locked algorithm's weight is 0,
  Lock still plays it (the user locked it deliberately); Fill Type forced overrides Lock
  (visible warning "Fill Type overrides Lock").
* Mistakes (C-3) roll fresh per fill; Like bias (C-11) is ignored while locked.

### State and threading
* Audio thread owns `FillRecipe lockRecipe; bool lockValid;`. Commands
  `LockLatest`, `LockRecipe (recipe)`, `Unlock` arrive via `commands`
  (message thread pushes them from `parameterChanged (fillLock)` and the History UI).
* Audio thread publishes `FillEvent::LockSet (recipe)` after storing, so the
  processor's mirror (`<Lock>` subtree: `valid`, 16-byte recipe as base64, 1-byte
  checksum) is always current and can be saved with the editor closed.
* On load: validate subtree (`isValid (recipe)` + checksum); valid -> push
  `LockRecipe`; invalid/absent with `fillLock = true` -> armed. Never serialises or
  replays audio; restore cannot trigger anything audible (nothing plays until the
  next natural gap).
* `fillLock` is in `performanceParameterIDs()` -> excluded from Random/Surprise/Undo.

### UI
Lock toggle (padlock icon + text "LOCK") in `CreativeExtra`; summary text "Locked:
Stutter 1/32 - High - ~180 ms"; tooltip lists the fit rules in one line each; shows
"armed" state. Keyboard + screen-reader label.

### Tests
`lock_pins_character_across_gaps` (20 gaps: algo/band/division identical) | `lock_does_not_force_fill_each_gap` |
`lock_uses_fresh_seed` | `lock_adapts_length_to_short_gap` | `lock_skips_unfit_gap_with_reason` |
`lock_band_fallback_and_skip` | `lock_armed_captures_next_fill` | `lock_ignores_preview_recipes` |
`unlock_returns_to_selection` | `lock_state_roundtrip` | `lock_corrupt_recipe_does_not_block_load` |
`lock_restore_plays_nothing` | `lock_with_protection_never_overruns_next_event` | `fill_type_forced_overrides_lock`.

---

## C-6 Density Breathing   (size M, depends F0-5)

Parameters `densityBreath` (bool, off), `densityBreathBars` (1/2/4/8, default 4),
`densityBreathDepth` (0-100, default 0), `densityBreathCurve` (Sine, RiseFall, Pulse).

* Decision stage. Valid only when transport is valid **and** the time signature is
  known (`tsNum > 0`, `tsDen > 0`, power of two). Else neutral (offset 0).
* `beatsPerBar = tsNum * 4.0 / tsDen`; `phase = frac (ppq / (bars * beatsPerBar))`
  (anchored at PPQ 0, so loops and seeks reproduce).
* Curve `c(phase) in [-1,1]`: Sine `sin (2 pi phase)`; RiseFall triangle
  `phase < 0.5 ? 4 phase - 1 : 3 - 4 phase`; Pulse `phase < 0.5 ? 1 : -1`.
* `density01' = clamp (density01 + 0.5 * depth01 * c, 0, 1)` ("centred" on the base;
  at depth 100 % the swing is +-0.5).
* Visual: a small "breathing" LED/curve in the density panel showing the current
  phase (polled from an atomic of the last decision).

Tests: `breath_neutral_when_disabled_depth0_or_no_transport` | `curve_values_at_quarter_points` |
`same_ppq_same_density` | `loop_wrap_continuity` (position p and p+loopLength agree) |
`depth_never_leaves_0_1` | `time_signature_6_8_and_7_4` | `block_size_invariant_decisions`.

---

## C-7 Musical Phrase Awareness   (size M, depends F0-5)

Parameters `phraseBars` (Off, 2, 4, 8, 16), `phraseBias` (0-100, default 0).

* Decision stage, same validity rule as C-6. `phraseLenBeats = bars * beatsPerBar`;
  `p = frac (ppq / phraseLenBeats)`; final-bar start `f = 1 - 1/bars`.
* `r = smoothstep (f, 1, p)` (0 before the last bar, ramps to 1 at the boundary).
* **Fill probability multiplier** `1 + phraseBias01 * 1.5 * r` (so up to x2.5 at full
  bias), then the existing probability clamp to 1. It multiplies the *probability*
  only; it can never create a gap or override `checkEligibility` (min gap, protection,
  voices). Outside the final bar the multiplier is exactly 1.
* UI: phrase combo + bias knob; a thin phrase-position bar under the visualiser.

Tests: `phrase_off_or_bias0_is_identity` | `multiplier_ramp_values` | `invalid_ppq_or_ts_neutral` |
`loop_and_seek_reproducible` | `never_fills_ineligible_gap` | `block_size_invariant_decisions`.

---

## C-8 Evolve   (size L, stretch, depends C-1, C-6/7 infrastructure)

New idea: slowly drift the creative character on its own (set-and-forget "it
keeps changing"), using the Surprise profiles as waypoints. Off by default.

* Parameter `evolve` (Off, 8, 16, 32 bars). Decision stage, needs valid PPQ/ts
  (else neutral).
* `Presets.cpp` exposes (new, read-only) `const SurpriseTargets& surpriseTargets (int profile)`
  returning the **centre** of each profile's ranges (`fill, intensity, complexity,
  chaos, density`, `core` algorithm set -> weight 80 vs 6, divisions[0]). Refactor
  `surpriseProfiles()` to share one table; Surprise behaviour must remain byte-for-byte
  (existing Surprise tests cover it) - **this is the one allowed edit to `Presets.cpp`
  outside a preset pass.**
* Sequence: `cycle = floor (ppq / (period * beatsPerBar))`. `profile (cycle)` =
  element `cycle % N` of a permutation of `0..N-1` seeded by `hashCombine (seed, cycle / N)`,
  with a swap so the first element of a permutation never equals the last of the
  previous one. No recursion, fully random access (seek-safe).
* Blend: for the first 25 % of each period, `w = smoothstep (profile(cycle-1) -> profile(cycle))`;
  after that fully `profile(cycle)`. The effective creative values are
  `lerp (effective, target, 0.6 * w')` (0.6 caps the pull so macros/personality remain audible).
* Never touches seed, detector, protection, mix, latency, Fill Type. UI: "EVOLVE" combo and
  a label showing the current waypoint name ("-> Tape Breakdown").

Tests: `evolve_off_identity` | `profile_sequence_has_no_immediate_repeat` | `random_access_seek_equals_play_through` |
`blend_bounds` | `ignores_protected_params` | `invalid_transport_neutral` | `surprise_unchanged_after_refactor`.

---

## C-9 Per-band fill type   (size M, depends F0-2)

Parameters `bandLowType`, `bandMidType`, `bandHighType` (choice: Auto + 15
algorithms, default Auto). Only meaningful in **Spectral** detector mode, where
`chooseBand` already selects a band.

* In `buildRecipe`: after `band >= 0` is chosen, if `perf.bandType[band] > 0` the
  algorithm is forced to `Algo (bandType - 1)` (still checked against
  `0.6 * minMs`; if it doesn't fit -> `NoAlgoFits`, no silent substitution). Auto
  uses today's `chooseAlgorithm`. Global Fill Type forced (!= Auto) wins over band types.
* `Spectral`/`Comb` etc. behave as when forced globally (they already isolate the band via `BandSplitter`).
* Not randomised by Random/Surprise (stays in `performanceParameterIDs()`).
* UI: three combo boxes inside the existing band lane rows (`Panels.cpp` spectral panel is the one
  UI edit allowed in a hot file - add a `bandTypeCombo` into the band-lane component only).

Tests: `auto_identical_to_baseline` | `forced_per_band_used_in_that_band_only` | `global_forced_wins` |
`too_short_reports_NoAlgoFits` | `non_spectral_mode_ignored` | `state_roundtrip`.

---

## C-10 Capture v2   (size M, depends F0-4)

`capture` (latched bool) shipped in 0.0.1. Add:
* Parameter `captureLength` (250-2000 ms, default 1000): window of history the latch
  freezes from "now" backwards; stored `captureHold` is **not** added (R3).
* **Recapture** command (button + MIDI Learn): refreshes the latched material to the
  latest `captureLength` while Capture stays on; implemented with the incremental
  copy (<= 4096 samples per block), "Pending" UI state, 5-10 ms source crossfade at the
  swap, as the creative spec §8 "Capture, queue, and history bounds" describes.
* Status text "Capture: pending / held (1.0 s)". Release cancels pending copies and
  fades back to live history; sample-rate change and `reset` clear the latch.
Existing 0.0.1 behaviour (engage after 0.5 s history, `historyOffset` anchoring) must
remain: with `captureLength` at default and no recapture the golden `capture` scenario
is unchanged.

Tests: `recapture_replaces_material` | `pending_uses_live_until_swap` | `copy_bounded_per_block` |
`release_cancels_pending` | `samplerate_change_clears` | `cannot_read_unwritten_samples` | `no_audio_thread_allocation`.

---

## C-11 More Like That   (size L, depends F0-3, F0-4, U-1)

Command `LikeLatest` / `LikeRecipe (recipe)` (button in the History strip and the
Creative panel; MIDI-learnable). The audio thread stores `likeRecipe`, `likeRemaining = 4`.

* While `likeRemaining > 0` the algorithm weights seen by `chooseAlgorithm` are multiplied:
  `x2.5` for `likeRecipe.algo`; `x1.4` for algorithms in the same *family*
  (rhythmic: Stutter/Scramble/Delay; textural: Granular/Freeze/Texture/Spectral/Comb; pitch/tape:
  PitchRise/PitchFall/TapeStop/Filter; destructive: Crush/Distort/Reverse - table is
  `constexpr`, unit-tested to cover all 15). Band and division are biased the same way
  (x2 for the same band when spectral; stutter/delay division set to the recipe's when
  sync is on and the user division is Auto - **not** implemented: division stays the user's;
  only algorithm/band bias in v1).
* Bias never changes eligibility or enables a zero-weight algorithm (0 x anything = 0),
  and draws from the normal RNG stream only through `chooseAlgorithm` (weights differ,
  draw count identical - preserves determinism of later decisions).
* `likeRemaining` decrements **only on a started natural fill**; rejected gaps and previews
  do not count; a transport jump (`noteTransportJump`) resets it to 0; a new Like restarts it.
* Ignored while Fill Lock is on. UI: "LIKED x3" indicator.

Tests: `like_biases_but_not_forces` (over 1000 seeded gaps, share of liked algo rises from p to
~2.5p/(...) and < 100 %) | `like_expires_after_four_started` | `rejected_gaps_dont_count` |
`preview_doesnt_count` | `transport_jump_resets` | `zero_weight_stays_zero` | `like_ignored_when_locked` |
`family_table_complete` | `draw_count_unchanged`.
