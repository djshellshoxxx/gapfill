# WP6 - Live control progress

Branch: `worktree-agent-a9e782cfe0edd35ae` (not pushed). Tickets in order: X-1, N-USE-a, F-L07, N-VAL, F-S08, F-L25, C-2,
F-M23, F-M18, F-L24, then the E-1-dependent ones (F-M10, F-S07, F-P02).

## Status

| Ticket | Status | Notes |
|---|---|---|
| X-1 | Done (core) | `features/commands/CommandMap.*`: 48 bindings (CC/Note/PC x Trigger/Momentary/Toggle), single MIDI scan, learn, `<Cmd>` subtree. |
| N-USE-a | Done (core) | `features/panic/Panic.*` + footer chip (`gui/slots/PanicSlot.cpp`) + `<Perf>` options. |
| F-L07 | Done (core) | `features/scenes/*`: `SceneModel` (sparse scenes, scopes, bank, `<Scenes>` codec), `SceneOverlay` (quantiser + audio-thread `SceneEngine`), `SceneController` (message thread, commit). Recall is an overlay applied in the decision stage (`Effective::applyScene`), committed to the APVTS with host gestures after the boundary. |
| N-VAL | Done (core) | `features/bank/BankFile.*`: `.gapbank` export/import (256 KiB, 64 scenes, SHA-256 digest, atomic apply, one-step undo), PC -> scene and Next/Prev/Cancel commands. |

## Deviations and gaps (honest list)

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
