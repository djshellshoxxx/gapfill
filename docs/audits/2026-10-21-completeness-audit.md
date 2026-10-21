# Completeness and wiring audit - 2026-10-21

Scope: the integration branch with work packages WP3-WP11 merged (every feature in `docs/dev/features/*` and `docs/dev/12-SPEC-BACKLOG-AND-NEW.md`).
Question: is every specified feature complete AND wired - engine, parameter ids, registration, ParamInfo, Help, GUI control, state, command/MIDI,
Random/Surprise/Undo classification, tests, docs? Method: a mechanical script (`tools/audit/completeness_audit.py`, run in this audit and kept for CI use),
plus manual reading of the code paths that a script cannot judge. Verified by build and test, not by listening: nothing here was heard in a DAW.

## Result in one paragraph

216 parameter ids (66 core + 150 feature ids in 43 groups), 32 command ids, 14 state subtrees, 75 feature tickets. The engines were complete; the gaps were at the
edges: controls that did not exist for implemented features, five inputs that nothing fed, two documented behaviours that were not true, and non-deterministic
UI ordering. Fixes 1-12 below are code and Help text, 13-15 are documents, tooling and tests; 8 script findings are waived with a recorded reason; 14 numbered gaps were left, each with its reason. The ParamInfo allow-list is now empty.
A gap-closing pass followed the same day (section "Closed by the gap-closing pass"): gaps 2, 3 (Skip button), 6, 7, 8 and 10 (Ctrl/Cmd+D and favourites-only stepping) are closed in code, gap 11 is closed as far as the repository can (T-01 extraction and tests, the Q-01 harness, the W-03 test matrix, a T-02 ratchet; W-01 and the T-02 visual pass stay external), and gap 1 (F-L20) is recorded as a design deviation. Table cells below still read as written by the audit; the status of each numbered gap is in the final section.
`python3 tools/audit/completeness_audit.py` exits 0 and fails if any of these regress.

