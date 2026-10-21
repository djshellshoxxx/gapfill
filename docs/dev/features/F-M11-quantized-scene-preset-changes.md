# F-M11 Quantized scene / preset changes

Status: no full spec exists. Requirements: `10-SPEC-LIVE-CREATIVE-EFFECTS.md` 4.3 (scenes, quantized modes, fallbacks, cancellable pending, "scene load must preserve dry/wet, latency, bypass"), `11` (Performance tab, queued marker, Panic independent of quantization). Scenes themselves are F-L07; this ticket builds the quantizer and delivers it for **preset changes** so F-L07 plugs in later without redesign.

## 1 Summary & user-visible behaviour
- Next to the preset selector (F-M12 browser, or the current combo) an **Apply** choice: `Now` (default, current behaviour), `Next beat`, `Next bar`, `Next phrase` (phrase length 2/4/8/16 bars, default 4).
- Choosing a preset (click, next/prev, file load from the user list) with a quantized Apply setting does not change the sound immediately. A pending chip appears: "Next bar: Dub Echo, in 2.5 beats  [Cancel]". At the boundary the new sound takes effect and the knobs/host parameters update; Undo gets one snapshot at that moment (not at request). A newer request replaces the pending one; Cancel (button, Esc on the chip, MIDI-learnable command) removes it. Panic (F-S12) always cancels pending and is never delayed.
- Fallbacks (explicit, shown in the chip): no playing transport, invalid tempo, or (for bar/phrase) invalid time signature -> setting `If no tempo`: **Apply now** (default) or **Cancel**. If the transport stops or becomes invalid while pending, the same rule applies. Pending expires after 120 s of audio time (applies per fallback rule).
- Host-initiated program changes (`setCurrentProgram` from the DAW) are always immediate; the host owns that timing.
- Quantized changes preserve Dry, Wet, Output, Lookahead (latency) and Bypass at their current values (10 section 4.3); immediate loads keep today's behaviour.
- Loops and jumps: the boundary is recomputed from the new position after a loop wrap, seek or tempo jump (detected by the existing transport-jump rule), never from a stale absolute PPQ.
- Sample accuracy contract: fill *decisions* switch exactly at the boundary sample; continuous settings (detector sensitivity, protection amounts, width) switch at the first chunk after it (<= one host block); parameters/knobs update on the message thread within ~100 ms. Fills already running are not altered.

## 2 Success / quality metric
- Exactness: with block sizes 32/64/512/4096/8192 a decision at ppq `b - epsilon` uses the old settings and at `b + epsilon` the new ones in 100% of 1000 randomised cases, `b` on the chosen grid.
- No sound change at `Now`: golden A-F bit-identical, `Apply = Now` path executes zero new audio-thread code except one null check.
- Latency of UI: request -> pending chip visible <= 100 ms; commit -> knobs updated <= 150 ms.
- Safety: 1000 random request/cancel/jump/stop/bypass sequences (seeded) never leave overlay active after ack, never crash, parameters end equal to the last committed preset.

