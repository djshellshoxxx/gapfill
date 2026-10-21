# F-M08 Capture v2 / Recapture (completion spec over C-10)

Status: canonical intent is `04-SPEC-CREATIVE.md` C-10 plus revision R3 in `02` (no `captureHold`). C-10 is a short sketch; code inspection shows it cannot be built as written, so this file defines the actual mechanism. Existing v1 (`capture` bool, `PluginProcessor.cpp` ~lines 497-506) stays untouched and is the neutral path.

## 1 Summary & user-visible behaviour
- v1 today: turning Capture on pauses the 6 s `SourceHistory` writer (after 0.5 s of history exists); every fill copies <= 320 ms ending at the frozen end (`FillVoice::captureSource`, `maxCaptureSamples = sr*0.32`), so all captured fills are cut from the same instant.
- v2 adds, while Capture is on:
  - **Recapture** command: replaces the held material with the most recent `Length` ms of live input (default 1000 ms, 250-2000), without releasing Capture.
  - **Length** (`fm08_length`) defines the window. **Scan** (`fm08_scan`, default 0) lets each fill's source slice start anywhere in that window (deterministic per gap) instead of always the end, so a long window gives variety. At Scan = 0 behaviour is identical to v1.
  - Status "Capture: held 1.0 s" / "Recapturing 62%" / "Recaptured 1.0 s" / "Hold Capture 0.3 s longer to recapture".
- Gaps in C-10 closed:
  1. While v1 holds, the live input is not recorded at all (writer paused), so "refresh to the latest N ms" is impossible. Fix: a **live tap** ring is written every chunk while Capture is held.
  2. C-10's "5-10 ms source crossfade at the swap" is unnecessary: voices copy their source privately at `start()`; a swap can never click a running voice. The swap is an index flip at a block boundary; fills started before it keep their copy.
  3. `Length` was meaningless in v1 (slice always <= 320 ms at the end). Scan gives it a defined effect.
  4. Predictable phase/state = a documented state machine and a window defined in absolute samples (below). Optional beat/bar alignment of the window end is Phase 2 (`fm08_align`, appended later).

## 2 Success / quality metric
- Neutrality: scenario G (new golden, generated from master before this change: Capture on at 3.2 s, off at 6 s, default settings) bit-identical.
- Recapture correctness: after Recapture, the first fill's source slice correlates >= 0.99 with the live input segment that ended at the command's window end, and < 0.2 with the previous held material (test signal: distinct sine bursts).
- Timing: Recapture completes within `ceil(windowSamples/4096)` blocks (<= 94 blocks at 192 kHz/2 s); status reflects it.
- Memory at 192 kHz stereo <= 10 MB extra (tap 2.25 s + 2 latches of 2 s); at 48 kHz <= 2.5 MB; recorded in `docs/progress/F-M08.md`.

## 3 Design
- Files: `Source/features/capture2/{CaptureStore.h,CaptureStore.cpp,Gui.h,Gui.cpp,Help.cpp}`, `Tests/features/TestCapture2.cpp`, `Source/params/fm08.{h,cpp}`, `tools/param_ids/fm08.lock`, cmake fragments.
- `class CaptureStore` (owned by the processor, audio-thread state; GUI reads atomics):
  ```cpp
  void prepare (double sr, int channels);                       // allocates tap (ceil(2.25 s)), latchA/B (2.0 s), scratch(4096) - prepareToPlay only
  void reset() noexcept;                                         // reset(), releaseResources, sample-rate change, bypass? (no), Capture off
  void onChunk (const juce::AudioBuffer<float>& in, int start, int n, bool captureHeld) noexcept; // writes tap only while held; steps pending copy
  bool requestRecapture (float lengthMs) noexcept;               // from Command::Recapture, audio thread; false => rejected (reason in status)
  const SourceHistory& source (const SourceHistory& liveHistory) const noexcept; // active latch or the live (paused) history
  bool usingLatch() const noexcept;
  float windowMs() const noexcept;                               // length of the material fills can scan
  std::uint8_t state() const noexcept;                           // Off, Held, Pending, HeldRecaptured, RejectedShort
  ```
- State machine: `Off` -(capture on & ready)-> `Held` (v1, tap starts filling) -(Recapture, tap >= 250 ms)-> `Pending` (copy tap window to the inactive latch, <= 4096 samples per chunk through a preallocated scratch buffer) -(done)-> `HeldRecaptured` (active latch flipped) -(Recapture)-> `Pending` ...; any state -(capture off | reset | prepare)-> `Off` (pending copy abandoned, latches invalid). Recapture during `Pending` is ignored (status "busy"). Window = `[tapEnd - min(len, tapFilled), tapEnd)` with `tapEnd` the tap write position at the chunk where the command was drained; window end therefore depends on host block size by at most one block (documented; Phase 2 alignment removes it for transport-synced use).
- Ring safety: the tap is 2.25 s; the copy reads absolute positions fixed at request time and aborts to `Held` with status if `tapEnd - windowStart + elapsedSincePending > tapSize - 4096` (can happen only with absurdly large host blocks).
- Processor hooks (the only edits to hot code): (a) in the capture block, call `captureStore.onChunk (...)` and replace `history` by `captureStore.source (history)` in the `engine.process` call; (b) compute `historyOffset` with the same expression as today but using the chosen source's `getWritePosition()`; (c) drain `Command::Recapture` (F0-4) at block top. When the store is `Off` or `Held`, `source()` returns `history` and nothing changes.
- Scan: in the `adjustRecipe` stage (F0-2 hook) `if (captured && scan01 > 0)`: one draw from a sub-generator seeded `hashCombine (key, kSalt_Capture)`; `back = u * scan01 * max(0, windowSamples - maxCaptureSamples)`; `setup.sourceAnchor = max(oldestValid, setup.sourceAnchor - back)`. Scan = 0 performs no draw and no arithmetic. Add `kSalt_Capture` to `Source/features/Salts.h`.
- Preview voice (F-M01) and Lock replays use the same `source()`.

