# F-L17 Mutation Lab

Status: specified, not approved. Size M. Category FUN, value M.
Overlap: none in `03`-`05`/`10` for generations or pinning. Related, not duplicated: Surprise/Random (`Presets.cpp`, one-shot), More Like That (C-11, recipe-level bias), A/B header slots (editor-owned snapshots), F-L23 (compares recipes, not parameter sets).

## 1 Summary and user-visible behaviour
A drawer "Mutation Lab" (File menu entry and a button at the foot of the Creative tab, no new header control). The current sound is the Parent. "Mutate" produces four children (A-D) by nudging the unpinned creative controls with seeded Gaussian noise. The user previews a child (it plays live through the engine without touching any knob, preset or host automation), then Keeps it (it becomes the new Parent and is written to the parameters as one Undo step), Discards, mutates again, steps back a generation (Revert), pins controls so they never change, breeds a child with a saved favorite, or saves a child as a Favorite (max 8, stored in the session). Same seed and generation always give the same children.

## 2 Success / quality metric
- Idle (no preview): `EngineSettings` identical to baseline; host automation lanes and parameter values untouched by previewing (test records `parameterChanged` count = 0 during preview).
- Children are distinct: pairwise L-infinity distance >= 0.04 and distance from parent >= 0.02 in normalised space (after at most 8 retries; else accepted and flagged).
- Determinism: children vectors are bit-identical across runs, platforms and sample rates for equal (lab seed, generation, parent).
- Keep then Undo restores bit-identical parameter values.

## 3 Design
Files: `Source/features/mutation/Genome.{h,cpp}` (gene table, mutate/breed, pure), `MutationLab.{h,cpp}` (message-thread controller, owned by the processor), `MutationOverlay.{h,cpp}` (audio-side overlay), `State.cpp`, `Gui.{h,cpp}`, `Help.cpp`.

### 3.1 Gene table (append-only, versioned)
`genes()` returns a constexpr array of `{ const char* paramId; }`, version 1 (29 genes): `fill, intensity, complexity, chaos, density`, the 15 `w*` weights in `Algo` order, and `band{Low,Mid,High}{Amt,Int,Prob}`. All are floats whose EngineSettings fields exist today (`fill01, intensity01, complexity01, chaos01, density01, weights[], bandAmt[], bandInt[], bandProb[]`). Choices, bools, detector, protection, mix, output, bypass, seed, sync and all `performanceParameterIDs()` are excluded. Scope selector: Character (first 5), Character+Weights (20), All (29).

### 3.2 Mutation operators (normalised 0..1 space, `Rng` from `DspUtils.h`)
- Gaussian: `x' = clamp (x + sigma * z, 0, 1)`, `sigma = rate` (5..50%, default 15%), `z` by Box-Muller from two `Rng::nextFloat()` draws (guard `u1 >= 1e-7`).
- Weights: after mutation, if all weights < 0.05 set the largest back to 0.5 (never produce a silent engine).
- Breed with favorite F: per gene take parent or F with probability 0.5, then Gaussian with `sigma/2`.
- Child RNG: `Rng (hashCombine (hashCombine (labSeed, generation), childIndex))`; distinctness retry uses `childIndex + 4 * attempt`. `labSeed` is stored in the subtree; "Reseed" draws from `juce::Random` on the message thread (never the engine RNG).
- Pins: 29-bit mask; pinned genes copy the parent value.

### 3.3 Generations
`std::vector<Generation>` capped at 16 (session only, not saved): parent vector, four children, pins at creation. Revert pops one. Keep on child c pushes a new generation whose parent is child c.

### 3.4 Preview overlay (audio side)
Message thread pushes `OverlayMsg { bool active; float v[29]; }` into `SpscQueue<OverlayMsg, 4>` (F0-4). The audio thread drains at the top of `processBlock` into its own `MutationOverlayState`. In `Effective::applyBlock`, first stage: if `active`, overwrite the matching `EngineSettings` fields (before personality and macros, so those still act on top). Retry on full queue at the next 15 Hz tick; UI rate-limit 20 msgs/s. Preview ends (inactive message) on Discard, Keep, editor close, preset change, Undo, state load, drawer closed.

### 3.5 Keep
Message thread: `pushUndoSnapshot()`; for each unpinned gene `param->beginChangeGesture(); setValueNotifyingHost (...); endChangeGesture();` then clear overlay. Undo works through the existing stack (`maxUndoSteps = 32`).

