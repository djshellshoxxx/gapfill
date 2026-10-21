# Completeness and wiring audit - 2026-10-21

Scope: the integration branch with work packages WP3-WP11 merged (every feature in `docs/dev/features/*` and `docs/dev/12-SPEC-BACKLOG-AND-NEW.md`).
Question: is every specified feature complete AND wired - engine, parameter ids, registration, ParamInfo, Help, GUI control, state, command/MIDI,
Random/Surprise/Undo classification, tests, docs? Method: a mechanical script (`tools/audit/completeness_audit.py`, run in this audit and kept for CI use),
plus manual reading of the code paths that a script cannot judge. Verified by build and test, not by listening: nothing here was heard in a DAW.

## Result in one paragraph

216 parameter ids (66 core + 150 feature ids in 43 groups), 32 command ids, 14 state subtrees, 75 feature tickets. The engines were complete; the gaps were at the
edges: controls that did not exist for implemented features, five inputs that nothing fed, two documented behaviours that were not true, and non-deterministic
UI ordering. Fixes 1-12 below are code and Help text, 13-15 are documents, tooling and tests; 8 script findings are waived with a recorded reason; 14 numbered gaps remain, each with its reason. The ParamInfo allow-list is now empty.
`python3 tools/audit/completeness_audit.py` exits 0 and fails if any of these regress.

Totals (`IMPLEMENTATION-STATUS.md`): tickets yes 62, partial 8, no 4, out of scope 1.

## What the script checks (and what it found)

| Check | Rule | First run | Now |
|---|---|---|---|
| `lock_file` | each group's X-macro ids equal `tools/param_ids/<group>.lock`, group order matches `GROUP_ORDER` | clean | clean |
| `param_info` | every id has a ParamInfo entry, no stale allow-list line | 34 ids on the allow-list (WP8 voice effects + `fl01_lock`) | 0 |
| `param_gui` | the id literal appears in a GUI source (a control exists) | 10 ids | 4 waived (`fm10_*` hold mirrors) |
| `param_classification` | no feature id is in the Random / Random All lists (plus a runtime test over Surprise) | clean | clean |
| `cmake_source`, `cmake_test`, `cmake_missing` | every `.cpp` under `Source/` and every `Tests/**/Test*.cpp` is compiled by a fragment, no fragment names a missing file | clean | clean |
| `feature_tests` | each source fragment has tests | `recipes` (waived: covered by TestAudition/History/Export/AbCompare) | 1 waived |
| `registrar_reachable` | a file defining a static Slot/Help/ParamInfo registrar is compiled | clean | clean |
| `slot_order`, `help_order` | no two components of one slot share an order, no two Help sections share an order | 2 ties | 0 |
| `slot_reachable` | the editor gives every used slot a rectangle | clean | clean |
| `command_handler` | every `CommandType` has a producer or handler | `SpectralCapture`, `SpectralRelease` had none | handled |
| `command_map_range` | every command id is bindable in the X-1 map | 31, 32 (Skip) | 2 waived |
| `state_handlers` | every `<subtree>` row has validate/apply/serialise/reset | `Learn` (reserved, empty) | 1 waived |
| `ticket_status` | every feature spec and doc-12 ticket has a status row | 75 rows were mostly `no` (stale) | rewritten |
| `help_section` | feature groups have Help text | clean | clean |

## Matrix

`ok` = present and wired; `FIXED` = gap closed in this audit; `n.a.` = not applicable; `GAP` = remains (see the section after the matrix).
Columns: Eng engine; Par parameter ids (lock file); Info ParamInfo/tooltip; Help section; GUI control (Simple/Advanced rule in brackets where not always visible);
State persistence; Cmd command map / MIDI; R/U Random-Surprise-Undo class; Tst tests; Doc docs rows.

