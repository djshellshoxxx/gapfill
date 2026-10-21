# WP6 - Live control progress

Branch: `worktree-agent-a9e782cfe0edd35ae` (not pushed). Tickets in order: X-1, N-USE-a, F-L07, N-VAL, F-S08, F-L25, C-2,
F-M23, F-M18, F-L24, then the E-1-dependent ones (F-M10, F-S07, F-P02).

## Status

| Ticket | Status | Notes |
|---|---|---|
| X-1 | Done (core) | `features/commands/CommandMap.*`: 48 bindings (CC/Note/PC x Trigger/Momentary/Toggle), single MIDI scan, learn, `<Cmd>` subtree. |
| N-USE-a | Done (core) | `features/panic/Panic.*` + footer chip (`gui/slots/PanicSlot.cpp`) + `<Perf>` options. |

## Deviations and gaps (honest list)

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
