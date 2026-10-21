# F-M18 Beat Battles (alternating fill styles)
Status: implementation spec, Gate C candidate (08). Category FUN/MUS, value M, size M. No existing spec. Related: `04-SPEC-CREATIVE.md` two-stage effective settings (this is a **decision-stage** feature like C-6/C-7/C-8), `10-SPEC-LIVE-CREATIVE-EFFECTS.md` s4.3 scenes (F-M11 quantized scene changes switch the *whole* instance state on request; this ticket switches two stored *fill styles* automatically every N bars and needs only the shared bar/PPQ maths, not the scene queue).

## 1 Summary & user-visible behaviour
- Store two fill styles from the current knob settings (**Store 1**, **Store 2**). Turn on **BATTLE** and GapFill alternates between them every 1, 2, 4 or 8 bars, so the fills "battle" (for example sparse Subtle Ear Candy versus dense Drum Gap Glitch) without touching a control.
- A style holds the creative character only: Fill, Intensity, Complexity, Chaos, Density, Fill Type, Ping Pong, and the 15 algorithm weights. It does **not** hold the seed, detector, protection, mix, latency, divisions, width or bypass.
- Deterministic and seek-safe: which style is active depends only on the PPQ of each decision, so the same bar always uses the same style, in loops, after seeks and in offline renders.
- A style left empty means "use the live knobs" for that side, so enabling Battle with nothing stored changes nothing.
- No tempo/time-signature from the host: Battle is neutral (live knobs) and the status line says "Battle waiting for transport".
- Names are **Style 1 / Style 2** (not A / B) to avoid confusion with the header A/B compare.

## 2 Success/quality metric
- Switch accuracy: every gap decision with `ppq >= k * unitBeats` uses the side for bar-unit `k`; zero decisions use the wrong side in a 64-bar render with random seeks (test oracle computes the side from PPQ independently).
- Block-size invariant: identical fill start positions and algorithms for block sizes {32, 64, 512, 4096, 8192}.
- Neutral: `fm18_on` off, or both styles empty, or no valid transport => `EngineSettings` byte-identical (`memcmp`) and golden A-I unchanged. 0 allocations; <= 1 microsecond added per gap decision.

## 3 Design
Module `Source/features/battle/` (`Battle.h/.cpp`, `BattleStore.h/.cpp`, `Gui.*`, `Help.cpp`); shared helper `Source/features/effective/BarMath.h` (created here if C-6 has not landed): `double beatsPerBar (int tsNum, int tsDen) noexcept` (= `tsNum*4.0/tsDen`), `std::int64_t barUnitIndex (double ppq, double unitBeats) noexcept` (= `floor (ppq / unitBeats)`, correct for negative PPQ).
```cpp
struct BattleStyle {                         // trivially copyable, 96 bytes, all normalised like EngineSettings
    bool  set = false; std::uint8_t fillType = 0; bool pingPong = true; std::uint8_t pad = 0;
    float fill, intensity, complexity, chaos, density;          // 0..1
    float weights[numAlgos];                                     // 0..1
};
struct BattleSettings { bool on = false; int unitBars = 2; int first = 0; };   // from params, block stage
struct BattleSnapshot { BattleStyle style[2]; };                // 192 bytes
int  battleSide (double ppq, const TransportInfo&, const BattleSettings&) noexcept;   // 0, 1, or -1 (invalid => neutral)
void applyBattle (EngineSettings& sLocal, const BattleSnapshot&, const BattleSettings&,
                  double ppq, const TransportInfo&, int* sideOut) noexcept;
```
`battleSide`: valid only if `tr.valid && tr.tsNum > 0 && tr.tsDen > 0` and `tsDen` is a power of two (same rule as C-6). `idx = barUnitIndex (ppq, unitBars * beatsPerBar)`; `side = (int) (((idx + first) % 2 + 2) % 2)`. `applyBattle`: returns immediately (no write) when `! on`, side < 0, or `! style[side].set`; else overwrites `fill01, intensity01, complexity01, chaos01, density01, fillTypeIndex, pingPong` and `weights[]` of `sLocal` with the style. **Order in `applyAtDecision`:** Battle first, then Evolve (C-8), Breathing (C-6), Tension (F-M21), Phrase (C-7); so later stages modulate the chosen style. Consequence (documented): while Battle owns the creative fields, block-stage Personality (C-1) and Macros (C-2) adjustments to those fields are replaced for sides that have a stored style; the C-0 effective ring shows the style value and the macro badge reads "overridden by Battle".

**Style store (message thread -> audio):** `BattleStore` owns `BattleSnapshot` in a single-writer seqlock `SeqSnapshot<BattleSnapshot>` (`std::atomic<uint32_t> seq`, writer: odd while copying; reader: up to 2 attempts, otherwise keeps its last good local copy; no loops beyond 2, no locks). `captureFromKnobs (int side)` runs on the message thread: reads the raw APVTS creative parameters (`creativeParameterIDs()` minus `seed`, `stutterDiv`, `delayDiv`, `width`) and writes the style. Data flow: knobs -> `BattleStore` -> snapshot -> `EngineSettings::perf.battle` (copied once per block in `gather`) -> decision stage. The audio side also publishes `std::atomic<int> lastSide` and `std::atomic<float> beatsToSwitch` (relaxed) for the UI at 15 Hz.
Hooks: `EngineSettings::perf.battle` (neutral struct, F0-5), one line in `Effective::applyAtDecision`, one `gatherBattle()` in `FeatureHooks.h`, subtree registration in `StateSubtrees` (F0-6).

## 4 Parameters & state
Group `GAPFILL_PARAMS_FM18(X)` (`Source/params/fm18.h`, `tools/param_ids/fm18.lock`); `_` regex note in F-M13 s4. In `performanceParameterIDs()`.