| Ticket (feature file) | Eng | Par | Info | Help | GUI | State | Cmd | R/U | Tst | Doc |
|---|---|---|---|---|---|---|---|---|---|---|
| E-1 Audition / F-M01, U-3 / F-S04 | ok | n.a. | n.a. | ok 5010 | AUDITION button ok; FILE > Audition FIXED; Options trigger note/channel/learn rows FIXED | `<Perf>` trigger ok | 1, 30; note + CC trigger ok | n.a. | ok | ok |
| E-2 tempo sync | ok | ok | ok | ok 5100 | Options fallback BPM ok | `<Perf>` fallbackBpm ok | n.a. | performance | ok | ok; `FreeTimingFallback` health input FIXED |
| E-3 reasons / F-M02, F-M03 timeline | ok | n.a. | n.a. | ok 3000 | legend + timeline ok | `<Health>` zoom/fold ok | n.a. | n.a. | ok | ok; timeline does not grow the editor (GAP 9) |
| F-S01 CPU, N-USE-b health | ok | n.a. | n.a. | ok 3000 | footer + details ok | `<Health>` muted codes | n.a. | n.a. | ok | PanicActive verified wired; FreeTimingFallback FIXED |
| N-EASY Fix-it | ok R1-R13 (R8 `fl01_lock` and R11 `qualityMode` verified live against the real parameters) | n.a. | n.a. | ok 3000 | APPLY / NOT NOW / UNDO ok | `<Health>` muted rules | n.a. | via Undo snapshot | ok | ok |
| F-D02 event capture / replay | ok | n.a. | n.a. | ok | diagnostics drawer ok | files only | 29 ok | n.a. | ok | ok |
| C-5 Lock / F-L01, C-11 / F-L03 | ok | ok | FIXED | ok 5020 | LOCK switch + LOCK THIS + MORE LIKE ok | `<Lock>` ok | 3,4,5,6 ok | performance; Undo/A-B no longer flip it FIXED | ok | ok |
| U-1 History / F-L02, U-2 Export / F-L04, F-S06 / F-L23 | ok | n.a. | n.a. | ok 5020-5040 | RECIPES drawer ok; FILE > Fill history / Export last fill FIXED | `<History>` ok | 2,3,28 ok | n.a. | ok | ok |
| U-6 Auto-Calibrate / F-L05 | ok | n.a. | n.a. | ok 1140 | LEARN card ok; Options length (4-15 s) and apply-automatically rows FIXED | global settings ok | n.a. | n.a. | ok | ok |
| C-1 personalities / F-M06, C-4 envelope / F-M05, C-6 breathing / F-M07, C-7 phrase / F-L08, F-D01 budget, C-3 mistakes / F-M13, C-8 evolve / F-L16, C-9 band type / F-M09, C-10 capture v2 / F-M08 | ok | ok | ok | ok 5110-5190 | SHAPE popover + band-lane combos + CAPTURE card ok | params | 7, 8 ok (capture) | performance | ok | ok |
| C-0 effective display / F-S05 | ok | n.a. | n.a. | ok | knob markers ok | none | n.a. | n.a. | ok | ok |
| X-1 command map, F-L24 controller profiles | ok | n.a. | n.a. | ok 6240 | Options profile + LED device + Fill Now timing ok | `<Cmd>` ok | 1-30 bindable; 24/25 handler FIXED; 31/32 not bindable (waived) | n.a. | ok | ok |
| N-USE-a Panic / F-S12 | ok: voices, preview, scrub, hold voices, bus cards (`fadeOut`), MIDI notes, wet gain ramp all verified in `panicWet()` / `onPanicAction` | n.a. | n.a. | ok 6200, 6205 | footer chip ok; large Performance-tab PANIC button FIXED | `<Perf>` options ok | 9, 10 ok | not saved (tested) | ok | ok; chip is 22 px high (GAP 7) |
| F-L25 live safety | ok | ok | ok | ok 6205 | WET SAFETY switch FIXED; LAST GOOD SCENE button FIXED | n.a. | 11 with arg -1 ok | performance | ok | ok |
| F-L07 scenes / F-M11, N-VAL bank | ok | n.a. | n.a. | ok 6220 | scene panel, EXPORT / IMPORT / UNDO ok | `<Scenes>` ok | 11-14 ok | n.a. | ok | ok |
| C-2 macros / F-L06, F-M23 | ok | ok | ok | ok 6230 | knobs + mapping editor ok; per-mapping smoothing and LFO row FIXED | `<Macros>` ok | n.a. | performance | ok | ok |
| F-M18 Beat Battles | ok | ok | ok | FIXED 6225 | BATTLE card (switch, STORE A / B, CLEAR, status) FIXED | session only (styles) | n.a. | performance | ok | ok; header A/B does not feed it (GAP 6) |
| F-M10 holds, F-S07 Storm, F-S08 Roulette, F-P02 Fill Now | ok | ok | ok | ok 6210 | HoldPads ok (pads drive commands; `fm10_*` hold bools are automation mirrors, waived) | n.a. | 17-23 ok | performance | ok | ok |
| FX-0 shell, F-L14 multiband | ok | ok | FIXED (`bandLowFx` ...) | ok 4060 | Effects section + lane switches ok | `<Fx>` ok | n.a. | performance | ok | ok |
| N-FX1 Swell, N-FX2 Cloud, F-M16 Ratchet, F-M14 Crush, F-M20 Tape | ok | ok | FIXED (34 ids) | ok 4000-4060 | effect cards ok | n.a. | n.a. | never randomised (tested) | ok | ok |
| F-M19 Freeze and Scrub | ok; snapshot length now follows `captureLength` FIXED | ok | FIXED | ok 4070 | ScrubStrip ok | n.a. | n.a. | performance | ok | ok |
| SP-0, F-M15 Resonator, F-L19 FS delay, F-L15 Dub, F-L21 Convolution, F-L20 Formant, F-X02 Spectral freeze | ok; Scale Lock reaches resonator + formant through `EngineBlockIO.scaleMask` (verified) | ok | ok | ok 4080-4130 | effect cards ok | `<Fx>` auto-capture flags | 24 / 25 handler FIXED | never randomised (tested) | ok | ok; F-L20 per-voice design GAP 1 |
| F-X01 Harmonic granular | ok | ok | ok | ok 6090 | HarmonicStrip (Advanced) ok | n.a. | n.a. | never randomised | ok | ok; not scene-recallable (cap) |
| N-MUS Scale Lock / F-L18, F-X03 key detection | ok | ok | ok | ok 6010, 6040 | ScaleStrip, KeyStrip ok; Key window + Min confidence knobs FIXED | `<Key>` ok | n.a. | never randomised | ok | ok |
| F-L22 energy, F-L09 groove, F-L10 call/response, F-X04 arrangement, F-X05 taste | ok | ok | ok | ok 6020-6080 | strips (Advanced) ok | `<Groove>`, `<Taste>` ok | 26, 27 ok; 31, 32 not bindable (waived) | never randomised | ok | ok; GAP 3, 5 |
| F-M21 tension | ok; FX lane now scales the effect Amounts FIXED | ok | ok | ok 6070 | TensionStrip ok | `<Tension>` ok | n.a. | never randomised | ok | ok; curve editor popover GAP 2 |
| F-L11 MIDI output | ok (build option, default OFF) | ok | ok | ok 6400 | Routing card ok; Options offset row FIXED | `midiOutOffsetMs` persisted in `<Perf>` FIXED | n.a. | performance | ok | ok |
| F-M17 wet-only, F-L12 Wet bus, F-L13 sidechain fills | ok | ok | ok | ok 6400 | WET ONLY / SC gate / SC snap ok | n.a. | n.a. | performance; preserve rule FIXED | ok | ok; Mix-panel selector + footer chip GAP 8 |
| F-D03 quality modes (F-X07), F-X06 co-op | ok | ok | ok | ok 6400 | QUALITY / CO-OP cards ok | n.a. | n.a. | performance | ok | ok; co-op subset GAP 4 |
| U-4 / F-M12 browser, F-S11 favourites, U-5 view, U-7 tour, U-8 scale, F-D04 access, F-M22 suggest, F-L17 Mutation Lab, U-10 / F-S10 presets | ok | n.a. | n.a. | ok 1010-1300 | ok; FILE > favourite the current preset FIXED | settings file / root `viewMode` / `<Mutation>` | n.a. | n.a. | ok | ok; GAP 10 |
| P-01, F-P01, F-P03 | out of scope | - | - | - | - | - | - | - | - | ADR 0001, 0002 FIXED |