Totals (`IMPLEMENTATION-STATUS.md`): tickets yes 62, partial 8, no 4, out of scope 1 at the audit; yes 65, partial 8, no 1, out of scope 1 after the gap-closing pass (counted by the script).

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
| N-USE-a Panic / F-S12 | ok: voices, preview, scrub, hold voices, bus cards (`fadeOut`), MIDI notes, wet gain ramp all verified in `panicWet()` / `onPanicAction` | n.a. | n.a. | ok 6200, 6205 | footer chip ok; large Performance-tab PANIC button FIXED | `<Perf>` options ok | 9, 10 ok | not saved (tested) | ok | ok; chip is a full-height 22 px target (G7 closed) |
| F-L25 live safety | ok | ok | ok | ok 6205 | WET SAFETY switch FIXED; LAST GOOD SCENE button FIXED | n.a. | 11 with arg -1 ok | performance | ok | ok |
| F-L07 scenes / F-M11, N-VAL bank | ok | n.a. | n.a. | ok 6220 | scene panel, EXPORT / IMPORT / UNDO ok | `<Scenes>` ok | 11-14 ok | n.a. | ok | ok |
| C-2 macros / F-L06, F-M23 | ok | ok | ok | ok 6230 | knobs + mapping editor ok; per-mapping smoothing and LFO row FIXED | `<Macros>` ok | n.a. | performance | ok | ok |
| F-M18 Beat Battles | ok | ok | ok | FIXED 6225 | BATTLE card (switch, STORE A / B, CLEAR, status) FIXED | session only (styles) | n.a. | performance | ok | ok; FROM A/B button feeds the styles from the header A/B (G6 closed) |
| F-M10 holds, F-S07 Storm, F-S08 Roulette, F-P02 Fill Now | ok | ok | ok | ok 6210 | HoldPads ok (pads drive commands; `fm10_*` hold bools are automation mirrors, waived) | n.a. | 17-23 ok | performance | ok | ok |
| FX-0 shell, F-L14 multiband | ok | ok | FIXED (`bandLowFx` ...) | ok 4060 | Effects section + lane switches ok | `<Fx>` ok | n.a. | performance | ok | ok |
| N-FX1 Swell, N-FX2 Cloud, F-M16 Ratchet, F-M14 Crush, F-M20 Tape | ok | ok | FIXED (34 ids) | ok 4000-4060 | effect cards ok | n.a. | n.a. | never randomised (tested) | ok | ok |
| F-M19 Freeze and Scrub | ok; snapshot length now follows `captureLength` FIXED | ok | FIXED | ok 4070 | ScrubStrip ok | n.a. | n.a. | performance | ok | ok |
| SP-0, F-M15 Resonator, F-L19 FS delay, F-L15 Dub, F-L21 Convolution, F-L20 Formant, F-X02 Spectral freeze | ok; Scale Lock reaches resonator + formant through `EngineBlockIO.scaleMask` (verified) | ok | ok | ok 4080-4130 | effect cards ok | `<Fx>` auto-capture flags | 24 / 25 handler FIXED | never randomised (tested) | ok | ok; F-L20 per-voice design: recorded design deviation (G1) |
| F-X01 Harmonic granular | ok | ok | ok | ok 6090 | HarmonicStrip (Advanced) ok | n.a. | n.a. | never randomised | ok | ok; not scene-recallable (cap) |
| N-MUS Scale Lock / F-L18, F-X03 key detection | ok | ok | ok | ok 6010, 6040 | ScaleStrip, KeyStrip ok; Key window + Min confidence knobs FIXED | `<Key>` ok | n.a. | never randomised | ok | ok |
| F-L22 energy, F-L09 groove, F-L10 call/response, F-X04 arrangement, F-X05 taste | ok | ok | ok | ok 6020-6080 | strips (Advanced) ok | `<Groove>`, `<Taste>` ok | 26, 27 ok; 31, 32 not bindable (waived) | never randomised | ok | ok; Skip in the History drawer (G3 closed); G3 M1 limit and G5 stay |
| F-M21 tension | ok; FX lane now scales the effect Amounts FIXED | ok | ok | ok 6070 | TensionStrip ok | `<Tension>` ok | n.a. | never randomised | ok | ok; curve editor popover (G2 closed) |
| F-L11 MIDI output | ok (build option, default OFF) | ok | ok | ok 6400 | Routing card ok; Options offset row FIXED | `midiOutOffsetMs` persisted in `<Perf>` FIXED | n.a. | performance | ok | ok |
| F-M17 wet-only, F-L12 Wet bus, F-L13 sidechain fills | ok | ok | ok | ok 6400 | WET ONLY / SC gate / SC snap ok | n.a. | n.a. | performance; preserve rule FIXED | ok | ok; Mix-panel OUTPUT selector and footer WET ONLY chip (G8 closed) |
| F-D03 quality modes (F-X07), F-X06 co-op | ok | ok | ok | ok 6400 | QUALITY / CO-OP cards ok | n.a. | n.a. | performance | ok | ok; co-op subset GAP 4 |
| U-4 / F-M12 browser, F-S11 favourites, U-5 view, U-7 tour, U-8 scale, F-D04 access, F-M22 suggest, F-L17 Mutation Lab, U-10 / F-S10 presets | ok | n.a. | n.a. | ok 1010-1300 | ok; FILE > favourite the current preset FIXED | settings file / root `viewMode` / `<Mutation>` | n.a. | n.a. | ok | ok; Ctrl/Cmd+D, favourites-only stepping, one Undo entry per burst (G10 closed); FILE Compare group stays |
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

## Closed by the gap-closing pass

Each item has tests in `Tests/features/TestGapClose.cpp` (group `GapClose`), `TestMeter.cpp` (`Meter`) or `TestTransport.cpp` (`Transport`); defaults stay neutral, `Tests/golden` and the state fixtures were not touched, no assertion was loosened. Nothing here was seen or heard: GUI geometry is asserted by bounds, not by screenshot.

