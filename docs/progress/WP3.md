# WP3 - Explain & assist progress

Branch: `worktree-agent-a4d4aba1514bc7216` (not pushed), based on `claude/inspiring-fermi-pv9gv5` (Phase 0 seams).
Tickets: E-3, F-S01, N-USE-b, N-EASY, F-M03, F-D02 (all implemented). No new parameters, no new parameter IDs, no golden changes.

## Status per ticket

| Ticket | Status | Notes |
|---|---|---|
| E-3 reasons + legend | Done | Engine side was mostly in place from F0 (`RejectReason`, `reasonCount`, `fillEvents`). Added: `VizFrame` reason/availMs/needMs, `kEnterGapConf` exported from `FillEngine.h`, threshold line + hollow skipped markers + hover tooltip in `WaveformGapView`, `features/reasons` (wording, top-3, 4 Hz limiter, legend strip), "Why so few fills?" in the status details. |
| F-S01 CPU / latency | Done (per-card cost deferred) | `CpuMeter` (EMA, block-size independent), labels always say "estimated". `meters.fxCpu` does not exist until FX-0 lands. Shown in the status details (not the Options page, see deviations). |
| N-USE-b health footer | Done | 10 codes, hysteresis, one atomic mask, priority, mute list in the `Health` subtree, footer + detail view. `FreeTimingFallback` (E-2) and `PanicActive` (N-USE-a) inputs are plumbed but nothing sets them yet. |
| N-EASY fix-it | Done | R1-R13, controller timings per spec, Apply/Undo through the existing undo snapshot, whitelist, R10 confirm. R8 (`fillLock`) and R11 (`qualityMode`) are inactive until those parameters exist. |
| F-M03 timeline | Done (height growth deferred) | 32-byte event record, 512 ring, engine events Onset/GapOpen/FillEnd, striped "incomplete" cap, pause/zoom, keyboard-reachable text list, Reduced Motion at 4 Hz. Expands over the gap view instead of growing the editor (see deviations). |
| F-D02 event capture/replay | Done | 24-byte records, `.gfevents`, `replay()` harness, diagnostics drawer, Command 29, Standalone "Open replay...". |

## Deviations from the specs (code reality) and honest gaps

* **Hot files were touched** (the WP "Owns" list is feature folders only, but the data path needs a few lines): `PluginProcessor.cpp/.h` (one `ExplainState` member, `BlockTimer`, `onChunk`, `onCommand`, drain filter, timer tick, tap attach), `FillEngine.cpp/.h` (timeline events, reject flags in the viz frame, `kEnterGapConf`), `PluginEditor.cpp` (`kFooterHeight` 12 -> 22 so the status line and its buttons are clickable; `openOptions` wiring for fix-it R12). Needs `hot-file-ok` on the PR. Other non-hot shared files: `EngineEvents.h` (new `FillEventType`s, `FillEvent` fields, `FillEventTap`, `pushOptional`), `VisualisationState.h`, `WaveformGapView.*`, `StateSubtrees.cpp` (the `Health` row).
* **`<Health>` holds more than muted codes**: also muted fix-it rules, "Show suggestions", the legend fold state and the timeline zoom. The spec put the legend state in `<Perf>`, but `<Perf>` has several future owners; one owner per subtree avoids a merge fight. 1 KiB cap respected (about 120 bytes).
* **Event recorder rings**: spec says two SPSC rings (audio producer + message-thread producer for parameter edits). Host automation can call parameter listeners on either thread, so parameter edits are instead sampled on the audio thread at each block start (a diff against the previous values): one producer, block-exact, and exactly what the replay re-injects. Ring capacity 65,536; the **recording cap is 60,000 records** so a saved file (base64) always stays below 2 MiB (65,536 would give 2.1 MB).
* **Replay semantics**: records `transportValid` (host playing + valid tempo + valid position), not the raw host fields; `PreviewRecipe`-style recipe payloads are not recorded (type/index only). Replay re-anchors ppq on Transport records and advances it with the audio. The Standalone "Open replay..." button runs against built-in test audio, so its outcomes differ from the recording by design (the dialog says so); the test harness replays against the recorded audio and is identical.
* **Timeline owner**: the spec calls `TimelineModel` UI-owned; it lives in the processor's `ExplainState` so history survives the editor being reopened.
* **Timeline "expands in place (height x2)"**: the editor layout is WP11 / UI-0. The expanded view covers the gap view below the legend row (collapsed by default, min-size safe, tested at 1020 x 620). Height growth is for UI-0.
* **Options page**: latency/CPU, "Show suggestions" and diagnostics are in the status details call-out (`health::DetailsPanel`); embedding it in the Options page needs a rectangle in `OptionsView` (UI-0, OptionsExtra slot).
* **Slot sharing**: `SlotHost` splits a slot between all its components, so WP3 registers exactly ONE component each in `VisualiserOverlay` (legend + timeline toggle + timeline) and `FooterStatus`. If another package registers a second component in either slot, widths will be split; coordinate through the composite or give the slot a layout policy in UI-0.
* **NoTransport noise**: `tempoFeatureOn` is `syncEnable || maxFillSync || perf.quantDivision >= 0 || perf.modSync`. `syncEnable` defaults to ON, so a Standalone run without transport shows "Transport unavailable - free timing" after 1 s. That is true (stutter/delay lengths use the fallback tempo) and the code is mutable; E-2 may refine it.
* **Fill-event queue**: Onset/GapOpen/FillEnd use `pushOptional` (never the last 16 of 64 slots). Outcome/recipe events keep the old behaviour (drop newest + count). `QueueOverflow` only counts lost outcomes; lost timeline-only events mark the timeline incomplete.
* **Commits**: one commit per ticket was not possible because the six features share `ExplainState`, the processor hooks and the engine event path; the code is one commit, documentation another.

## Not verified here

Real DAW or hardware (every health code reproduced in a host, M-3 manual script, latency equal to host PDC), Windows/macOS builds, pluginval/clap-validator (not installed), audibility (nothing here changes sound; the golden tests are bit-identical), Standalone "Open replay..." dialogs (needs a display + user interaction), screen-reader behaviour of the call-out and list (accessibility titles/descriptions are set; not exercised with a reader).

## Measured

* Golden scenarios, state fixtures and all earlier tests unchanged (bit-identical neutral output).
* New tests: see `docs/TEST-TRACEABILITY.md` (84 added).