## Fixed in this audit

Parameters and registration
1. `Source/features/info/FxInfo.cpp`: ParamInfo (tooltip, one line, range, unit) for the 34 ids that were allow-listed (`fl01_lock`, swell, cloud, ratchet, rate crush, tape destruction, scrub, band switches). `Tests/param_info_allowlist.txt` is empty.

Behaviour that was documented but not true
2. **Undo and the header A/B flipped performance state.** `docs/dev/02` F0-1 and the parameter comments say Lock, holds, scrub, battle, quality, routing ... are excluded from Undo snapshots; `undo()` and `toggleAB()` restored the whole tree. New `replaceParameterStateKeepingPerformance` keeps the current value of every `performanceParameterIDs()` id (Undo and A/B).
3. **F-M17 preserve rule.** Wet-only is kept by factory presets, Reset, Undo and `.gapfill` file load (`routingParameterIDs()`); the Help already promised this. A DAW session restore still applies it.
4. **F-M21 FX lane did nothing.** `perf.fxSend01` was computed and never read. `ContextState::tensionFxSend` + `fx::applyEffectSend` scale every effect Amount once per block (identity while Tension is off).
5. **F-M19** scrub snapshot length is `captureLength` (it was a fixed 1000 ms; same at the default).
6. **X-1 commands 24/25** (Spectral capture / release) had a name in the command map and no handler: `FxChain::handleCommand`.
7. **E-2 / N-USE-b** `FreeTimingFallback` health input was never set (tempo feature on, no valid transport, no host tempo seen lately).
8. **Non-deterministic UI order.** AUDITION and SHAPE shared order 110 in `CreativeExtra`; the Panic chip and the status line shared 100 in `FooterStatus`. Order between equal keys follows link order, i.e. differs per platform. Now 110/120 and 90/100.

