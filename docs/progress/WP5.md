# WP5 - Shaping and decisions progress

Branch: `worktree-agent-a0290edf613f38ecd` (not pushed). Tickets in order: E-2, C-0, C-4, C-1, C-6, C-7, F-D01,
C-9, C-10, C-3, C-8 (F-L16). Neutral defaults keep the six goldens and the 0.0.1 state fixtures identical.

## Status per ticket

| Ticket | Status | Notes |
|---|---|---|
| E-2 tempo-sync audit | Engine, state, tests, help done; UI pending | see below |

## Conventions used by this package

* Help section orders 5100-5199 are reserved for WP5 (one per ticket: E-2 5100, C-0 -, C-4 5110, C-1 5120,
  C-6 5130, C-7 5140, F-D01 5150, C-9 5160, C-10 5170, C-3 5180, C-8 5190).
* Runtime state shared with the editor lives in `ShapingRuntime` (`GapFillAudioProcessor::getShaping()`), so the
  tickets add one processor member in total (`Source/features/shaping`).
* Every WP5 parameter is added to its group's `add<Group>PerformanceIds` (excluded from Random/Surprise/Undo, C9).

## Deviations and gaps (honest list)

* Hot files touched (justified, each by a line or a hook): `PluginProcessor.h/.cpp` (`ShapingRuntime` member,
  `updateTempoInfo` tempo memory + musical clamping, C-0 publish call), `FillEngine.cpp` (stage functions, WP5-owned),
  `FillVoice.h/.cpp` (E-2 Sync Mods rates / slice length hook). Run `tools/check_hot_files.py` with the
  `hot-file-ok` label for these.
* `Tests/features/TestFoundations.cpp` and `TestStateFixtures.cpp`: the "66 parameters" assertions track the
  frozen core instead (core IDs keep index 0-65; fixtures are 0.0.1 states and only set the 66 core IDs). Not a
  weakening: the core-index and fixture-restores checks are unchanged, new groups are allowed to append.
* E-2: the Options-page control for the fallback tempo and the "effective ms next to each division" readout need a
  reserved rectangle in `PluginEditor`/`Panels.cpp` (WP11 only). The processor side is complete
  (`getShaping().tempo`, `<Perf>` subtree `fallbackBpm`, `MeterState::syncClamped`).