| Gap | What was built | Where |
|---|---|---|
| G8 F-M17 | Mix-panel `OUTPUT: NORMAL / WET ONLY` selector under the readouts (a `GapToggle` on `wetSolo`: automation, MIDI learn, tooltip, accessible name "Output mode ..."); Dry and Wet knobs dimmed to 45 % while wet only (still readable and still hold their values); footer chip `WET ONLY` next to PANIC with `(bypassed)` / `- monitor aid active` (Band Solo) text and its own tooltip. Both follow the parameter, so host automation and the preserve rule show up. | `gui/Panels.*` (`MixPanel`), `gui/slots/PanicSlot.cpp` |
| G2 F-M21 | `TENSION > EDIT CURVES...` opens a call-out with the curve editor: lane tabs (Density, Intensity, Width, FX Send), a 480 x 160 canvas with the 1/16 grid, a 0.5 "no change" line, the curve drawn exactly as the audio thread evaluates it, draggable points (nearest within 12 px), double-click adds (max 16), right-click or Alt-click removes (end points kept), BUILD / BUILD+DROP / BREAKDOWN / SWELL copy a sampled built-in shape into the lane, RESET LANE, a playhead from `lastPhase` at 15 Hz. Keyboard: Page Up / Page Down / Home / End select, arrows move (1/64 length, 1 %, Shift = 10x), Insert adds in the widest gap, Delete removes; the selection is announced through the component title ("Density point 2 of 3, bar 4.5, 72 percent"). The first edit switches Shape to Custom (a parameter gesture) so the drawn curve is heard. The pure edit rules (`features/tension/CurveEdit.*`) keep every lane valid (20 000 random operations in the test). Tab is deliberately not used for point selection (it would trap keyboard focus). | `features/tension/CurveEdit.*`, `gui/slots/TensionEditor.*`, `TensionStrip.cpp` |
| G3 F-X05 | SKIP button in the History drawer (row 2, next to MORE LIKE) and a context-menu item: rates the selected recipe (`SkipRecipe`) or the latest fill (`SkipLatest`); with LEARN MY TASTE off it posts "Turn on LEARN MY TASTE" instead of queuing a rating the service would discard. | `features/recipes/RecipeDrawer.cpp`, `Help.cpp` |
| G10 F-S11 | Ctrl/Cmd+D toggles the favourite of the current preset (factory and user presets; a modified sound cannot be starred); Ctrl/Cmd+Up / Down step; FILE > "Arrows step through favourites only" (per-user setting `presets.stepScope`; with no favourites stepping falls back to all presets and the footer says so); footer message `Preset 12/25: name`; a burst of steps within 3 s is one Undo entry (`discardNewestUndoSnapshot`). Help section 1110. The optional MIDI Program Change responder of the feature file is not built (default off in the spec). | `PluginEditor.*`, `features/presetbrowser/PresetCatalog.*` (`favouriteIndices`, `stepWithin`) |
| G6 F-M18 | `FROM A/B` button on the BATTLE card copies the header A and B snapshots (the side on screen is captured first) into styles A and B through `ui::UiState::EditorActions::battleFromHeaderAb`. Still session state. | `gui/slots/BattleSlot.cpp`, `PluginEditor.*` |
| G7 F-S12 | The footer is 22 px high (`kFooterHeight`) and the PANIC chip is the whole footer height: a 150 x 22 px target (the WET ONLY chip beside it is 168 x 22); the 36 px PANIC button stays in the Performance tab. Growing the footer would push every tab's layout and needs a visual review, so the target is documented and pinned by a test instead. | `PanicSlot.cpp`, `TestGapClose.cpp` |
| G11 T-01 | `gui/MeterMath.h` (dB map, `PeakHold` with injected clock, gradient) extracted from `LevelMeter`, which delegates; 8 tests for the 1500 ms hold, 20 dB/s fall (+-1 dB per second), settling, restart rules, monotonic map, gradient stops. Behaviour unchanged by construction (same constants, same per-tick step). The pixel-golden item of the ticket is not done (needs per-platform images). | `gui/MeterMath.h`, `WaveformGapView.h`, `TestMeter.cpp` |
| G11 W-03 | Matrix tests for the ticket (block patterns 8192/32/4096/64/512 ..., 60 s steady playback, 60 -> 180 BPM ramp at five host block sizes, tempo step, seek, loop wrap, stop/start, small nudge, no PPQ, block-pattern independence of the wet signal). One behaviour fix from the ticket text: "playing without a PPQ" was a transport jump (and a wet panic) in EVERY callback; it is now one reset on the transition (`updateTempoInfo`). | `PluginProcessor.cpp`, `TestTransport.cpp` |
| G11 Q-01 | `tools/qa/run_matrix.py` (+ `summarise.py`, `tools/test_qa_matrix.py`, 11 tests): fast / full tiers, evidence JSON per SHA and OS, a step that executes nothing fails, a validator that is not installed is `unverified`, manual rows listed as open. Wired into no CI job yet. | `tools/qa/` |
| G11 T-02 | `tools/check_layout_literals.py` is a ratchet over `PluginEditor.cpp` / `Panels.cpp` (fails when more spacing literals that are not multiples of 4 appear; baseline `tools/layout_literals_baseline.json`). The layout pass itself needs screenshots and stays open. | `tools/` |

## Status of the numbered gaps

