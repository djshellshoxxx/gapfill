# F-X05 Local preference learning for variations

Status: specified (this pass), not implemented. Size L. Depends on U-1 (History strip), C-11 (Like), F0-3 (`FillRecipe`), F0-5, F0-6, U-4 (global settings folder), Gate B. Opt-in, explainable, offline, no telemetry.

## 1. Summary and user-visible behaviour
The user rates fills in the History strip with **Like** (C-11) and **Skip** (new, dislike). With **Learn my taste** switched on, GapFill keeps those ratings on the user's own computer and derives a small, readable taste profile: which fill types and spectral bands the user tends to keep or reject. In Auto mode the profile gently tilts the odds of choosing each algorithm and band (never below half or above double, never enabling something at weight 0, never forcing). A **Taste** card shows exactly what it learned ("Leaning toward: Stutter 5 of 6 liked; Scramble 4 of 5 ... Leaning away from: Crush 1 of 6"), a strength control, undo of the last rating, and **Reset taste**. The taste profile used by a project is saved inside the project, so a saved project always renders the same way on every machine.

## 2. Success / quality metric
| # | Metric | Target |
|---|---|---|
| M1 | Simulated user (hidden liked set of 3 algorithms, 80 % consistent ratings), 30 ratings, strength 100: share of Auto fills from the liked set vs unbiased | at least +15 points (bounded by the 2x cap) |
| M2 | No collapse: any single algorithm's share vs unbiased | >= 25 % of baseline, <= 2.5x |
| M3 | Explainability: top-3 "leaning toward" items match the simulated truth | >= 90 % of 100 simulated users |
| M4 | Single rating effect (one Like) | multiplier change < 0.1 (smoothing) |
| M5 | Privacy: network symbols in `Source/features/taste/**`, files written | none / only the two files below, <= 64 KB each |
| M6 | Neutral: off, or on with fewer than the readiness threshold of ratings | bit-identical to no feature |

## 3. Design
### Modules (`Source/features/taste/`)
`TasteModel.h/.cpp` (pure: features, counts, scores, multipliers, explanation), `RatingStore.h/.cpp` (message thread: global library file I/O), `TasteBias.h` (POD for the audio thread), `Gather.cpp`, `Gui.*`, `Help.cpp`, `Info.cpp`, plus `Source/features/common/SnapshotBox.h` (single-writer, wait-free-reader double buffer, also reusable by F-S01).

### Features (computed from a `FillRecipe`, 18 binary indicators used for the bias)
`algo` one-hot (15), `band` one-hot for bands 0-2 (3). Extra indicators are computed and shown but not used for bias in the MVP: lengthClass (4), division bucket (fine/medium/coarse/none), pitch sign, modifier bits (5), stereo mode (4), mistake type. (Using them needs recipe overrides from F0-3; see stretch.)

### Model (transparent naive-Bayes style log-likelihood ratios)
Counts per feature `f`: `L_f` (liked recipes having f), `D_f` (skipped recipes having f); totals `L`, `D`. Smoothing `alpha = 2`.
`s_f = ln((L_f + alpha)/(L + 2 alpha)) - ln((D_f + alpha)/(D + 2 alpha))`. `ready = (L >= 3 && D >= 3 && L + D >= 8)`; below that every multiplier is 1.
`confidence = min(1, (L + D) / 40)`; `beta = fx05_strength / 100`.
Multiplier for algorithm `a`: `m_a = clamp(exp(beta * confidence * s_algo(a)), 0.5, 2.0)`; band `b` likewise `m_band(b)` (spectral mode only). Result `TasteBias { float algo[15]; float band[3]; bool ready; }`, quantised to int8 `q = round(64 * ln m)` in the project (`m = exp(q/64)`, range +-44).
Rationale for this simple model: it has no hidden state, is bounded by construction, can be explained in one sentence per feature, and 8-40 ratings is all a person will give. Heavier models (logistic regression, bandits) are explicitly out of scope.

### Where the bias is applied (no new random draws)
Block stage (`gatherEngineSettings`, F0-5): `if (taste.on && bias.ready) for i: s.weights[i] *= bias.algo[i]` (a zero weight stays zero; the draw sequence in `chooseAlgorithm` is unchanged because only weights differ), and `s.bandProb[b] *= bias.band[b]` clamped to [0,1]. One hook line. Ignored while Fill Lock is on, ignored when Fill Type is forced, and applied after Like bias (C-11 multiplies on top; both bounded: combined <= 2.0 * 2.5 on one algorithm, still never forcing).

### Data and persistence
* **Project** (`<Taste>` subtree, <= 64 B): `v`, `likes`, `skips` (uint16), `q[18]` int8, `ready`. Validated on load (ranges; bad subtree dropped and reset per F0-6). This is what the audio uses. Determinism: same saved subtree -> same weights.
* **Global ratings library** `<userApplicationData>/GapFill/ratings.xml`: up to 256 entries `{recipe(base64 16 B), rating(+1/-1), seq}`, FIFO eviction, de-duplicated by recipe bytes (last rating wins). Written atomically (temp file + rename) on the message thread, debounced 500 ms, read at editor creation and on "Rebuild from my history". If unreadable: empty library, a one-line notice. Multiple instances merge by recipe bytes on write (re-read before write; last writer wins per recipe).
* **Rating flow:** History chip action -> `RateRecipe(recipe, +-1)` on the message thread -> update library -> recompute counts -> recompute bias -> post `TasteBias` to the audio thread through `SnapshotBox` and store in the project subtree (a deliberate user action, like moving a knob) -> mark processor state dirty (`updateHostDisplay`).
* **Rebuild from my history** (button): recompute the project's profile from the global library (for a new instance), explicit user action.
* **Reset taste**: confirm dialog; clears project profile; second button "Also clear my library" clears the file.

