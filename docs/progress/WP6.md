# WP6 - Live control progress

Branch: `worktree-agent-a9e782cfe0edd35ae` (not pushed). Tickets in order: X-1, N-USE-a, F-L07, N-VAL, F-S08, F-L25, C-2,
F-M23, F-M18, F-L24, then the E-1-dependent ones (F-M10, F-S07, F-P02).

## Status

| Ticket | Status | Notes |
|---|---|---|
| X-1 | Done (core) | `features/commands/CommandMap.*`: 48 bindings (CC/Note/PC x Trigger/Momentary/Toggle), single MIDI scan, learn, `<Cmd>` subtree. |
| N-USE-a | Done (core) | `features/panic/Panic.*` + footer chip (`gui/slots/PanicSlot.cpp`) + `<Perf>` options. |
| F-L07 | Done (core) | `features/scenes/*`: `SceneModel` (sparse scenes, scopes, bank, `<Scenes>` codec), `SceneOverlay` (quantiser + audio-thread `SceneEngine`), `SceneController` (message thread, commit). Recall is an overlay applied in the decision stage (`Effective::applyScene`), committed to the APVTS with host gestures after the boundary. |
| F-S08 | Done | `features/live/Roulette.*`: spins the existing Surprise profiles through `surpriseParameters` (Undo snapshot + randomisable rules inherited), injected RNG, no immediate repeat, command 18. |
| F-L25 | Done (core) | `wetSafety` param (group `safety`), `panic/WetSafety.h` soft limiter after the width stage, SceneGoto(arg -1) = last good scene. |
| C-2 | Done (engine + state) | `features/macros/MacroEngine.*` (curves, mapping, runtime, factory table), `MacroModel.*` (rules, `<Macros>` codec, 2 KiB), params `macroActivity/Chaos/Space/Destruction`, `macrosOn` (off), `Effective::applyMacros`. Macros own a target (`perf.macroOwnedMask`); the Space macro drives `widthAmount` through `perf.macroWidth01`. |
| F-M23 | Done (engine + state) | Targets 21..35 (effect Amount ids) appended to the table, per-mapping `smoothMs` and `bipolar`, one LFO per macro (sine/tri/saw/square/seeded S&H, Hz or division), PPQ-locked decision-stage re-evaluation `Effective::applyMacroLfos`. |
| F-M18 | Done (engine) | `features/battle/Battle.*`: `battleEvery` param (group `battle`), `BattleModel` (two styles restricted to the scene whitelist), audio `BattleRuntime`, `Effective::applyBattle`. Overlay only. |
| N-VAL | Done (core) | `features/bank/BankFile.*`: `.gapbank` export/import (256 KiB, 64 scenes, SHA-256 digest, atomic apply, one-step undo), PC -> scene and Next/Prev/Cancel commands. |

## Deviations and gaps (honest list)

* **F-M18 style source**: the header A/B snapshots live in `PluginEditor` (WP11-owned hot file), so Battle keeps its own two style slots
  (`LiveControl::battles().setStyleFromState / setStyleFromCurrent`). The header A/B code needs one call per store to feed them
  (`setStyleFromState (0/1, apvts, slotA/slotB)`); until then the Performance panel stores "current sound" into A or B. The styles are
  session state (like the header A/B) and are not written to the project.

* **Macro editor UI** (4 knobs, MACROS LED, mapping popover) is not built: it needs the `PerformanceStrip`/Performance-tab rectangle that
  UI-0 (WP11) reserves. All behaviour is reachable through `LiveControl::macroMappings()`, MIDI learn on the four value parameters and
  host automation.
* F-M23 targets 21..35 are `Unavailable` until the owning feature adds its Amount parameter (`MacroModel::addMapping` checks the layout).
  Their effective value is computed and published (`getMacroEffective01`) but only becomes sound when that feature's gather stage reads it.
* The block stage has no PPQ yet (transport is read later in the chunk), so a tempo-synced LFO is evaluated at the free-run clock there and
  re-evaluated at the PPQ of every scheduling decision; continuous targets (width, FX amounts) therefore follow the block-rate value.
* Fixtures: `StateCompat.session_extreme` toggled *every* parameter; it now toggles the 66 core parameters the 0.0.1 fixture knows.

* **wetSafety**: with the spec's formula and c = 0.89 the deviation from unity is 0.086 dB at -8 dBFS and 0.21 dB at -6 dBFS, so the
  spec's "< 0.1 dB below -6 dBFS" is met below -8 dBFS only (tests assert exactly that). The "overload fallback" part of F-L25 depends on
  F-D03 `qualityMode = Auto` / N-EASY R11 (other work packages) and is not implemented here.
* `TestFoundations.parameter_set_and_order_are_unchanged_by_the_group_split` asserted exactly 66 parameters; it now asserts >= 66 with the
  same first/last core ids (the 66 core ids and their order are still checked one by one). Every work package that adds a parameter needs
  the same change, so expect an identical hunk to merge.

* **Scene storage**: scenes store only parameters that differ from the default (that is what "sparse" means); recall sets EVERY in-scope
  parameter (stored value, else default). With full 6-byte entries 64 typical scenes (~36 changed params) take ~19 KiB of the 24 KiB cap;
  a bank that cannot fit is truncated in project state (never corrupted) and `toValueTree` reports `truncated` so the UI can say
  "export the bank to keep it". Files use the same sparse form.
* **Scene scopes** are built from the group X-macros (`GAPFILL_PARAMS_<GROUP>`), so the `fx`/`musical`/`macros` scopes grow
  automatically as WP3-WP8 fill their groups. With every group populated the default scope covers ~121 ids (<= 128 limit); the
  opt-in scopes together can exceed 128, in which case a scene is refused with a clear message instead of being truncated.
* Overlay entries are applied for the parameters that map to an `EngineSettings` field (fill, intensity, complexity, chaos, density,
  fillType, weights, protection, bands, detector timings). Parameters without such a field (effect amounts, scale, ...) are applied at the
  commit (APVTS write ~one timer tick after the boundary) and by their own stages once they exist; there is nothing to ramp yet
  (`click_free_fx_amount_ramp` is pending the FX-0 cards).
* A scene recalled while macros own a target does not override the macro (`macroOwnedMask`), matching "macros own the effective value".

* The processor (`PluginProcessor.h/.cpp`, hot files) gained ~12 hook lines (live-control hub member, `beginBlock`, `handleCommand`,
  `prepareChunk`, per-sample panic gain, timer `pump`). No seam existed for a wet-bus gain, so this is the minimum; every line is a
  call into `LiveControl`. `FillEngine.cpp` got the one `Panicked` early return the spec reserves. `PluginProcessor` edits need the
  `hot-file-ok` label.
* `<Perf>` is one shared subtree: WP6 writes only its own attributes (`recovery`, `panicToggles`) and reads only those. Other owners
  of `<Perf>` must merge their attributes into the same row functions in `StateSubtrees.cpp` (one-line conflict expected).
* Footer height is 12 px (WP11 owns `PluginEditor.cpp`), so the Panic chip's hit target is 12 px high instead of >= 32 px; the large
  PANIC button belongs to the Performance tab, which does not exist until UI-0.
* MIDI Learn (CC -> parameter) is unchanged; the only addition is `MidiLearnManager::getParameterIDForCC` for the duplicate check.
  Bindings loaded from state are not re-checked against CC parameter mappings (only at learn time).
* Panic clearing "all FX cards" is not testable yet: the FX-0 effect cards (WP8) do not exist; `LiveControl::onPanicAction` is the
  single place to call `fadeOut/reset` on them.