| ID | Type | Range | Default | Unit |
|---|---|---|---|---|
| `fm18_on` | bool | - | off | - |
| `fm18_unit` | choice | 1 bar, 2 bars, 4 bars, 8 bars | 2 bars | - |
| `fm18_first` | choice | Style 1, Style 2 | Style 1 | which style plays on even bar-units |

State subtree `<Battle version="1">` (<= 512 B): `<Style side="1" fillType=".." pingPong=".." v="base64(22 floats)"/>` x2, absent side = empty. Validation (atomic per subtree, F0-6): finite floats in [0,1], `fillType <= numAlgos`, exactly the expected byte count; any violation drops the whole subtree (both styles empty) and the rest of the state loads. Old sessions: no subtree, no IDs => Battle off, styles empty, unchanged sound. Styles are session state, not part of `.gapfill` parameter files (same limitation as macros; Help says so). **Neutral proof:** the three early-outs in `applyBattle` plus the `memcmp` test.

## 5 Real-time safety
Decision stage only (once per evaluated gap): a floor/divide, a modulo and, at most, 22 float copies. Block stage copies the 192-byte snapshot once per block via the seqlock (bounded, wait-free for the reader). No allocation, no locks, no RNG draws (Battle never touches `Rng`; the decision key is unchanged because `seed` is not part of a style). Resets (06 s1.6): `reset`, `releaseResources`, sample-rate change, bypass, transport jump: Battle holds no per-fill state; the published `lastSide` is recomputed at the next decision.

## 6 UI
Slot: Performance tab when it exists (11), otherwise Creative tab Advanced via `CreativeExtra`. Row: BATTLE toggle, UNIT combo, buttons "STORE 1", "STORE 2", "CLEAR" (menu: clear 1 / clear 2 / clear both), a status text "Style 2 - switches in 3 beats" or "Battle waiting for transport". Each store button shows a one-line style summary as tooltip (for example "Density 70 %, Chaos 40 %, Stutter/Scramble heavy"). Tooltips: BATTLE "Alternates two stored fill styles every N bars. Needs a host tempo and time signature."; STORE "Saves the current Fill, Intensity, Complexity, Chaos, Density, Fill Type, Ping Pong and algorithm weights."; Help "Beat Battles" (order 1180) with the field list, order of effects, and the macro-override note. Accessible: titles "Battle on", "Store style 1", status text exposed as a live region (polite, announced only when the side changes). Keyboard: all buttons in Tab order. Reduced motion: the side indicator is a text label, no pulse or flash; non-reduced mode may tint the label for 150 ms. Narrow: the row wraps to two lines; status text truncates with ellipsis and full text in the tooltip.

## 7 MIDI/automation/CLAP/host impacts
`fm18_on`, `fm18_unit`, `fm18_first` are automatable and MIDI-learnable; storing styles is a UI action (not automatable, an optional `CommandId::BattleStore1/2` via F-S12 `CommandBindings` is deferred). Hosts without PPQ or time signature (some standalone/ARA contexts) leave Battle neutral. CLAP and VST3 expose only the three parameters. Audition/Preview (E-1) uses the live knobs, not the Battle side (documented). Offline render determinism follows PPQ.

## 8 Tests (`Tests/features/TestBattle.cpp`)
`off_or_empty_or_no_transport_is_identity` (`memcmp`) | `side_matches_independent_oracle_64_bars` | `negative_ppq_preroll` | `time_signatures_6_8_7_4_3_4` | `first_style_choice_flips_parity` | `unit_bars_1_2_4_8_boundaries` (decision at `ppq = k*unit - epsilon` vs `+ epsilon`) | `empty_side_uses_live_knobs` | `fields_replaced_and_others_untouched` (seed, protection, divisions, width unchanged) | `evolve_breathing_phrase_apply_on_top` | `seek_equals_playthrough` | `block_size_invariant_decisions` | `rng_not_consumed` | `seqlock_concurrent_writer_reader_stress` (TSan job) | `capture_from_knobs_excludes_seed_and_divisions` | `subtree_roundtrip` | `subtree_invalid_dropped_rest_loads` (NaN, 2.0, wrong length) | `old_state_loads_battle_off` | `macro_override_flag_published` | `no_audio_thread_allocation`. Golden `FM18-A`: scenario A, styles = two factory-preset snapshots, unit 1 bar, `[golden]`.

## 9 Dependencies & build order
Needs F0-5 (effective pipeline, `TransportInfo` with time signature), F0-6 (subtrees), F0-9 (slots), F0-4 (publish pattern). Shares bar maths with C-6/C-7 and ordering with C-8 and F-M21 (build C-6/C-7 first if possible; this PR creates `BarMath.h` otherwise). Soft: F-M11 (scene/phrase quantization), U-5 (Advanced disclosure), F-S12 (command binding). F-M23 macro targets are overridden on owned fields (documented there).

## 10 Effort & risks
M. Risks: users edit knobs while Battle is on and see no change on a side with a stored style (mitigation: effective-value ring, status text, "overridden" macro badge); style is a snapshot, so preset load does not change it (Help); name clash with A/B (renamed 1/2); seqlock misuse (stress test, TSan). Not stored in `.gapfill` files.

## 11 Acceptance checklist
- [ ] Neutral cases `memcmp`-identical; golden A-I unchanged
- [ ] Side selection matches the independent oracle through seeks, loops, tempo/time-signature changes
- [ ] Invalid subtree drops safely; old states unchanged
- [ ] Seqlock stress and TSan clean; no audio-thread allocation
- [ ] Status text, tooltips, Help, accessible names, minimum-size screenshot reviewed
- [ ] Manual check in one DAW per OS with a looped region; unverified hosts listed