### Explanation
`explainTaste (const TasteModel&, int maxItems)` returns strings from the same numbers used for the bias: `"Stutter: liked 5 of 6"`, ordered by `|s_f|`, only features with `L_f + D_f >= 3`. UI shows top 3 toward and top 3 away, plus a 15-bar multiplier chart with numeric labels.

## 4. Parameters and state
Group `fx05`, file `Source/params/fx05.h`, lock `tools/param_ids/fx05.lock`, appended to `GAPFILL_PARAMS_EXT`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fx05_on` | bool | off | - |
| `fx05_strength` | float 0-100 | 50 | % |
* `fx05_on` is in `performanceParameterIDs()` (never randomised). Subtree `<Taste>` as above. `stateVersion` unchanged; old sessions load with feature off and no subtree.
* Neutral proof: `fx05_on = off` -> the gather loop is skipped; on but not ready -> multipliers 1.0 and the loop is skipped by the `ready` check; both covered by golden A-F bit-identity and a `memcmp` of `EngineSettings` (weights untouched).

## 5. Real-time safety
Audio thread only reads `TasteBias` (21 floats) from `SnapshotBox` (atomic index, two preallocated slots) and multiplies 15+3 values per block. No file I/O, no allocation, no locks. All file I/O, model updates and UI on the message thread. Resets: not stateful in the audio path (bias is data); `reset()` keeps the bias; transport jump has no effect. Editor closed: ratings arrive only from the editor; the processor holds the model so a closed editor loses nothing.

## 6. UI
Placement: History strip actions (U-1 slot) get **Like** and **Skip** (both keyboard reachable, accessible names "Like this fill", "Skip this kind of fill"); the **Taste** card lives in the Creative tab Advanced area (`CreativeExtra`) as a foldable group: on/off ("Learn my taste"), strength, counts, the two explanation lists, the 15-bar chart (bars have text labels and numbers, not colour-only), Undo last rating, Rebuild from my history, Reset. First time the toggle is switched on a short notice states where data is stored and that nothing leaves the computer. Reduced Motion: bars change instantly. Help section "Taste" order 1740 with the privacy statement and the reproducibility rule (the profile is saved in the project).

## 7. MIDI / automation / CLAP / host
`fx05_on` and `fx05_strength` automatable; Like/Skip are commands (MIDI-learnable, extending `Command` with `SkipLatest`, `SkipRecipe` handled on the message thread because they only edit the model). No audio-thread MIDI work. Host project state carries the `<Taste>` subtree, so freezing/bouncing is deterministic. No latency change.

## 8. Tests (`Tests/features/TestTaste.cpp`, group `Taste`)
`features_from_recipe_all_algorithms` | `scores_zero_without_ratings` | `not_ready_below_threshold_is_identity` | `single_like_changes_multiplier_under_0_1` (M4) | `learns_liked_algorithms` (M1, simulated users, seeded) | `multipliers_clamped_0_5_to_2` (M2) | `zero_weight_stays_zero` | `draw_count_unchanged` (rng state equal after `chooseAlgorithm` with and without bias) | `ignored_when_locked_or_forced` | `explain_top3_matches_truth` (M3) | `explain_counts_match_data` | `quantise_roundtrip_error_under_1_percent` | `project_subtree_roundtrip` | `bad_subtree_dropped_rest_loads` | `same_project_same_fills_on_two_machines` (deterministic render with a saved subtree) | `library_cap_256_fifo_and_dedupe` | `library_atomic_write_and_corrupt_file_ignored` | `two_instances_merge_by_recipe` | `reset_clears_profile_and_optionally_library` | `no_network_symbols` (source scan test) | `no_audio_thread_allocation` | `off_is_bit_identical_golden_A_to_F` | `old_state_loads_off`. All simulated users use `Rng` seeded in the test; no wall-clock.

## 9. Dependencies and build order
U-1 (History, `RecipeHistory`), C-11 (Like command and family table), F0-3/4/5/6, U-4 settings folder. Order: model + store (pure) -> gather hook -> UI. F-L17 Mutation Lab and F-M22 smart suggestions can reuse the model.

## 10. Effort and risks
~6 ideal days (model 1, store 1, hook/state 1, UI 1.5, tests 1.5). Risks: (a) few ratings -> weak effect: by design, and the UI says so ("needs about 8 ratings"); (b) users may expect it to learn timing, not algorithm choice: Help states the scope; (c) cross-instance file races: atomic write + merge; (d) bias stacking with Like: bounded and tested. Decision for the owner: whether Skip should also influence the next 4 fills (symmetrical to Like's 4-fill push) or only the long-term profile (spec: long-term only). Stretch: bias for modifiers, stereo mode, mistake type and length class once recipe overrides ship; per-preset profiles; import/export of a taste file (reuse G-05 bank exchange).

## 11. Acceptance checklist
- [ ] Off and not-ready are bit-identical (goldens A-F, `memcmp`)
- [ ] M1-M6 recorded in `docs/progress/F-X05.md`
- [ ] Project determinism test (saved subtree) green on both OS
- [ ] Library I/O robust: corrupt, locked, read-only folder
- [ ] No network symbols; files documented in Help and `docs/COMPLIANCE.md`
- [ ] UI checklist (tooltip, names, keyboard, Reduced Motion, min size, Help)
- [ ] Unverified: multi-instance behaviour in two real hosts
