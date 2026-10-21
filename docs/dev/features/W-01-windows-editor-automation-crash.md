# W-01 / W-02 Windows pluginval editor-automation crash: reproduce, root cause, regression (issue #13)

Status: specified (this pass). Size L (investigation + fix). Gate A (08 W-01, W-02). Nothing here asserts a root cause: the hypotheses below are leads read from the code, to be proven or killed by the protocol.

## 1. Summary and user-visible behaviour
`pluginval --strictness-level 10 --validate <GapFill.vst3>` reports a crash during editor automation on Windows (issue #13). Goal: a deterministic reproduction, a stack trace, a minimal failing test, the fix, and a regression test, so that opening/closing the editor while the host automates parameters is safe in all hosts. User-visible: no crash, no change in behaviour.

## 2. Success / quality metric
| # | Metric | Target |
|---|---|---|
| M1 | pluginval level 10 incl. editor tests on Windows 2022 CI and a Windows 10/11 machine | exit 0, 3 consecutive runs |
| M2 | Stress: 200 editor create/destroy cycles with concurrent automation of every parameter (10 kHz writes), playback, preset loads, state restore | 0 crashes, 0 ASan/TSan reports |
| M3 | New regression test fails on the pre-fix commit | yes |
| M4 | `GAPFILL_SKIP_GUI` style bypass | not used as acceptance (isolation only) |

## 3. Design
### Protocol (record in `docs/progress/W-01.md`)
1. Reproduce on native `windows-2022` CI with verbose logging (`--verbose`, `--output-dir`), exit code captured; locally under `procdump -ma -e 1 -x <dir> pluginval.exe ...` (or WER LocalDumps) to obtain a dump; symbols from a `RelWithDebInfo` build (`/Zi`, `/DEBUG`, PDB kept as artifact).
2. Open the dump (WinDbg `!analyze -v` or Visual Studio) and record: faulting module, thread (message vs audio vs pluginval worker), top 10 frames, last JUCE call.
3. Build an ASan variant (`/fsanitize=address`, MSVC 17.7+) and a Debug-with-checked-iterators build; run pluginval against each.
4. Minimise: a test in group `Gui`: create `GapFillAudioProcessor`, run N cycles of `createEditor()` -> run message loop slices -> delete editor, while a second thread calls `setValueNotifyingHost` on all parameters and a third calls `processBlock`. Reduce until it fails in < 10 s.
### Hypotheses to test (from reading `PluginEditor.cpp`, `Knob.cpp`, `WaveformGapView.cpp`, `PluginProcessor.cpp`)
* H1 destruction order: `~GapFillAudioProcessorEditor` calls `setLookAndFeel (nullptr)` while child components and their `ParameterAttachment`/`ButtonParameterAttachment` members are still alive; members declared before the LookAndFeel member are destroyed after it. Check the member declaration order and make attachments destroyed before their controls and before the LAF.
* H2 timers: `FineSlider` (Knob.cpp 60 Hz timer), `FlashButton`, `WaveformGapView` (30 Hz) and the editor (24 Hz) timers can fire after a parent began destruction if `stopTimer()` is not first in every destructor; also `Timer` callbacks that touch `proc` after the processor is gone (processor owns `visualisation` state referenced by the view).
* H3 processor `timerCallback` (15 Hz) runs `snapParametersToState`/`parameterChanged` paths that call `setValueNotifyingHost` from the message thread while the editor repaints; host automation from a non-message thread calls APVTS listeners that touch GUI state.
* H4 `TooltipWindow` creation/destruction (Options) vs desktop shutdown at the end of pluginval runs.
* H5 `StandalonePluginHolder::getInstance()` checks in the VST3 build when the editor is created outside a standalone context.
Each hypothesis gets a pass/fail line in `docs/progress/W-01.md` with evidence; unproven ones stay open.
### Fix rules (W-02)
Fix the proven root cause only; no weakening of thread-safety or host notifications; add the regression test first (red), then fix. Typical safe patterns: declare attachments after the components they control; give every `Timer` component `stopTimer()` as the first statement of its destructor; guard async callbacks with `juce::Component::SafePointer`; never touch `proc` from a timer after `releaseResources`.

## 4. Parameters and state
None.

## 5. Real-time safety
Any fix touching the processor's listener paths must keep `parameterChanged` allocation-free on the audio thread (existing AllocGuard tests stay green) and must not take locks in `processBlock`.

## 6. UI
No visual change. Verify keyboard focus and accessibility tests still pass after any destruction-order change.

## 7. MIDI / automation / CLAP / host
Same code runs in VST3, CLAP, Standalone; run the stress test against all three wrappers where possible (clap-validator editor tests). Host check: REAPER (Windows) open/close editor 30 times during playback (M-1.5).

## 8. Tests (`Tests/features/TestEditorLifecycle.cpp`, group `Gui`)
`editor_create_destroy_100_cycles_under_automation` | `editor_destroy_while_timers_pending` | `attachments_destroyed_before_controls` (reflection-free: count live listeners on every parameter after destroy == baseline) | `no_listener_leak_after_editor_destroy` | `editor_survives_processor_state_restore_during_open` | `preset_load_during_editor_open` | `async_callback_after_destroy_is_safe` | `pluginval_level_10_windows` (CI job, not a unit test). CI: add `--repeat 3` to the Windows pluginval step in nightly.

## 9. Dependencies and build order
First runtime blocker of Gate A (08 "Immediate first work session" step 3). Needs E-4 CI steps working, PDB artifacts. Must land before Gate B B4 (realtime communication) edits the same lifecycle paths.

## 10. Effort and risks
~5 ideal days; unknown until the stack exists. Risks: crash only reproduces in pluginval's thread pattern (keep pluginval as the oracle); it may be a JUCE 8.0.15 issue (then pin or patch with an ADR). Do not claim fixed without 3 green CI runs plus the stress test.

## 11. Acceptance checklist
- [ ] Dump/stack and root cause documented (or "not reproduced" with evidence)
- [ ] Regression test red before, green after
- [ ] pluginval 10 green 3x on Windows CI; local Windows 10/11 run
- [ ] ASan/TSan clean on the stress test
- [ ] No weakened assertions, no skipped GUI tests