## 3 Design
Files: `Source/features/quantize/{QuantMath.h,QuantMath.cpp,Quantizer.h,Quantizer.cpp,Resolve.h,Resolve.cpp,Gui.h,Gui.cpp,Help.cpp}`, `Tests/features/TestQuantize.cpp`, cmake fragments. Prerequisite refactor PR (behaviour-preserving, golden identical): `F-M11-0` introduces `struct ParamValues { float v[kNumParams]; static ParamValues fromCache (const ParamCache&); }` generated from `GAPFILL_PARAMS` and changes `gatherEngineSettings()` to read from a `ParamValues`; one new overload `gatherEngineSettings (const ParamValues&)`.
- Pure math (`QuantMath.h`, unit-tested, no JUCE):
  ```cpp
  enum class Grid : std::uint8_t { Beat, Bar, Phrase };
  struct TransportInfo2 { bool playing, tempoValid, tsValid, barStartValid; double ppq, bpm, barStartPpq; int tsNum, tsDen; };
  bool   nextBoundaryPpq (Grid, int phraseBars, const TransportInfo2&, double& boundaryPpq) noexcept; // false => fallback
  bool   crossed (double boundaryPpq, double ppqDecision) noexcept;                                   // ppqDecision >= boundary - 1e-9
  ```
  Beat: `ceil (ppq - 1e-9)` (armed exactly on a beat = that beat). Bar: if `barStartValid` (JUCE `getPpqPositionOfLastBarStart`, finite, <= ppq) then `barStart + beatsPerBar` else `ceil (ppq / beatsPerBar) * beatsPerBar` anchored at PPQ 0 with `beatsPerBar = tsNum*4/tsDen`; Phrase: `ceil (ppq / (beatsPerBar*phraseBars)) * ...` anchored at PPQ 0 (same rule as F-M07). Hosts differ in what they provide ([JUCE PositionInfo](https://docs.juce.com/master/classAudioPlayHead_1_1PositionInfo.html): bar start, time signature and loop points are optional).
- Message thread: `Resolve` turns a request (factory index or validated `.gapfill` tree via a new `parseParameterState (File) -> ValueTree` split out of `loadParameterStateFromFile`) into a `ParamValues` target plus a stored apply closure; invalid -> error text, nothing armed. The quantized subset overlays only the decision-time and block-time sound parameters; mix/lookahead/bypass keep live values.
- Command path: `SpscQueue<QuantCommand,4>` (message -> audio): `Arm {generation, grid, phraseBars, fallback, ParamValues target}` (about 400 B POD), `Cancel {generation}`; `SpscQueue<QuantEvent,8>` (audio -> message): `Armed(gen, boundaryPpq)`, `Committed(gen)`, `Cancelled(gen, reason)`, `Replaced(gen)`. Latest `Arm` wins.
- Audio thread (`Quantizer`, owned by the processor): states `Idle -> Pending -> Committed -> Idle`.
  - Pending: each chunk recomputes `nextBoundaryPpq` only when armed or after a jump; builds `EngineSettings sAlt` from the target (`gatherEngineSettings (target)` + `Effective::applyBlock`), one extra gather per chunk while pending; calls `FillEngine::setAlternate (&sAlt, boundaryPpq)`. In `trySchedule` the one-line choice `const auto& sUse = (alt && crossed (boundary, ppqDecision)) ? *alt : s;` (before `applyAtDecision`).
  - When a chunk's end passes the boundary: state `Committed`; subsequent chunks use `sAlt` as `s`; push `Committed(gen)`.
  - Message thread on `Committed`: `pushUndoSnapshot()`, apply the stored closure (same code path as an immediate preset load), then `ack.store (gen)`; audio drops the overlay when `ack == gen` or after 2 s.
  - Fallback/Cancel/expiry per section 1; all paths emit an event and return to `Idle`.
- Hooks: `processBlock` (command drain; also in `processBlockBypassed`, which cancels), `processChunk` (settings source), `FillEngine::trySchedule` (one line + `setAlternate`), `GapFillAudioProcessor::timerCallback` (events + ack), editor slot `FooterStatus` + preset area.

## 4 Parameters & state
- NEW APVTS IDs: none. Apply mode is a workflow preference; as a parameter it would be saved inside presets and change how presets load. Justified exception to "fm11_*" naming; reserved prefix `fm11_` for future use.
- State `<Perf>`: `quantGrid` int {0 Now,1 Beat,2 Bar,3 Phrase} (0), `quantPhraseBars` {2,4,8,16} (4), `quantFallback` {0 Apply now, 1 Cancel} (0). Invalid values -> defaults. Pending state is never serialised (06 section 3).
- Old sessions: no `<Perf>` -> `Now`. Neutral proof: `now_mode_no_audio_change` + golden A-F.

## 5 Real-time safety
Audio thread adds: one SPSC drain per block (<= 4 items), one `nextBoundaryPpq` evaluation (O(1)) per chunk while pending, one extra `EngineSettings` build per chunk while pending (~300 B, ~90 loads), 90-float copy at arm. No allocation, locks, strings or file IO (preset resolution/parsing is message thread). Bounded lifetime (120 s). Reset table: `reset()`/`releaseResources`/sample-rate change -> `Cancelled(reason=Reset)`; bypass -> cancel; transport jump -> re-arm; Panic -> cancel; editor closed -> pending continues (processor-owned), commit applies on the processor timer even without an editor.

## 6 UI
- Placement: `Apply` dropdown beside the preset selector/browser (F-M12) as a compact segmented chip "NOW | BEAT | BAR | PHRASE" (collapses to a dropdown at < 1100 px); pending chip in `FooterStatus` with Cancel; a dashed marker with preset initials on the timeline's right edge (F-M03) when expanded. No header row added.
- Tooltips: Apply "When a preset change takes effect: right away, or on the next beat, bar or phrase of the host transport." Cancel "Cancel the pending preset change." Chip text format as section 1; fallback text "No tempo from host - applying now".
- Help (order 2000): grid semantics, host differences, fallbacks, what is not delayed (mix, latency, bypass, Panic).
- Accessibility: chip is a live region (announce "Pending: Dub Echo on next bar" once, "Applied" once); Cancel reachable by keyboard; Esc cancels when the chip or preset control has focus. Reduced Motion: countdown shown as text only, no pulsing.
- Narrow: chip text truncates, full text in tooltip; the Apply control never hides.

## 7 MIDI / automation / CLAP / host
- `Cancel pending` is a learnable command (shared command-learn table, see F-M01). Next/prev preset triggers from F-M12 (`fm12_next/prev`) and F-S11 stepping obey Apply; host program change does not.
- Needs host PPQ, tempo, optional time signature and bar start: if absent the fallback applies (standalone always falls back). Variable block sizes and offline render are handled because the boundary test is per decision, not per block. Tempo changes are irrelevant (grid in beats). CLAP/VST3: plain parameters appear to change slightly after the audio did, by design (<= ~100 ms); documented in Help.
- No latency/PDC change.

## 8 Tests (`Tests/features/TestQuantize.cpp`)
`next_boundary_beat_bar_phrase_4_4_6_8_7_4_3_4` | `armed_exactly_on_boundary` | `negative_ppq` | `host_bar_start_preferred_when_valid` | `fallback_when_tempo_or_ts_invalid` | `loop_wrap_rearm_from_new_position` | `seek_rearm` | `tempo_change_keeps_beat_grid` | `decision_before_after_boundary_uses_old_new_settings` | `block_size_invariance_32_to_8192` | `chunk_continuous_settings_switch_within_one_block` | `latest_request_replaces_pending` | `cancel_and_panic_never_delayed` | `expiry_120s` | `transport_stop_while_pending_follows_fallback` | `commit_applies_once_pushes_single_undo` | `ack_handshake_and_2s_timeout` | `mix_lookahead_bypass_preserved_on_quantized_commit` | `invalid_file_never_arms` | `now_mode_no_audio_change` | `overlay_equivalence_gather_refactor` (F-M11-0) | `fuzz_1000_sequences` | `no_audio_thread_allocation`. Fixtures: scenario A with scripted transports (play/loop/seek/stop), factory presets, a valid and a truncated `.gapfill`.

## 9 Dependencies & build order
F-M11-0 refactor (small PR, own golden proof) -> this ticket. Needs F0-4/F0-5/F0-6, shared command-learn (F-M01). Better after F-M12 (browser UI hosts the control) but works with the existing combo. Enables F-L07 scenes, F-L25 last-good-scene, F-M10 quantised engage. Correct transport behaviour depends on issue #10 (variable-block jump detection, 08 Gate A W-03).

## 10 Effort & risks
Effort L (+S prerequisite). Risks: (1) refactor of `gatherEngineSettings` in a hot file; (2) overlay vs host automation of the same parameters during the ~100 ms hand-off (documented, automation wins after ack); (3) hosts with partial transport data; (4) user expectation that knobs move at the boundary (they move slightly after); (5) dependence on W-03 fix.
Owner decisions: D1 default fallback `Apply now` vs `Cancel`; D2 whether quantized loads should also hold Width/protection constant (current: they change).

## 11 Acceptance checklist
- [ ] F-M11-0 merged with golden unchanged
- [ ] Boundary exactness metric met at all block sizes
- [ ] Loop/seek/stop/tempo fallbacks tested and visible in the chip
- [ ] One Undo snapshot per committed change; mix/latency/bypass preserved
- [ ] Panic and Cancel immediate; fuzz passes
- [ ] Chip/controls keyboard- and screen-reader-accessible; layout checked at 1020x620
- [ ] Verified in REAPER (loop + tempo ramp) and one more host
