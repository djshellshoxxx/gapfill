# F-L06 Four assignable performance macros
Status: implementation spec, Gate C candidate (backlog `08` F-L06, existing ticket C-2). Category LIVE/VAL, value H, size L (split 2a engine, 2b UI, 2c editor per C-2). Canonical: `04-SPEC-CREATIVE.md` C-2 (model, 21-entry target table, default mappings, `<Macros>` subtree rules, 14 tests) and `10` section 4.3 (polarity, smoothing, conflicts). This file: renames IDs to the ticket-prefix rule, settles three open points where `04` and `10` disagree, and adds the missing detail.

## 1 Summary & user-visible behaviour
- Four macro knobs (default names Activity, Chaos, Space, Destruction; renamable, 1-16 chars) each drive up to 8 targets with per-target min, max, curve and polarity. A **MACROS** switch enables the system; **off by default** (revision R1) so old sessions sound identical.
- Mapped controls elsewhere draw the C-0 effective-value ring and a "M1" badge; the base knob value is never overwritten and stays visible (10: "macro targets write to the effective-settings layer").
- Macro knobs are normal parameters: host automation, MIDI Learn, scenes (F-L07). An **Edit** popover per macro lists targets: target combo, min, max, curve (Linear, Ease-in, Ease-out, S-curve), **Invert** toggle, remove; **Add target**, **Reset macro**, rename field.
- A target may belong to one macro; a second assignment is refused with a visible message ("Density is already driven by Activity"). One CC maps to one parameter (existing `MidiLearnManager`), so "one CC, one target" is inherent.

## 2 Success/quality metric
- At macro value 0 and 100 % the effective target equals `min` and `max` exactly (float equality after curve(0)=0, curve(1)=1); all curves monotonic; effective value never leaves the target range.
- Macros off: `memcmp` of `EngineSettings` equal to the no-feature build over scenarios A-F (bit-identical audio).
- Responsiveness: MIDI CC to changed decision-stage value <= 1 processing block + 1 hop; UI ring updates within 2 timer ticks (15 Hz).

## 3 Design
Files (`cmake/sources/macros.cmake`, `Tests/features/TestMacros.cpp`; `Source/params/macros.{h,cpp}`):
- `Source/features/macros/Macros.{h,cpp}`: `MacroState`, `MacroMapping { std::uint8_t target; float min01, max01; std::uint8_t curve; }` exactly as C-2 (with `min01 > max01` legal and meaning inverted polarity; the **Invert** UI toggle swaps min and max). `constexpr std::array<MacroTarget, 21> macroTargets()` = stable persisted order from C-2 (`fill, intensity, complexity, chaos, density, width, wReverse ... wTapeStop`), each entry holds the parameter ID, the `EngineSettings` field accessor and its 0..1 conversion. Pure function `void applyMacros (EngineSettings&, const MacroState&, MacroOutputs&) noexcept` (F0-5 block stage): for each mapped target, `eff01 = lerp (min01, max01, curve (value01))`, converted from the target's normalised range to the EngineSettings unit. Early return if `! on`.
- Settled points:
  1. **Smoothing (10 says per-macro smoothing; 04 says none needed).** Decision: no smoothing on scheduler targets, because they are sampled once per gap and block-level smoothing would make decisions depend on host block size (violates `06` section 2). `width` is the only continuous target and already passes through the `widthAmount` smoother (set up in `processChunk`); its macro value feeds that smoother. 10's "smoothing" requirement is therefore met for continuous targets and intentionally not applied to discrete-per-gap targets. Record this deviation in `docs/progress/F-L06.md` and in Help.
  2. **Polarity**: via min/max order (C-2 already allows it); UI Invert toggle.
  3. **Neutral reset**: "Reset macro" restores the C-2 default mappings and default value for that macro; "Reset all macros" restores all four. Neither touches base parameters.
- `Source/features/macros/Subtree.{h,cpp}`: `<Macros v="1" on="0|1"><M n="Activity" v="50"><T t="4" a="0.25" b="0.9" c="0"/>...</M>...</Macros>` (<= 2 KiB). Validation (atomic per subtree): names UTF-8 printable 1-16 bytes, count <= 8, `t < 21`, `0 <= a,b <= 1` finite, `c <= 3`, duplicate targets across macros dropped (lower macro index wins). Any violation drops the whole subtree to defaults while the rest of the state loads (F0-6). Absent subtree -> default mappings.
- `Source/features/macros/Gui.{h,cpp}` (knobs + LED in `PerformanceStrip`; popover editor), `Help.cpp`, `Info.cpp`.
- Effective-value publication: `getEffectiveCreative()` (C-0) written once per block from the block-stage output; macros extend its array; editor timer reads it.
Data flow: parameters -> `ParamCache` atomics -> `gatherEngineSettings()` -> `Effective::applyBlock` -> `applyMacros` -> `EngineSettings`. Mapping table copy: message thread edits a staging `MacroState`, publishes with a double-buffer + generation counter (audio thread reads the buffer whose generation is even-stable; two preallocated `MacroState` objects, ~400 bytes each); never a shared mutable struct.
Minimal hooks: `Effective::applyBlock` (F0-5 already calls `applyMacros`), `processChunk` uses `MacroOutputs.hasWidth` for `widthAmount`, subtree registry entry, `params/macros.*`.