Controls that did not exist for implemented features
9. Performance tab: PANIC / RECOVER button (36 px high, N-USE-a "large hit target"), LAST GOOD SCENE, WET SAFETY (`LiveSafetySlot.cpp`); BEAT BATTLE card (`BattleSlot.cpp`); macro per-mapping smoothing and the LFO row; Key window and minimum confidence knobs.
10. Options page (now a scrolling Stack inside a viewport; before, four rows were squeezed into ~180 px by an even split): Auto-Calibrate length and apply-automatically, MIDI output offset (persisted in `<Perf>`), Fill Audition trigger note / channel / learn.
11. FILE menu: Audition a fill, Fill history..., Export last fill..., Add preset to favourites (`ui::UiState::EditorActions`).
12. Help: "Live safety" (6205), "Beat Battles" (6225), macro smoothing/LFO text, Options "More options", wet-only preservation text.

Docs and tooling
13. `IMPLEMENTATION-STATUS.md` rewritten (75 rows, was 5 yes / 64 no), README feature list and source layout, COMPLIANCE (file export), TEST-TRACEABILITY (stale F-X01 row, audit rows), MODULES (audit additions), PROGRESS, ADR 0001 / 0002 (F-P01 / F-P03).
14. `tools/audit/completeness_audit.py` (+ `--selftest`).
15. `Tests/features/TestAudit.cpp` (16 tests) and `tools/test_completeness_audit.py` (3 tests) pin each fix and keep the audit green in CI.

## Remaining, with reasons

1. **F-L20 per-voice formant** (`fl20_*`, Preserve, Detail, formant pool): the doc-12 registry decided a BUS card with 4 ids; building the feature-file design needs new parameter ids and a decision. Latency 21.3 ms, not 16 / 10.7 ms.
2. **F-M21 curve editor popover**: Custom curves are editable only through the saved `<Tension>` subtree. Needs a visual design review.
3. **F-X05**: no Skip button in the History drawer (the Taste strip has Like / Skip); M1 is +10 points, not +15 (model limit, documented in WP7). Skip ids 31/32 are not MIDI-bindable (controller-profile schema v1 lists 1-30).
4. **F-X06 co-op**: safe subset only (no rendezvous-hash slots, priority / yield, group scene recall).
5. **F-X04** prediction accuracy +-2 s, not +-1 frame (detection lag).
6. **Beat Battle styles** are stored by the card; the header A/B does not feed them (A/B lives in `PluginEditor`, a hot file); styles are session state.
7. **Panic footer chip** is 22 px high (the footer height set by WP3); the 36 px button in the Performance tab satisfies "large hit target"; growing the footer needs a visual review of every tab.
8. **F-M17 Mix-panel OUTPUT selector and footer "WET ONLY" chip**: the switch is in Performance > Routing with a status line. A hidden wet-only state is guarded by the preserve rule instead.
9. **F-M03** timeline expands over the gap view, the editor does not grow.
10. **F-S11** Ctrl/Cmd+D and "favourites only" step scope; **F-S06** FILE "Compare" group (the drawer has the A/B row).
11. **T-01, T-02, W-01, Q-01** (QA / GUI-polish tickets, `no` in the status file): T-01 needs the meter arithmetic extracted, T-02 needs screenshots, W-01 is Windows-only, Q-01 (`tools/qa`) belongs with the robustness audit. **W-03** partial (full matrix not run).
12. **F-L11 extras** (Hit / CC events, velocity mode) and **F-L12** `fl12_main_mode` are not built: the doc-12 registry decided the id set (DECISIONS 18).
13. Scenes: the four `hg*` ids are not recallable (128-id file cap), `wetSolo` / `qualityMode` are excluded by design.
14. **Not verified here** (no host, hardware or display available): real DAW behaviour of every feature (REAPER pin mapping, Bitwig, Cubase, FL, Live), MIDI hardware and LED feedback, drag-export into a DAW, pluginval / clap-validator / auval with the new parameters, Windows and macOS builds, audibility of every effect and preset, screenshots at minimum / default / large size of the new cards and the scrolling Options page, screen-reader behaviour of the new controls, CPU budgets on the reference machine.

## Waived by the script (`KNOWN_EXEMPTIONS`, each with its reason in the file)

`fm10_stutter/freeze/reverse/tape` (no direct control: pads drive the commands), `Learn` subtree row (reserved, empty), `SkipLatest/SkipRecipe` (not bindable), `recipes` (tests live in other fragments).

## Verification

Build and test results of this audit are in the hand-off and in `PROGRESS.md` (session entry of 2026-10-21). `Tests/golden` and the state fixtures were not touched; no tolerance was loosened. One existing assertion was corrected rather than loosened: `ShapeGui.slots_register_the_shape_button_and_the_tempo_option` looked for SHAPE at order 110, which AUDITION also used (so it passed by accident); it now looks for 120.
