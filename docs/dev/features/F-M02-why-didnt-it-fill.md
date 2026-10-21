# F-M02 "Why didn't it fill?" explanations (enhancement spec over E-3)

Status: implementation-completion spec. Canonical: `03-SPEC-ESSENTIALS.md` E-3 (reason catalogue, data path, UI, tests), `02-FOUNDATIONS.md` F0-2 (`RejectReason`, `GapContext`, `ScheduleResult`) and F0-4 (queues, reserved `VizFrame::reason`/`flagRejected`), `10-SPEC-LIVE-CREATIVE-EFFECTS.md` 4.1. This file adds what is missing. F-M02 owns the engine data, footer status and "Why so few fills?" popover. F-M03 owns the timeline drawing; both consume the same record.

## 1 Summary & user-visible behaviour
Per evaluated gap the user can see exactly why a fill happened or not, with the numbers and a one-click path to the control that fixes it. Gaps in E-3 closed here:
1. Bypass is mislabelled: `processChunk` forces `fill01 = 0` when bypassed, which would report `FillOff`. Add reason `Bypassed` (appended before `Count`), set from a `GapContext::bypassed` flag; status "Bypassed - no fills while bypassed".
2. `DensityRoll` advice needs the number: add `probPct` (0-100) to `FillEvent` and show "Rolled the dice: 38% chance".
3. `neededMs` is undefined for several reasons. Definition table in section 3.
4. Advice must be actionable: each advice names a parameter ID; clicking it reveals the control (switches tab, focuses, outlines it).
5. Confidence legend (08 wording): the confidence lane shows the 0.55 enter-gap threshold and the text "Confidence is how gap-like the audio is. It is not a promise a fill will play."
6. Optional (P2, behind `#if GAPFILL_FM02_TRUNCATED`): outcome `FillTruncated` when `requestStop` from the next-event guard cuts a started fill; shows "Cut short by Protect Next at 62%". Not part of v1 acceptance.

## 2 Success / quality metric
- Accuracy: for every reason the displayed label, `availMs` and `neededMs` equal the integers used by the decision (tests assert equality, not tolerance).
- Coverage: 100% of rejected evaluated gaps in scenarios A-F carry a non-`None` reason; `reasonCount` sum equals evaluated gaps.
- UX: in a 5-tester first-run script ("nothing is happening"), >= 4 reach the correct control within 60 s using only the status line/popover.
- Cost: editor closed -> audio-thread delta < 0.1% CPU (E-3 acceptance).

## 3 Design
- Files: `Source/features/reasons/{Reasons.h,Reasons.cpp,Advice.cpp,Gui.h,Gui.cpp,Help.cpp}`, `Tests/features/TestReasons.cpp`, cmake fragments.
- Engine (F0-2 contract): `RejectReason` gains `Bypassed` before `Count`. `FillEvent` (F0-4) gains `std::uint8_t probPct`. Table of `neededMs`:

| Reason | availMs | neededMs |
|---|---|---|
| TooShort / GuardEaten | `available` (after pre-guard) | `minGapMs` |
| NoAlgoFits (auto) | `lengthMs` | smallest `kProfiles[i].minMs` over algorithms with weight > 0.001 |
| NoAlgoFits (forced Fill Type or F-M09 band type) | `lengthMs` | `kProfiles[algo].minMs * 0.6` |
| TooSoon | ms since last fill | `minGapMs / 2` |
| others | 0 | 0 |

- Pure mapping `const AdviceEntry& adviceFor (RejectReason)`: `{ const char* label; const char* adviceText; const char* targetParamId; }` (`targetParamId` null for `SourceUnavailable`, `LockSkipped` -> `fillLock`). Text per E-3 catalogue. A `static_assert` checks table length == `Count`.
- Audio side: `MeterState::reasonCount[Count]` relaxed `fetch_add` once per evaluation (always on). `fillEvents` carries `GapEvaluated` only while `viz->isEnabled()`; `FillStarted` always (E-3).
- `GapOutcomeLog` (processor-owned, 64 entries, message thread) exposes `snapshot (std::array<Outcome,64>&) const` + `topReasons (Top3&)`; counts cleared on `reset()` and by a "Clear" button in the popover (not on transport jump); never persisted.
- Reveal: `std::function<void (const juce::String& paramId)> onRevealParameter` set by the editor; implementation switches tab (Creative / Weights / Spectral) and, for controls hidden by Simple view (F-M04) the link switches the view to Advanced first (one explicit, persisted user-visible change, announced as "Switched to Advanced view"). Reduced Motion: outline only, no flash.
- Hooks in existing files: `FillEngine::trySchedule` (via F0-2 orchestrator, already planned), `PluginProcessor::timerCallback` drains events (already planned in F0-4), editor `FooterStatus` slot.

