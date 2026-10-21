# F-M11 Quantized scene / preset changes

Status: no full spec exists. Requirements: `10-SPEC-LIVE-CREATIVE-EFFECTS.md` 4.3 (scenes, quantized modes, fallbacks, cancellable pending, "scene load must preserve dry/wet, latency, bypass"), `11` (Performance tab, queued marker, Panic independent of quantization). Scenes themselves are F-L07; this ticket builds the quantizer and delivers it for **preset changes** so F-L07 plugs in later without redesign.

## 1 Summary & user-visible behaviour
- Next to the preset selector (F-M12 browser, or the current combo) an **Apply** choice: `Now` (default, current behaviour), `Next beat`, `Next bar`, `Next phrase` (phrase length 2/4/8/16 bars, default 4).
- Choosing a preset (click, next/prev, file load from the user list) with a quantized Apply setting does not change the sound immediately. A pending chip appears: "Next bar: Dub Echo, in 2.5 beats  [Cancel]". At the boundary the new sound takes effect and the knobs/host parameters update; Undo gets one snapshot at that moment (not at request). A newer request replaces the pending one; Cancel (button, Esc on the chip, MIDI-learnable command) removes it. Panic (F-S12) always cancels pending and is never delayed.
- Fallbacks (explicit, shown in the chip): no playing transport, invalid tempo, or (for bar/phrase) invalid time signature -> setting `If no tempo`: **Apply now** (default) or **Cancel**. If the transport stops or becomes invalid while pending, the same rule applies. Pending expires after 120 s of audio time (applies per fallback rule).
- Host-initiated program changes (`setCurrentProgram` from the DAW) are always immediate; the host owns that timing.
- Quantized changes preserve Dry, Wet, Output, Lookahead (latency) and Bypass at their current values (10 section 4.3); immediate loads keep today's behaviour.
- Loops and jumps: the boundary is recomputed from the new position after a loop wrap, seek or tempo jump (detected by the existing transport-jump rule), never from a stale absolute PPQ.
- Accuracy contract: the processing chunk is split at the boundary sample, so fill *decisions* and all engine/analysis settings switch there; `width` (post-engine, smoothed) follows within its smoothing time; parameters/knobs update on the message thread within ~150 ms. Fills already running are not altered.

## 2 Success / quality metric
- Exactness: with block sizes 32/64/512/4096/8192 the first scheduler decision at or after the boundary uses the new settings and none before it, in 100% of 1000 randomised cases (decision timestamp error 0 hops).
- No sound change at `Now`: golden A-F bit-identical, `Apply = Now` path executes zero new audio-thread code except one null check.
- Latency of UI: request -> pending chip visible <= 100 ms; commit -> knobs updated <= 150 ms.
- Safety: 1000 random request/cancel/jump/stop/bypass sequences (seeded) never leave overlay active after ack, never crash, parameters end equal to the last committed preset.