## 4 Parameters & state
Existing `capture` bool is unchanged. New IDs (replace unreleased `captureLength` of the 02 registry; CAPTURE2 group slot): group file `Source/params/fm08.{h,cpp}`, lock `tools/param_ids/fm08.lock`, append-only, version hint 1:

| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fm08_length` | float 250-2000, step 10, skew none | 1000 | ms |
| `fm08_scan` | float 0-100 | 0 | % |

- Not in randomisable allowlists; `Reset` restores defaults. Never serialised: tap/latch audio, pending state, Recapture command (06 section 3). On load with `capture=true` the store starts `Off` and v1's 0.5 s readiness rule applies.
- Old sessions: IDs absent -> defaults -> identical. Validator regex change D1 (see F-M05).
- Neutral proof: scenario G bit-identical; `scan_zero_no_draw_no_change`.

## 5 Real-time safety
Per chunk: while held, one extra block write into the tap (the same memcpy cost as `history.write`); while pending, one bounded <= 4096-sample copy per channel via scratch; state machine is branch-only. All buffers allocated in `prepare()`. No locks: GUI sends `Command::Recapture` through the SPSC queue; GUI reads `MeterState` atomics (`captureState`, `captureWindowMs`, `capturePendingPct`). Reset table (06 s1 rule 6): reset -> Off; releaseResources -> Off; sample-rate change (prepare) -> realloc + Off; bypass -> state kept (fills are off anyway); transport jump -> unchanged (material is not time-locked); capture off -> Off.

## 6 UI
- Placement: `CreativeExtra` slot next to CAPTURE: `RECAPTURE` button (enabled only while Capture is on and engaged), status chip; Length and Scan knobs in the Advanced section (F-M04 rows). No header change.
- Tooltips: Recapture "Replace the held sound with what is playing now. Capture stays on." Length "How much recent audio Capture holds." Scan "How far back into the held sound each fill may start. 0 = always the end (classic)."
- Help (order 1700). Status strings as in section 1, shown in `FooterStatus` and on the chip.
- Accessibility: button name "Recapture"; progress announced at start and end only; keyboard focus per preference. Reduced Motion: progress as text "62%", no animated bar.
- Narrow: chip collapses to an icon with tooltip below 1100 px.

## 7 MIDI / automation / CLAP / host
`Recapture` is a command, MIDI-learnable through the shared command-learn table (see F-M01), CC rising edge >= 64 / re-arm < 48; not host-automatable (use the `capture` parameter for automation). `fm08_length`/`fm08_scan` are automatable. PDC/latency unchanged. Hosts that offline-render: window ends are sample-exact for given command positions; commands are not replayed from automation, so offline re-renders only match when Recapture is not used (documented).

## 8 Tests (`Tests/features/TestCapture2.cpp`)
C-10 list plus: `scenario_G_golden_unchanged` | `tap_filled_only_while_held` | `recapture_rejected_when_not_held_or_tap_short` | `pending_copy_bounded_4096_per_chunk` | `pending_uses_old_material_until_flip` | `running_voice_unaffected_by_flip` | `recapture_during_pending_ignored` | `release_during_pending_cancels` | `window_bounds_exact_samples` | `scan_zero_bit_identical` | `scan_deterministic_same_seed_varies_by_seed_and_gap` | `scan_never_reads_unwritten_or_overwritten` | `samplerate_change_clears_all` | `ids_locked_old_state_defaults` | `no_audio_thread_allocation` | `block_size_32_to_8192_all_complete`. Fixtures: alternating sine-burst signal (distinct frequencies per 500 ms), scenario A/G.

## 9 Dependencies & build order
Needs F0-2 (`adjustRecipe`), F0-4 (`Command::Recapture`), F0-1/F0-6. Shares the command-learn table with F-M01/F-M10. Interacts with F-L01 Lock (lock stores recipe incl. anchor semantics: replay uses the then-active source) and F-M01 preview. Build after F-M01.

## 10 Effort & risks
Effort L. Risks: edits the capture block in the largest hot file (`PluginProcessor.cpp`); extra memory at 192 kHz; the `historyOffset` quirk (anchor sits `latency + remaining block` before the frozen end) is preserved, not fixed, to keep goldens; owner may prefer fixing it in a separate [golden] PR. Decision D1 (shared): ID regex. Decision D2: confirm Scan as the meaning of `Length` (alternative: drop `fm08_length` and Scan, ship Recapture only with a fixed 1 s window).

## 11 Acceptance checklist
- [ ] Scenario G golden unchanged; v1 behaviour identical at defaults
- [ ] State machine tests green incl. cancel, busy, short-tap rejection
- [ ] Recapture correlation metric met; memory recorded
- [ ] IDs locked, old fixtures load, no audio/pending state serialised
- [ ] Button/status/knobs usable at 1020x620, keyboard + screen reader
- [ ] MIDI command-learn roundtrip; CLAP/VST3 parameters visible
- [ ] AllocGuard and TSan queue tests pass