## 4 Parameters & state
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fl06_on` | bool | off | - |
| `fl06_m1` | float 0-100 | 50 | % |
| `fl06_m2` | float 0-100 | 25 | % |
| `fl06_m3` | float 0-100 | 25 | % |
| `fl06_m4` | float 0-100 | 15 | % |
Appended in `params/macros.h`, locked in `tools/param_ids/macros.lock`, all five in `performanceParameterIDs()` (excluded from Random/Surprise/Undo). Naming: replaces `macrosOn`, `macroActivity`, `macroChaos`, `macroSpace`, `macroDestruction` in `02`/`04` (needs owner OK; regex widening to allow `_` required, see F-L01). Host-visible names stay "Macro 1 (Activity)" etc.; the display name is static (hosts cache names), the user rename is shown only inside GapFill. Old sessions: IDs absent -> off with default values -> neutral. Neutral proof: `applyMacros` returns first when `! on`; no RNG draws anywhere in the feature.

## 5 Real-time safety
Audio: <= 4 x 8 mappings, a few multiplies per block, curve evaluation by `switch`; no allocation, locks or logging. Mapping tables switch via generation-checked double buffer. Block-size invariance: block stage uses parameter values only (no PPQ), identical per block regardless of slicing because parameter changes are block-granular in every host (documented residual: a CC arriving mid-block takes effect next block). Resets: none needed (stateless); `reset()` keeps mappings.

## 6 UI
Placement per `11` Performance area (slot `PerformanceStrip`; Creative tab keeps only a small MACROS toggle until the Performance tab exists). Four knobs with names under them, MACROS LED toggle with text, Edit button. Knob tooltip: "Activity: moves Density and Fill together. Right-click to MIDI-learn." Popover is keyboard operable (Tab order target rows -> add -> reset), each control has an accessible title ("Macro 1 target 2 minimum"). Mapped controls announce "effective value 84 percent" (C-0). Reduced Motion: effective ring is static, no animated transitions. Narrow window: knobs shrink to a compact row of four; names truncated with tooltip; editor popover is the same component with a scroll area. Help section "Performance Macros" (order 1150) including the smoothing note.

## 7 MIDI/automation/CLAP/host impacts
Five automatable parameters; MIDI Learn works unchanged (CC value -> 0..1 -> param); mapping lives in `MidiLearnManager` state `MIDIMAP`. Host automation of `fl06_m*` is the intended way to script macros. CLAP/VST3/AU: standard parameters. Presets/scenes may store `fl06_*`. No latency change. Hardware feedback (LED rings) is F-L24, not here.

## 8 Tests
Group `Macros` (C-2's list plus additions): `macros_off_is_identity` (memcmp EngineSettings and golden) | `macro_value_zero_and_max_hit_min_max` | `each_curve_monotonic_and_bounded` | `macro_never_exceeds_target_range` | `macro_does_not_write_base_parameters` | `lower_index_macro_wins_shared_target` | `invalid_subtree_dropped_atomically` | `macros_roundtrip_state` | `old_state_loads_with_macros_off` | `macro_width_output_applied` | `macro_midi_learn_drives_value` | `macro_effective_values_published_for_ui` | `macro_names_sanitised` | `default_mappings_match_table` | `invert_swaps_min_max_and_is_monotonic_down` | `reset_macro_restores_defaults_only_that_macro` | `target_table_order_is_frozen` (hash of the 21 IDs) | `mapping_double_buffer_no_torn_read` (TSan) | `macro_no_audio_thread_allocation` | `macro_block_size_invariant`. Parameter-fuzz covers `fl06_*`.

## 9 Dependencies on other tickets
Hard: B3/F0-5 (`EngineSettings`, `applyBlock`, `MacroOutputs`), F0-6, F0-9, C-0 effective-value display (S-size prerequisite, `F-S05`). Soft: F-L07 scenes store macro values; F-L24 feedback. Build order: C-0/F-S05 -> F-L06 -> F-L07.

## 10 Effort & risks
Effort L (XL in C-2 terms; split into engine, parameters+state, editor popover). Risks: spec deviation on smoothing (documented); parameter ID rename vs `04`; popover editor complexity (largest UI item, can ship later than the engine since defaults work); hosts that cache parameter names.

## 11 Acceptance checklist
- [ ] Five IDs appended and locked; excluded from Random/Surprise/Undo; ID regex widened
- [ ] Macros off is bit-identical; default mappings equal the C-2 table
- [ ] Subtree validation atomic, bounded, fuzzed
- [ ] Polarity, curves, conflict rule, refusal message implemented
- [ ] Effective-value rings with non-colour cue; accessible names; narrow layout
- [ ] MIDI-learn and host automation drive macros (manual REAPER test)
- [ ] TSan, AllocGuard, block-size invariance green
