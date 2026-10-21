# F-L05 Auto-Calibrate
Status: implementation spec, Gate C candidate (backlog `08` F-L05, existing ticket U-6). Category UX/VAL, value H, size L. Canonical algorithm: `05` U-6 (24 candidate runs, score, apply with Undo). Not repeated; this file adds the engineering details U-6 omits (how the private analysis instance is driven, event-queue limits, threading, determinism, UI states, parameter write protocol) and the metrics.

## 1 Summary & user-visible behaviour
- **LEARN** button (Creative tab > Detector area, or the detector panel header). Press, play representative audio for 4-15 s (default 8 s, stepper), watch a progress bar with **Cancel**. Then a result card: "Sensitivity 50 -> 62, Gap Threshold 60 -> 55, Min Gap 55 ms. Found 3.2 hits/s; gaps 28 % of the time." with **Apply** (default focus), **Discard**; after Apply a **Revert** button (the Undo snapshot).
- Auto-apply is not the default: the result is shown first, so users see what will change. Option "Apply automatically" in the Options page (default off).
- Refusals: "Not enough signal" (input above -60 dBFS for < 25 % of the time), "Try again" (buffer not ready), "Calibrating needs playback" (no audio blocks). Source Sidechain/Hybrid: warning "Calibrated from the main input only".
- Only Sensitivity, Gap Threshold and Min Gap change. Protection, fill, creative, detector mode/source, mix are untouched.

## 2 Success/quality metric
On the synthetic suite (120 BPM four-on-the-floor at 3 dynamics, sparse snare, dense hi-hat loop, noise, pad) the chosen settings put `eventsPerSec` in [0.8, 10], `gapFraction` in [0.15, 0.45] (or the best achievable for the material, reported as "limited" when outside), `chatter` < 0.05, in >= 5 of 6 fixtures; pad and silence are refused or reported "no events found - material has no clear hits" without changing parameters. 24 runs of 8 s audio finish < 3 s wall-clock (record in `docs/progress/F-L05.md`); UI thread never blocked > 5 ms. Repeating on identical input gives identical results.

## 3 Design
Files (`cmake/sources/calibrate.cmake`, `Tests/features/TestCalibrate.cpp`):
- `Source/features/calibrate/RecordTap.{h,cpp}`: audio-thread recorder. `bool allocate (double sr)` (message thread, on first LEARN, `15 s x sr x float` = 11.5 MB at 192 kHz; `prepareToPlay` after a rate change drops it and re-allocates lazily); `void start (int seconds) noexcept` / `cancel()`; `void write (const float* monoMain, int n) noexcept` (copies the live **input** mono sum `monoMain` from `processChunk`, before the lookahead delay, up to the target length, then sets `done`). Status atomics: `Idle, Recording, Done, Cancelled`, `framesWritten`. Records while bypassed too (analysis already runs while bypassed).
- `Source/features/calibrate/Calibrator.{h,cpp}`: pure analysis, no JUCE threads inside, deterministic.
  ```cpp
  struct CalibInput  { const float* mono; int numSamples; double sampleRate; AnalysisSettings base; };   // base = current detector settings
  struct CalibMetrics { float eventsPerSec, gapFraction, chatter; float gapLenP20Ms; };
  struct CalibResult { bool ok; juce::String reason; int sensitivity, gapThresh, minGapMs; CalibMetrics chosen; bool limited; };
  CalibResult calibrate (const CalibInput&, std::atomic<bool>& cancel, std::function<void (float)> progress);
  ```
  Per candidate: construct one private `AnalysisEngine` (prepare with the same `hopSize`), set `AnalysisSettings` = base with `sensitivity01`, `gapThresh01` from the grid, `source = Main`, `sidechainActive=false`. Feed the recording in chunks of 1024 samples, and after **each chunk** drain events: `AnalysisEngine` keeps only 256 events (`eventQueueSize`), so the runner must read `nextDetectionEvent(pos)` repeatedly and call `discardEventsBefore` per chunk to count every event (an 8 s dense hi-hat loop exceeds 256). Metrics: `eventsPerSec` = events / seconds; `gapFraction` = frames with `frameAt(p).conf >= kEnterGapConf (0.55)` / frames; `chatter` = events within 30 ms of the previous / events; gap lengths from runs of `conf >= 0.55`. Reuse one engine per candidate (reset between runs) to avoid 24 allocations; memory per engine ~1 MB (8192 frames) plus FFT scratch.
  Score and tie-break exactly as U-6 (distance to target band, +0.05 x normalised change penalty, tie -> lower sensitivity, then lower gapThresh). `minGapMs = clamp (round (0.8 x gapLenP20Ms), 20, 200)`; no gaps found -> keep current Min Gap.