| # | Gap | Status | Reason / what remains |
|---|---|---|---|
| 1 | F-L20 per-voice formant (`fl20_*`, Preserve, Detail, formant pool) | **Design deviation, recorded** | Shipped as the doc-12 BUS card (`fmtAmount`, `fmtShift`, `fmtTilt`, `fmtVowel`; DECISIONS 18 keeps the doc-12 ids; latency 21.3 ms, not 10.7 ms). The per-voice design needs a formant pool inside `FillEngine` / `FillVoice` (hot files), a scheduler change (`minMs + kLatency` for every voice with formant amount > 0), six new permanent parameter ids that duplicate the four shipped ones, and a Preserve default (100) that would make the shipped Vowel setting inaudible. None of it can be judged without listening, and the bus card is already correct and tested. Preserve / Detail alone would need a new parameter group and a DSP change to a shipped card with no way to hear it here: not done. |
| 2 | F-M21 curve editor popover | **Closed** | see above |
| 3 | F-X05 Skip in History drawer; M1 +10 not +15; Skip ids 31/32 not MIDI-bindable | **Skip button closed**; M1 and binding open | M1 is a model limit (bounded by the 2x cap, documented in WP7). Ids 31/32 need a controller-profile key; schema v1 lists commands 1-30 (waived in the script). |
| 4 | F-X06 co-op beyond the safe subset | **Open, not safe** | Rendezvous-hash slots, priority / yield and group scene recall need a cross-instance protocol whose failure modes (two instances disagreeing about who owns a slot, an instance that never yields) cannot be exercised in one process; the in-process subset stays. |
| 5 | F-X04 prediction +-2 s, not +-1 frame | **Open (model limit)** | Detection lag of the section tracker. |
| 6 | Beat Battle styles from the header A/B | **Closed** | see above |
| 7 | Panic footer chip height | **Closed (documented, tested)** | see above |
| 8 | F-M17 Mix-panel selector + footer chip | **Closed** | see above |
| 9 | F-M03 timeline expands over the gap view, the editor does not grow | **Design deviation, recorded** | Growing the editor shifts every tab and the saved-size clamp; it needs the visual review of T-02. |
| 10 | F-S11 Ctrl/Cmd+D, favourites-only scope; F-S06 FILE "Compare" group | **F-S11 closed**; Compare group deviation | The drawer's SET A / SET B / A/B / MATCH row is the Compare group (same actions). MIDI Program Change (optional, default off) not built. |
| 11 | T-01, T-02, W-01, Q-01, W-03 | **T-01 mostly closed, Q-01 harness built, W-03 test matrix built, T-02 ratchet only, W-01 external** | T-01 still lacks the pixel-golden and the narrow-readout test of the ticket. W-03 still lacks the jitter measurement in two real hosts that would justify tightening the 0.25 beat tolerance (it was not changed). W-01 is a Windows-only crash (pluginval level 10, editor automation): it needs a Windows machine, a dump and symbols; the protocol and the code-reading leads are in `docs/progress/W-01.md`, nothing is claimed as a root cause. T-02 needs screenshots at three sizes before any layout value changes. Q-01 is not yet a CI job; the validators rows stay `unverified` until their tools run. |
| 12 | F-L11 extras (Hit / CC events, velocity mode), F-L12 `fl12_main_mode` | **Open by decision** | DECISIONS 18 (the doc-12 registry decided the id set). |
| 13 | Scenes: `hg*` ids not recallable (128-id cap); `wetSolo` / `qualityMode` excluded by design | **Open by design** | |
| 14 | Not verified here | **Open** | Unchanged and extended: no host, hardware or display. Also unverified for this pass: how the Mix-panel selector, the WET ONLY chip, the curve editor popover and the new drawer / BATTLE buttons look at minimum, default and large size and under 150 / 200 % scale; Ctrl/Cmd+D and Ctrl/Cmd+Up / Down inside REAPER, Live, FL, Logic and Bitwig (hosts may take those keys); screen-reader announcements of the curve editor; the TENSION call-out inside a real host window. |

## Waived by the script (`KNOWN_EXEMPTIONS`, each with its reason in the file)

`fm10_stutter/freeze/reverse/tape` (no direct control: pads drive the commands), `Learn` subtree row (reserved, empty), `SkipLatest/SkipRecipe` (not MIDI-bindable; GUI Skip is in the Taste strip and the History drawer), `recipes` (tests live in other fragments).

## Verification

Build and test results of this audit are in the hand-off and in `PROGRESS.md` (session entry of 2026-10-21). `Tests/golden` and the state fixtures were not touched; no tolerance was loosened. One existing assertion was corrected rather than loosened: `ShapeGui.slots_register_the_shape_button_and_the_tempo_option` looked for SHAPE at order 110, which AUDITION also used (so it passed by accident); it now looks for 120.