## 4 Parameters and state
No APVTS parameters (lab settings are session tooling, not automation targets; avoids adding index slots). Subtree `<Mutation v="1" seed rate scope pins>` with up to 8 `<Fav name="..." g="base64 of uint16 x genes">`, total cap 2 KiB, registered in `StateSubtrees`. Validation: name 1..16 printable UTF-8, `g` length exactly `2 * nGenesAtVersion` (shorter = older gene table, remaining genes keep current values), values <= 65535, rate 0.05..0.5, scope 0..2; any violation drops the whole subtree to defaults and the rest of the state still loads. Absent subtree = defaults. Generations, previews and the overlay are never serialised. Neutral proof: with no active overlay `applyBlock` returns before touching `EngineSettings` (memcmp test).

## 5 Real-time safety
Audio thread work: one queue drain (<= 4 small PODs, no allocation) and a copy of <= 29 floats when active. No RNG, no locking. All generation, base64 and state work happens on the message thread. Queue overflow behaviour defined (drop newest, UI retries). Overlay values pass through the same range clamps as parameters (`jlimit 0..1`, finite check) before use.

## 6 UI
Drawer inside the Creative tab content area (collapsed by default; occasional action per `11`, rule 4). Contents: Parent summary line, four child cards (A-D) each with Preview / Keep / Fav buttons and a mini bar showing the three largest differences, a row Mutate / Revert / Breed with... / Reseed, Rate knob, Scope combo, and a per-control Pin toggle available from the control's right-click menu ("Pin for Mutation Lab") and in a compact pin list in the drawer. Tooltips: Mutate "Make four variations of the current sound. Nothing changes until you press Keep." Preview "Hear this variation now. Your knobs and automation are not touched." Help section order 1170 "Mutation Lab". Accessible names: "Variation B, differs in Density plus 18 percent"; live-region announcement on Keep. Keyboard: arrow keys move between cards, Enter previews, Ctrl+Enter keeps. Reduced motion: no card flip animation. Narrow: cards stack vertically in a scroll area confined to the drawer. C-0 effective rings show the preview values with a "preview" tick.

## 7 MIDI / automation / CLAP / host
No automatable parameters. Commands (Mutate, Keep, Discard, Revert, Preview A-D) may be bound through F-L24 `CommandBinding`. Keep writes real parameters, so hosts record automation only on Keep (inside a gesture). CLAP/VST3/AU unaffected. Preview is not part of the saved state; closing the editor ends it.

## 8 Tests (`Tests/features/TestMutation.cpp`)
`gene_table_ids_exist_and_are_floats` | `children_deterministic_same_seed` | `children_distinct_thresholds` | `pins_never_change` | `values_always_in_range` | `weights_never_all_zero` | `breed_takes_genes_from_both` | `rate_scales_distance_statistically` (1000 draws, mean distance monotonic in rate) | `preview_does_not_change_parameters_or_notify_host` | `preview_ends_on_editor_close_preset_undo_state_load` | `keep_is_one_undo_step_bit_exact` | `revert_restores_previous_parent` | `generation_cap_16` | `overlay_idle_is_identity_memcmp` | `overlay_applied_before_personality_and_macros` | `queue_full_retry` | `subtree_roundtrip` | `subtree_bad_entry_drops_subtree_keeps_state` | `old_gene_table_shorter_vector_loads` | `no_audio_thread_allocation`.

## 9 Dependencies and order
F0-3..F0-6 (queue, effective pipeline, subtrees), F0-9 (slot/drawer host), C-0 (rings). Optional: E-1 (Preview then Audition), U-4 (save child as user preset). Independent of F-L16, F-L22 beyond the documented precedence.

## 10 Effort and risks
M. Risks: users losing a good preview by closing the editor (mitigated by Fav and a confirm toast); overlay shown by C-0 rings must not be confused with macros (distinct tick); gene-table growth must stay append-only.

## 11 Acceptance checklist
- [ ] Idle overlay is identity; previewing causes zero parameter notifications
- [ ] Deterministic children on all three OSes
- [ ] Keep is one exact Undo step; Revert works across 16 generations
- [ ] Favorites round-trip; corrupt subtree dropped without blocking state
- [ ] Preview always ends on the listed events
- [ ] UI reviewed at min/default/large, keyboard-only, screen-reader names
- [ ] No allocation on the audio thread (tracer test)