- `Source/features/calibrate/Job.{h,cpp}`: `class CalibrationJob : juce::Thread` started with `Thread::Priority::low`, owns a copy-on-start snapshot of the recording (copy on the message thread from `RecordTap` when `Done`, so the audio buffer is reusable), checks `threadShouldExit()` between candidates and chunks, exits on editor close, `releaseResources`, Cancel, or processor destruction (destructor calls `stopThread (2000)`). Result posted via `MessageManager::callAsync` with a `SafePointer`.
- `Source/features/calibrate/Apply.{h,cpp}`: message thread. Steps: `processor.pushUndoSnapshot()`; for each of the 3 parameters `beginChangeGesture(); setValueNotifyingHost (convertTo0to1 (v)); endChangeGesture();`; result stored for the card/Revert (`Revert` = `processor.undo()` if the snapshot is still on top, else disabled with tooltip "Undo history moved on").
- `Gui.{h,cpp}`, `Help.cpp`, `Info.cpp` in slot `CreativeExtra` (button + progress) with the result card as an overlay in `VisualiserOverlay`.
Minimal hooks in existing files: one `recordTap.write (monoMain.data(), numSamples)` line in `processChunk` after the mono sum; `releaseResources`/`reset` cancel; Options page toggle.

## 4 Parameters & state
No new APVTS parameters (the action is a command; the three targets are existing parameters: `sensitivity`, `gapThresh`, `minGap`). User prefs (duration 4-15 s, auto-apply) go in `<Perf>` global-settings subtree `calibDurationSec`, `calibAutoApply`; validated ranges 4..15 and bool. Nothing else is saved; the recording and result are never serialised. Old sessions unaffected. Neutral proof: tap idle -> `write` returns on the first compare; golden A-F unchanged; the Calibrator never touches the processor's `AnalysisEngine`.

## 5 Real-time safety
Audio thread: at most one `std::memcpy`-class copy of `numSamples` floats while Recording (<= block size), plus an atomic status store. Buffers preallocated lazily on the message thread; if not ready the request is refused ("Try again") instead of allocating. The analysis thread never calls processor audio functions, uses its own engine, may take 100 % of a core at low priority; cap with a `std::thread::hardware_concurrency()`-independent single worker. Cancel/exit latency < 100 ms (checked every chunk). Sample-rate change during Recording cancels (generation mismatch).

## 6 UI
Per `11`: lives in the Creative tab detector area, not the header. Button "LEARN" (tooltip: "Listen to your track for a few seconds and suggest Sensitivity, Gap Threshold and Min Gap. Needs audio playing through GapFill."). Progress bar with seconds remaining and Cancel (Esc). Result card is a focusable overlay announcing its text to screen readers; Apply/Discard/Revert reachable by Tab in keyboard mode and by Enter/Esc. Reduced Motion: progress is a numeric countdown plus static bar steps, no pulsing. Narrow window: card becomes a one-line footer message with an "Details" popover. Help section "Auto-Calibrate" (order 1140) explains what it can and cannot do (needs rhythmic material).

## 7 MIDI/automation/CLAP/host impacts
Changes the three target parameters with proper gestures, so hosts record automation if armed and show touched values; plugin must not call this while the host is rendering offline (button disabled when `isNonRealtime()`). Latency/PDC unaffected (Min Gap, Sensitivity, Gap Threshold do not alter lookahead). Standalone works the same (needs an open audio device; the heartbeat check is `blockCounter`). CLAP/VST3/AU: parameter changes use the normal parameter path.

## 8 Tests
Group `Calibrate`: fixtures from `TestSignals` (four-on-the-floor x3 dynamics, sparse snare, dense hat, pad, noise, silence). `calibrate_four_on_floor_in_targets` | `calibrate_sparse_snare_in_targets` | `calibrate_dense_hats_limits_chatter` | `calibrate_pad_reports_no_events` | `calibrate_noise_reports_limited` | `silence_refused` | `calibrate_counts_more_than_256_events` (dense loop, proves drain logic) | `deterministic_same_input` | `cancel_leaves_params_untouched` | `apply_is_undoable_and_revert_restores` | `revert_disabled_after_other_undoable_action` | `apply_uses_gestures_and_clamps_ranges` | `source_sidechain_warns` | `tap_no_audio_thread_allocation` | `record_cancelled_on_samplerate_change` | `large_sample_rate_192k_ok` | `thread_exits_on_processor_destruction` (stress: create/destroy 50 times mid-run) | `calibrate_runtime_budget` (reports time; fails > 3 s on the reference runner only when `GAPFILL_PERF=1`).

## 9 Dependencies on other tickets
Hard: B4 (queues/atomics pattern), B6 only for `<Perf>` prefs. Uses existing Undo. Independent of the other F-L tickets; benefits from F-S03/U-4 settings file. Build order: any time after Gate B; recommended early because it is independent.

## 10 Effort & risks
Effort L. Risks: heuristic targets may not suit sparse or ambient material (hence "limited" reporting and no auto-apply default); 11.5 MB lazily allocated buffer at 192 kHz; Windows pluginval editor-automation crash class (issue #13) when threads outlive the editor, so all callbacks go through `SafePointer` and the thread stops in the destructor; scoring bands are product tuning and may change in their own PR.

## 11 Acceptance checklist
- [ ] Only the three target parameters change, with host gestures and an Undo snapshot
- [ ] Event counting verified beyond the 256-event queue limit
- [ ] Refusal and warning messages for silence, no signal, sidechain source, busy, offline render
- [ ] Cancel, editor close, sample-rate change and destruction are safe (stress test)
- [ ] Runtime < 3 s for 24 x 8 s on reference machine, recorded
- [ ] Result card accessible; Reduced Motion and narrow layout verified
- [ ] No audio-thread allocation; golden and old-state fixtures unchanged