## 4 Parameters & state
- NEW APVTS IDs: none (explanations are read-only; adding a "show reasons" toggle would only add automation noise).
- State: legend collapsed flag in `<Perf>/legendCollapsed` (owned by F0-6/F-M04 `<Perf>`), bool, default false, ignored if malformed. No other persisted state.
- Neutral proof: new enum value and `probPct` do not alter any decision; golden A-F bit-identical; `reasons_not_published_when_viz_disabled`.
- Old sessions: unaffected (no new keys read from APVTS).

## 5 Real-time safety
- Audio thread adds: one `fetch_add(relaxed)` per evaluated gap; <= 1 `SpscQueue::push` of a <= 40-byte POD when the editor is open; one byte write into `VizFrame`. No allocation/locks. Overflow: newest dropped, `droppedEvents` incremented, popover shows "Some events were missed" (never alters decisions).
- Threshold constant `kEnterGapConf` exported from `FillEngine.h` as `inline constexpr float`; the GUI must not duplicate the number.

## 6 UI
- Placement (docs/dev/11): `VisualiserOverlay` (skipped markers, threshold line, legend strip) + `FooterStatus` (one line, <= 4 Hz). No header change.
- Status text format: `Last gap: skipped - <label> (<avail> ms free, need <need> ms)`; Started: `Last gap: filled - <Algorithm>, <len> ms`.
- Tooltip (status chip): "Why GapFill did or did not fill the last gap. Click for the most common reasons this session."
- Popover "Why so few fills?": top 3 reasons by count, each `label - count - advice - [Go to <Control>]`. Opens with Enter/Space on the focused chip; Esc closes; list items are focus targets; screen-reader text "Reason 1 of 3: Gap too short, 14 times. Advice: lower Min Gap."
- Legend strip: Event, Gap-eligible, Fill, Protected, Lookahead, Preview, Skipped; every swatch has colour + pattern/shape (hatch, dot, bar). Collapsible; at min width it collapses automatically to an info button.
- Announcements: status changes are posted to the accessibility layer at <= 1 per 2 s (verify `AccessibilityHandler::postAnnouncement` exists in pinned JUCE 8.0.15; otherwise update the status component's title, which screen readers poll).
- Reduced Motion: no fade on status; markers appear without animation.
- Help (order 1200): section "Why didn't it fill?" explaining the catalogue and threshold line.

## 7 MIDI / automation / CLAP / host
None. Pure observation. Works identically in stopped/no-transport hosts because it keys off gap evaluation, not transport. In bypass the only reason is `Bypassed`.

## 8 Tests (`Tests/features/TestReasons.cpp`)
E-3 list plus: `reason_bypassed_reported` | `reason_density_roll_carries_probability` | `needed_ms_matches_decision_inputs` (all rows of the table) | `advice_table_covers_every_reason_and_param_exists` (each `targetParamId` resolves in APVTS) | `reveal_callback_invoked_with_target` | `popover_top3_sorted_stable_on_ties` | `outcome_log_wraps_at_64` | `dropped_events_counter_increments_when_full` | `reason_count_sum_equals_evaluations` | `announcement_rate_limited`. Fixtures: one synthetic input per reason (generated in-test), scenario A for sum check. Edge: reason reached twice in one gap is impossible (one evaluation per gap) - test asserts exactly one event per gap index.

## 9 Dependencies & build order
F0-2, F0-4, F0-9 required. Build second (after F-M01 shares queue drain). Provides the record F-M03 draws. F-M09 adds `NoAlgoFits` detail for band types; F-L01 (Lock) adds `LockSkipped`.

## 10 Effort & risks
Effort L (E-3 sizing). Risks: advice text drifting from real behaviour when defaults change (mitigate: advice references parameter IDs, test checks they exist); reveal logic coupling to editor tabs (keep behind callback); `Bypassed` enum addition must land before any release locks enum order.

## 11 Acceptance checklist
- [ ] One test per reason passes with exact numbers
- [ ] Golden A-F unchanged; CPU delta editor-closed < 0.1%
- [ ] Footer line, popover, legend usable by keyboard and screen reader
- [ ] Every advice link reveals an existing control; hidden-in-Simple case handled
- [ ] Legend has shape + text for every state; narrow-size collapse verified at 1020x620
- [ ] Manual script M-3 reproduces every reason
