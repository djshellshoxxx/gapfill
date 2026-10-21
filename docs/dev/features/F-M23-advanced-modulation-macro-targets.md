# F-M23 Advanced modulation and macro targets
Status: implementation spec, Gate C candidate (08). Category VAL/LIVE, value H, size L. **Canonical spec: `04-SPEC-CREATIVE.md` C-2 Performance Macros** (four macros, <= 8 target mappings each, fixed `macroTargets()` table of 21, `min01/max01/curve`, lower-index wins, `<Macros>` subtree, UI strip). This file does not repeat it. It extends C-2 with: more macros, a modulation source per macro (tempo-synced LFOs and an input-energy follower) with host-automatable depth, new targets for the F-M effects, and one ordering change that makes macros, Battle (F-M18) and Tension (F-M21) compose predictably.

## 1 Summary & user-visible behaviour
- **Eight macros** (C-2's Activity, Chaos, Space, Destruction plus Macro 5-8; the extra four sit behind a "+4" expander).
- Each macro has a **Source**: **Knob** (today's behaviour, default), **LFO A**, **LFO B**, or **Energy** (follows how loud/busy the input is). With a modulation source the macro value is `knob + depth * source`, depth -100..+100 %, so the knob is the centre and depth is host-automatable.
- Two **LFOs**: shape (Sine, Triangle, Saw, Square, Random steps, Random glide), tempo division (16 fixed entries, 1/16 ... 8 bars) or free Hz when the host gives no tempo. They are locked to PPQ, so loops and renders repeat exactly.
- **New mapping targets** (append-only, indices 21-35): Mistakes amount, Crush mix/bits/rate (F-M14), Resonator mix/decay/damp (F-M15), Ratchet gate/mix (F-M16), Tape Damage amount/mix (F-M20), FX Send, and the three band Fill Amounts. Targets whose module has not shipped are hidden and inert.
- Macros still change only the *effective* value; base knobs are untouched and C-0 rings show the effective value. Everything is bounded: a mapping can never push a target outside its own range.

## 2 Success/quality metric
- Equivalence: with all sources = Knob and macros 5-8 unassigned, effective settings are `memcmp`-identical to C-2 as specified, and golden A-I unchanged (macros-on goldens from C-2 unchanged).
- Determinism: LFO-driven decisions identical across block sizes {32 ... 8192}, loops and seeks (oracle compares LFO value at each decision PPQ to 1e-6); Energy-driven decisions identical across block sizes.
- Range safety: 100 000 random mapping tables x random depths x all sources keep every target inside its legal range and finite.
- Cost: <= 64 mapping evaluations + 2 LFO evaluations per gap decision (< 2 microseconds); zero allocation.

## 3 Design
Module `Source/features/modulation/` (`Lfo.h/.cpp`, `Sources.h/.cpp`, `Targets.h/.cpp`, `MacroEval.h/.cpp`, `Gui.*`, `Help.cpp`); C-2's own files (`Source/features/macros/`) keep model, state and strip UI and gain the hooks below.
```cpp
enum class MacroSource : std::uint8_t { Knob, LfoA, LfoB, Energy };
enum class LfoShape : std::uint8_t { Sine, Triangle, Saw, Square, RandomStep, RandomGlide };
struct LfoSettings { LfoShape shape; int divIndex; float freeHz; };                 // divIndex 0..15, see below
float lfoValue (const LfoSettings&, double ppq, bool transportValid, std::int64_t outputPos, double sr,
                std::uint64_t seed, int lfoId) noexcept;                            // returns [-1, 1]
struct ModInputs { float knob01[8]; MacroSource src[8]; float depth[8]; LfoSettings lfo[2]; };  // from params, block stage
struct DecisionInputs { double ppq; bool transportValid; std::int64_t outputPos; double sr; float energy01; std::uint64_t seed; };
void evaluateMacros (EngineSettings& sLocal, const MacroState&, const ModInputs&, const DecisionInputs&,
                     MacroOutputs&) noexcept;                                      // decision stage
struct TargetInfo { const char* id; float& (*field) (EngineSettings&); juce::NormalisableRange<float> range; };   // field == nullptr => module absent
const TargetInfo& macroTarget (int index) noexcept;                                // index 0..35, permanent
```
**LFO.** `cycles = ppq / divBeats` (transport valid, anchored at PPQ 0) else `cycles = (outputPos / sr) * freeHz`; `ph = frac (cycles)`. Sine `sin (2 pi ph)`; Triangle `1 - 4*abs (ph - 0.5)`... normalised to [-1,1]; Saw `2 ph - 1`; Square `ph < 0.5 ? 1 : -1`; RandomStep `v(floor (cycles))`; RandomGlide `lerp (v(k), v(k+1), smoothstep (ph))`, where `v(k) = 2 * unitFromHash (hashCombine (seed, hashCombine (kSalt_Lfo + lfoId, k))) - 1`, where `unitFromHash (h)` maps the top 24 bits of the 64-bit hash to [0,1) (pure function: random access, seek safe, no stream state). Divisions (`fm23_lfoADiv`), 16 permanent entries in beats: 1/16 (0.25), 1/8T, 1/8 (0.5), 1/8., 1/4T, 1/4 (1), 1/4., 1/2 (2), 1 bar (4), 2 bars (8), 4 bars (16), 8 bars (32), plus 1/32 (0.125), 1/16T, 1/16., 1/2T appended at indices 12..15 so the list is append-only; beats per bar use the host time signature (default 4/4). Indices never change meaning.
**Energy.** `energy01` = `analysis.frameAt (outputPos).energy` (hop-grid, deterministic); source value `2*energy01 - 1`.
**Macro value.** `src == Knob`: `x = knob01`. Else `x = clamp01 (knob01 + depth * s)`, `s` in [-1, 1], `depth` in [-1, 1]. Then C-2: `lerp (min01, max01, curve (x))` converted to the target's unit with its `NormalisableRange::convertFrom0to1` (honours skew; no allocation). Targets in `perf.fx.*` sit in the neutral structs F-M14/15/16/20 add; a null `field` skips the mapping. FX Send is multiplicative with Tension's lane (F-M21): `send = macroSend * tensionSend`.
**Ordering change (needs C-2 owner agreement).** C-2 applies macros once per block. This ticket moves macro application into the **decision stage**, using the block's knob/parameter values as inputs: for Knob-source macros the result is identical (same inputs, same formula; test `knob_macros_equal_c2_block_stage`), and it lets LFO/Energy sources be evaluated at the decision PPQ/sample, keeping block-size invariance. Decision-stage order becomes: Battle (F-M18) -> **Macros** -> Evolve (C-8) -> Breathing (C-6) -> Tension (F-M21) -> Phrase (C-7). Block stage keeps Personality (C-1). Width: the processor evaluates the width-mapped macro once per chunk at the chunk-mid PPQ and feeds the existing `widthAmount` smoother (20 ms); Energy is not allowed on a width mapping in v1 (UI refuses it).
**Smoothing rule (bounded and click-free by construction):** every target except width is consumed once per fill decision (no zipper possible); width goes through the existing smoother; any future per-sample target must use a `SmoothedValue` >= 5 ms (reviewed in PR).
**Extended target table (permanent indices; 0-20 are C-2's):**

| Idx | Target | Owner | Range (normalised -> unit) |
|---|---|---|---|
| 21 | Mistakes | C-3/F-M13 | `mistakes` 0-100 |
| 22 | Crush mix | F-M14 | `fm14_mix` |
| 23 | Crush bits | F-M14 | `fm14_bits` 2-16 |
| 24 | Crush rate | F-M14 | `fm14_rate` 1-64 (skew) |
| 25 | Resonator mix | F-M15 | `fm15_mix` |
| 26 | Resonator decay | F-M15 | `fm15_decay` 20-2000 ms (skew) |
| 27 | Resonator damp | F-M15 | `fm15_damp` |
| 28 | Ratchet gate | F-M16 | `fm16_gate` 10-100 |
| 29 | Ratchet mix | F-M16 | `fm16_mix` |
| 30 | Tape Damage amount | F-M20 | `fm20_amount` |
| 31 | Tape Damage mix | F-M20 | `fm20_mix` |
| 32 | FX Send | F-M21 | 0-1 multiplier on all FX mixes |
| 33-35 | Band Fill Amount low/mid/high | core | `bandLowAmt` ... |

Never assignable (unchanged from C-2): detector, protection, mix/output, latency, routing, bypass, seed, Fill Type, choice/bool/command parameters, `fm17_mode`, `fm19_*` (real-time per-sample voice), `fm21_*` structure.

## 4 Parameters & state
Group `GAPFILL_PARAMS_FM23(X)` (`Source/params/fm23.h`, `tools/param_ids/fm23.lock`), appended after C-2's `macros` group; `_` regex note in F-M13 s4. C-2's `macroActivity`..`macroDestruction`, `macrosOn` are **not new** and stay as registered in 02. If F-L06 lands first its macro parameters are `fl06_*`; macros 1-4 in this spec then map to those IDs and the equivalence test runs against them.

| ID | Type | Range | Default | Unit |
|---|---|---|---|---|
| `fm23_macro5` ... `fm23_macro8` | float | 0-100 | 0 | % |
| `fm23_src1` ... `fm23_src8` | choice | Knob, LFO A, LFO B, Energy | Knob | - |
| `fm23_depth1` ... `fm23_depth8` | float | -100..100 | 50 | % |
| `fm23_lfoAShape`, `fm23_lfoBShape` | choice | Sine, Triangle, Saw, Square, Random step, Random glide | Sine | - |
| `fm23_lfoADiv`, `fm23_lfoBDiv` | choice | the permanent division list (default 1 bar) | 1 bar | - |
| `fm23_lfoAHz`, `fm23_lfoBHz` | float, skew centre 1 | 0.05-10 | 0.5 | Hz (free-run fallback) |

(`src1..4` / `depth1..4` address C-2's four macros in order Activity, Chaos, Space, Destruction.) All in `performanceParameterIDs()`. Host-automatable: macro values, sources, depths, LFO settings. **Not automatable (state):** mapping tables and names. Subtree `<Macros>` is bumped to `version="2"`: up to 8 macros x 8 mappings, cap 4 KiB; v1 loads with macros 5-8 empty (names "Macro 5"...), identical behaviour; atomic per-subtree validation as C-2 (target index < 36, finite 0..1, curve <= 3, duplicates dropped, violation drops the whole subtree). A v2 subtree opened by an older build is dropped by that build's validation (downgrade unsupported, noted in release notes). **Neutral proof:** `macrosOn` off => nothing runs (C-2 R1); sources default Knob; macros 5-8 have no mappings; `knob_macros_equal_c2_block_stage` and golden tests.

## 5 Real-time safety
Decision stage: 8 macro values, <= 64 mappings, 2 LFO evaluations (a few sin/hash ops, no tables, no allocation, no RNG stream: LFO randomness is a pure hash of `(seed, lfoId, cycleIndex)`), fixed-size loops. The per-chunk width path adds one evaluation. `TargetInfo` ranges are built in `prepare()`. Parameter values are read through `ParamCache` atomics (06 s1.4). Reset table (06 s1.6): all five events: no state kept apart from the published effective values (recomputed at next decision).

## 6 UI
C-2's `PerformanceStrip` (F0-9) shows four macro knobs; a "+4" toggle reveals macros 5-8 (same row, scrolls inside the strip at minimum width). The Edit popover gains, per macro: a SOURCE combo and a DEPTH knob (bipolar, centre notch); a footer section "LFOs" with two rows (SHAPE, RATE combo, FREE Hz knob visible only when the host gives no tempo). A target picker lists only available targets, grouped (Core, Mistakes, Rhythmic, Tape, Resonator, Crush). Mapped controls show the C-0 effective ring and a source badge ("LFO A") in the tooltip. Tooltips: SOURCE "Knob: you set the macro. LFO: the macro swings around the knob, tempo-synced. Energy: follows how loud and busy the input is."; DEPTH "How far the source moves the macro from the knob. Negative inverts." Help "Macros and modulation" (order 1230). Accessible: titles "Macro 5 source", "Macro 5 depth"; keyboard Tab through rows, arrows adjust. Reduced motion: rings update without easing; no animated LFO scope (a static shape glyph). Narrow: popover becomes single-column, LFO section collapsed.

## 7 MIDI/automation/CLAP/host impacts
~26 new automatable, MIDI-learnable params (total parameter count grows; VST3/CLAP lists remain well under host limits, to be confirmed by pluginval/clap-validator). A macro with a modulation source and an automated knob both work; the knob moves the centre. MIDI Learn on a macro works unchanged. Hosts without PPQ use free-run Hz from the sample position (deterministic in offline render). No latency change.

## 8 Tests (`Tests/features/TestModulation.cpp`)
`knob_macros_equal_c2_block_stage` (memcmp over 10k random states) | `c2_tests_still_pass_unchanged` | `lfo_shapes_values_and_range` | `lfo_ppq_locked_loop_seek_equal` | `lfo_free_run_deterministic_by_sample_position` | `random_lfo_pure_hash_random_access` | `energy_source_block_invariant` | `source_depth_bounds_and_inversion` | `mapping_never_leaves_target_range_fuzz` | `absent_module_target_inert_not_dropped` | `fx_send_multiplicative_with_tension` | `order_battle_macros_evolve_breathing_tension_phrase` | `width_chunk_eval_smoothed_energy_refused` | `v1_subtree_loads_macros_5_8_empty` | `v2_roundtrip_and_invalid_dropped_rest_loads` | `target_table_indices_permanent` (golden list) | `macro5_8_defaults_neutral` | `automation_of_depth_applies_next_decision` | `midi_learn_new_params` | `no_audio_thread_allocation`. Golden `FM23-A`: scenario A with LFO A on Density (1 bar), `[golden]`.

## 9 Dependencies & build order
Hard: C-2 (all of it: model, subtree, strip) and F0-5 (decision stage, `TransportInfo`), F0-6, F0-9. Landing order of targets is free (indices reserved; `field == nullptr` until the module ships). Best after F-M18 and F-M21 so the decision-stage order is settled once; consumers of the new targets: F-M13, F-M14, F-M15, F-M16, F-M20 (their settings structs must be in `perf`). Last of my tickets in the build order.

## 10 Effort & risks
L. Risks: moving macro evaluation into the decision stage touches C-2's design (equivalence test is the guard; needs owner agreement); many new parameters (26) for hosts and the Random/Undo exclusion list; LFO on density plus Breathing/Tension can stack (documented order, all clamped); UI density (expander, collapsed LFO section); downgrade drops the v2 `<Macros>` subtree.

## 11 Acceptance checklist
- [ ] Knob-source behaviour `memcmp`-identical to C-2; golden A-I and C-2 goldens unchanged
- [ ] LFO/Energy decisions deterministic across block sizes, loops, seeks
- [ ] Fuzz: targets always within range; invalid subtree dropped safely; v1 state loads
- [ ] Decision-stage order tested with Battle, Evolve, Breathing, Tension, Phrase
- [ ] No audio-thread allocation; per-decision cost within budget
- [ ] Keyboard, screen-reader names, reduced motion, minimum-size screenshots reviewed; Help merged
- [ ] C-2 owner sign-off on the ordering change recorded in `docs/progress/F-M23.md`