## 3 Design
**One quantizer, shared with F-L07.** `F-L07-performance-scenes.md` already specifies a `SceneScheduler` (chunk splitting at the boundary, overlay applied inside `gatherEngineSettings()`, `SceneApplied` event, `AckOverlay`/`ReleaseOverlay`). This ticket adopts that mechanism and generalises it into `Source/features/quantize/`; whichever of F-M11 / F-L07 is built first owns the module, the second must reuse it and must not fork it (lead to confirm; D3). F-M11's payload is a preset, F-L07's is a scene.
Files: `Source/features/quantize/{QuantMath.h,QuantMath.cpp,QuantizedApplier.h,QuantizedApplier.cpp}`, `Source/features/presetquant/{Resolve.h,Resolve.cpp,Gui.h,Gui.cpp,Help.cpp}`, `Tests/features/TestQuantize.cpp`, cmake fragments. Prerequisite refactor PR `F-M11-0` (behaviour-preserving, golden identical): add `struct ParamValues { float v[kNumParams]; static ParamValues fromCache (const ParamCache&); }` generated from `GAPFILL_PARAMS`, and make `gatherEngineSettings()` and `gatherAnalysisSettings()` read from a `ParamValues` (live values by default). F-L07's 29-entry table becomes a `presentMask` over the same array.
- Pure math (`QuantMath.h`, no JUCE, unit-tested; F-L07 and F-M07 should call it, not copy it):
  ```cpp
  enum class Grid : std::uint8_t { Beat, Bar, Phrase };
  struct Transport { bool playing, tempoValid, tsValid, barStartValid; double ppq, bpm, barStartPpq, samplesPerBeat; int tsNum, tsDen; };
  bool nextBoundaryPpq (Grid, int phraseBars, int offsetBars, const Transport&, double& boundaryPpq) noexcept;  // false => fallback
  int  samplesUntil (double boundaryPpq, const Transport&, int numSamples) noexcept;                            // < numSamples => split chunk
  ```
  "Next" is strictly after the request: Beat `floor (ppq + 1e-9) + 1`; Bar with `bar = tsNum*4/tsDen`: `(floor (ppq/bar + 1e-9) + 1) * bar` (identical to F-L07), or `barStart + bar` when the host supplied a finite `getPpqPositionOfLastBarStart() <= ppq`; Phrase `len = phraseBars*bar` anchored at `offsetBars*bar` (F-L08 offset, default 0). Hosts differ in what they provide ([JUCE PositionInfo](https://docs.juce.com/master/classAudioPlayHead_1_1PositionInfo.html): bar start, time signature, loop points optional).
- Message thread (`Resolve`): turns a request (factory index, or a validated `.gapfill` via a new `parseParameterState (File) -> ValueTree` split out of `loadParameterStateFromFile`) into a `ParamValues` target + `presentMask` + a stored apply closure (same code path as an immediate load). Invalid -> error text, nothing armed. Mask excludes `dry`, `wet`, `output`, `bypass`, `lookahead`, so those keep live values.
- Command path: `Command::QuantArm {generation, grid, phraseBars, offsetBars, fallback, ParamValues target, mask}` (about 400 B POD) and `Command::QuantCancel` through the F0-4 `commands` queue (latest `Arm` wins, `Cancel` wins over `Arm`); events audio -> message: `Armed`, `Committed(gen)`, `Cancelled(gen, reason)`, `Replaced(gen)` through `fillEvents` (F-L07's `SceneApplied` is the same event type with a payload kind).
- Audio thread (`QuantizedApplier`): `Idle -> Pending -> Overlay -> Idle`. Each chunk while pending: recompute the boundary only when armed or after a transport jump (`noteTransportJump` re-arms from the new ppq; loop wrap and seek therefore never leave a stale absolute PPQ); `samplesUntil` shortens the current chunk to the boundary sample using the existing sub-chunk loop (must not disturb the `startSample == 0` jump detection in `updateTempoInfo`; test every split position). At the boundary the overlay becomes active: `gatherEngineSettings` reads `ParamValues` = live values with masked entries replaced by the target, before `Effective::applyBlock`. Because decisions run on the absolute hop grid, the first decision with `outputPos >= boundary` sees the new settings regardless of host block size; analysis/protection settings switch at the same chunk. Running voices keep their setup.
- Sync: audio posts `Committed(gen)`; the processor timer does `pushUndoSnapshot()` once, applies the stored closure (parameters move, `updateHostDisplay`), then sends `AckOverlay(gen)`; audio drops the overlay (values identical). A user/host edit of a masked parameter while the overlay is active sends `ReleaseOverlay` and the edit wins; a stalled message thread leaves the overlay active (correct sound), and the overlay is dropped after 2 s without ack. Never an APVTS write from the audio thread.
- Fallbacks/cancel/expiry per section 1; every path emits an event and returns to `Idle`.
- Hooks: `processBlock`/`processBlockBypassed` (drain; bypassed path cancels), chunk loop (`samplesUntil`, ~6 lines, shared with F-L07), `gatherEngineSettings` (read from `ParamValues`), `GapFillAudioProcessor::timerCallback` (events + ack), editor `FooterStatus` + preset area.

## 4 Parameters & state
- NEW APVTS IDs: none. Apply mode is a workflow preference; as a parameter it would be saved inside presets and change how presets load. Justified exception to "fm11_*" naming; reserved prefix `fm11_` for future use.
- State `<Perf>`: `quantGrid` int {0 Now,1 Beat,2 Bar,3 Phrase} (0), `quantPhraseBars` {2,4,8,16} (4), `quantFallback` {0 Apply now, 1 Cancel} (0). Invalid values -> defaults. Pending state is never serialised (06 section 3).
- Old sessions: no `<Perf>` -> `Now`. Neutral proof: `now_mode_no_audio_change` + golden A-F.

## 5 Real-time safety
Audio thread adds: one command drain per block, one atomic load per block when idle, O(1) boundary math per chunk while pending, one 90-float `ParamValues` build per chunk while an overlay is active, one 90-float copy at arm. No allocation, locks, strings or file IO (preset resolution/parsing is message thread). Bounded lifetime (120 s). Reset table: `reset()`/`releaseResources`/sample-rate change -> `Cancelled(reason=Reset)`; bypass -> cancel; transport jump -> re-arm; Panic -> cancel; editor closed -> pending continues (processor-owned), commit applies on the processor timer even without an editor.

## 6 UI
- Placement: `Apply` dropdown beside the preset selector/browser (F-M12) as a compact segmented chip "NOW | BEAT | BAR | PHRASE" (collapses to a dropdown at < 1100 px); pending chip in `FooterStatus` with Cancel; a dashed marker with preset initials on the timeline's right edge (F-M03) when expanded. No header row added.
- Tooltips: Apply "When a preset change takes effect: right away, or on the next beat, bar or phrase of the host transport." Cancel "Cancel the pending preset change." Chip text format as section 1; fallback text "No tempo from host - applying now".
- Help (order 2000): grid semantics, host differences, fallbacks, what is not delayed (mix, latency, bypass, Panic).
- Accessibility: chip is a live region (announce "Pending: Dub Echo on next bar" once, "Applied" once); Cancel reachable by keyboard; Esc cancels when the chip or preset control has focus. Reduced Motion: countdown shown as text only, no pulsing.
- Narrow: chip text truncates, full text in tooltip; the Apply control never hides.

## 7 MIDI / automation / CLAP / host
- `Cancel pending` is a command bound through F-S12 `CommandBindings` (append `CommandId::CancelPending`). `PresetNext/PresetPrev` (F-S11/F-M12) obey Apply; host program change and Program Change messages handled by F-S11 do not (documented).
- Needs host PPQ, tempo, optional time signature and bar start: if absent the fallback applies (standalone always falls back). Variable block sizes and offline render are handled because the boundary test is per decision, not per block. Tempo changes are irrelevant (grid in beats). CLAP/VST3: plain parameters appear to change slightly after the audio did, by design (<= ~100 ms); documented in Help.
- No latency/PDC change.

## 8 Tests (`Tests/features/TestQuantize.cpp`)
`next_boundary_beat_bar_phrase_4_4_6_8_7_4_3_4` | `armed_exactly_on_boundary_goes_to_next` | `negative_ppq` | `host_bar_start_preferred_when_valid` | `fallback_when_tempo_or_ts_invalid` | `loop_wrap_rearm_from_new_position` | `seek_rearm` | `tempo_change_keeps_beat_grid` | `decision_before_after_boundary_uses_old_new_settings` | `block_size_invariance_32_to_8192` | `split_at_every_position_keeps_jump_detection` | `analysis_and_protection_switch_at_boundary_chunk` | `latest_request_replaces_pending` | `cancel_and_panic_never_delayed` | `expiry_120s` | `transport_stop_while_pending_follows_fallback` | `commit_applies_once_pushes_single_undo` | `ack_handshake_and_2s_timeout` | `mix_lookahead_bypass_preserved_on_quantized_commit` | `invalid_file_never_arms` | `now_mode_no_audio_change` | `overlay_equivalence_gather_refactor` (F-M11-0) | `fuzz_1000_sequences` | `no_audio_thread_allocation`. Fixtures: scenario A with scripted transports (play/loop/seek/stop), factory presets, a valid and a truncated `.gapfill`.

## 9 Dependencies & build order
F-M11-0 refactor (small PR, own golden proof) -> this ticket. Needs F0-4/F0-5/F0-6, F-S12 `CommandBindings` (Cancel) and Panic. Better after F-M12 (browser UI hosts the control) but works with the existing combo. Shares the module with F-L07 scenes (see section 3; resolve who builds first), enables F-L25 last-good-scene and F-M10 quantised engage (`engageAt`). Correct transport behaviour depends on issue #10 (variable-block jump detection, 08 Gate A W-03).

## 10 Effort & risks
Effort L (+S prerequisite). Risks: (1) refactor of `gatherEngineSettings`/`gatherAnalysisSettings` in a hot file; (2) overlay vs host automation of the same parameters during the ~100 ms hand-off (documented, automation wins after ack); (3) hosts with partial transport data; (4) user expectation that knobs move at the boundary (they move slightly after); (5) dependence on W-03 fix.
Owner decisions: D1 default fallback `Apply now` vs `Cancel`; D2 whether quantized loads should also hold Width/protection constant (current: they change); D3 confirm one shared quantizer module with F-L07 (F-L07 text currently contains its own `SceneScheduler`; its beat/bar maths already match this file).

## 11 Acceptance checklist
- [ ] F-M11-0 merged with golden unchanged
- [ ] Boundary exactness metric met at all block sizes
- [ ] Loop/seek/stop/tempo fallbacks tested and visible in the chip
- [ ] One Undo snapshot per committed change; mix/latency/bypass preserved
- [ ] Panic and Cancel immediate; fuzz passes
- [ ] Chip/controls keyboard- and screen-reader-accessible; layout checked at 1020x620
- [ ] Verified in REAPER (loop + tempo ramp) and one more host
