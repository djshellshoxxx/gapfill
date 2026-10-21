# W-03 Variable-block transport prediction fix (issue #10, PR #12)

Status: specified (this pass). Size M. Gate A item (08 W-03); blocks any live-scheduling feature (F-L07, F-X04, F-X06, F-M21). Must be reconciled with PR #12 (compare patches, adopt only what has green evidence).

## 1. Summary and user-visible behaviour
`updateTempoInfo` decides whether the host transport "jumped" by comparing the reported PPQ with a prediction. With variable or large host callbacks the prediction is wrong, so the engine falsely calls `noteTransportJump()` + `panic()`: fills are cut off and the deterministic gap counter restarts for no musical reason. After the fix, only real seeks, loops, start and stop reset the engine, for every host block size and internal sub-chunking. Users see no behaviour change in well-behaved hosts and stop seeing dropped fills in hosts that vary block size.

## 2. Success / quality metric
| # | Metric | Target |
|---|---|---|
| M1 | Alternating host block sizes 8192/32/4096/64/512 over 60 s of steady playback | 0 false jumps (counter), fills identical to a fixed-512 render |
| M2 | Real seek (>= 0.25 beat), loop wrap, stop->start, tempo ramp 60->180 BPM over 8 bars, tempo step | each real event detected exactly once; ramp and step produce 0 false jumps |
| M3 | Prior code | the new tests fail on the unfixed code (red first) |
| M4 | Determinism | same seed + PPQ gives the same fill positions regardless of block pattern |

## 3. Design
Verified by reading `Source/PluginProcessor.cpp` `updateTempoInfo` (about lines 272-340):
* `expected = lastPpqPosition + numSamples / samplesPerBeat` uses `numSamples` of the **current sub-chunk** (at most `preparedBlockSize`) and the **current** tempo, while `lastPpqPosition` was captured at the start of the **previous whole host callback**. With a host block larger than `preparedBlockSize`, or varying block sizes, the predicted advance is wrong by thousands of samples, exceeding the 0.25-beat tolerance.
* `|| ! ppqValid` inside the jump condition runs on every callback when the host reports `isPlaying` without a PPQ: the engine would panic every block. Verify against a host that does this (some hosts do while stopped or in render modes) before changing; the fix below makes it a one-shot.

Fix (pure helper, `Source/features/transport/TransportTracker.{h,cpp}`, called from `updateTempoInfo`):
```cpp
struct TransportSample { bool playing, ppqValid, tempoValid; double ppq, bpm; int blockSamples; };
enum class JumpKind { None, Start, Stop, Seek };
class TransportTracker {
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    JumpKind update (const TransportSample&) noexcept;   // call once per host callback (first sub-chunk only)
};
```
* Remember the previous **callback**: `prevPpq`, `prevBlockSamples` (whole host `buffer.getNumSamples()`, passed from `processBlock`, not the chunk), `prevBpm`.
* Predicted advance in beats: `blockSamples_prev / (sampleRate * 60 / avgBpm)` with `avgBpm = (prevBpm + bpm)/2` (exact for a linear tempo ramp).
* Tolerance: a **seek** is `|ppq - expected| > tolBeats`. Initial `tolBeats = 0.1` (about 50 ms at 120 BPM, tighter than today's 0.25 so small nudges are caught, still far above host rounding jitter of a few samples). The PR measures PPQ jitter in two real hosts, may adjust within `[0.02, 0.25]`, and records the final value in `docs/progress/W-03.md`. The old 0.25 is the hard upper bound so hosts that worked before keep working.
* `Start` = `playing && ! prevPlaying`; `Stop` = `! playing && prevPlaying`; both call `panic()` as today; `Start` also `noteTransportJump()`.
* If `playing && ! ppqValid`: report `None` after the first callback (deterministic fallback key uses `gapIndex`, as today); one `Start`-like reset on the transition into this state only.
* `updateTempoInfo` keeps its signature; it calls the tracker only when `startSample == 0` (first sub-chunk) and applies the returned kind.
Data flow: `processBlock` (knows full callback length) -> stores `hostBlockSamples` -> `processChunk` -> `updateTempoInfo` -> `TransportTracker::update`.

## 4. Parameters and state
None. No state change. Neutral proof: with a constant block size equal to `preparedBlockSize` and steady tempo, the new predicate equals the old one up to the tolerance; goldens A-F (fixed block size) unchanged; added test renders A at 7 block patterns and compares fill start positions with the fixed-block render.

## 5. Real-time safety
Pure arithmetic on doubles, no state beyond 5 scalars. Reset table: `reset()` and `prepareToPlay` clear; sample-rate change re-prepare; bypass: tracker still updates (so a bypassed stop/start is seen); Capture hold: unaffected.

## 6. UI
None. The debug overlay (existing) may show a `transportJumps` counter; if the Gate B overlay does not exist, expose the counter through `MeterState` only (test-visible).

## 7. MIDI / automation / CLAP / host
Host behaviour matrix to test manually (E-7): REAPER (large blocks, loop), a host with variable blocks (Bitwig), FL Studio (tempo automation), standalone (no playhead). CLAP/VST3/AU share the code.

## 8. Tests (`Tests/features/TestTransport.cpp`, group `Transport`)
`steady_playback_no_jump_at_blocks_8192_32_4096_64_512` | `chunk_boundary_inside_large_block_no_jump` | `tempo_ramp_no_false_jump` | `tempo_step_no_false_jump` | `seek_detected_once` | `loop_wrap_detected_once` | `stop_start_detected` | `playing_without_ppq_is_oneshot_not_per_block` | `prediction_uses_previous_callback_length` | `fill_positions_equal_across_block_patterns` | `old_predicate_fails_these_tests` (regression guard, documents red state) | `bypass_still_tracks_transport` | `sample_rate_change_resets`. Use the F0-7 TestRig with an injected playhead (`juce::AudioPlayHead` stub returning a scripted `PositionInfo`).

## 9. Dependencies and build order
Independent of F0 if done first (small), but should land before F0-2 so the golden for scenario F captures correct behaviour. Coordinate with PR #12 and issue #10; adopt its tests where they fail on master.

## 10. Effort and risks
~2 ideal days. Risks: hosts with jittery PPQ (tolerance too tight -> false jumps; too loose -> missed small seeks): constants come from measurements in two real hosts, documented. Changing detection can alter golden F (bypass->active); if it does, that is a `[golden]` PR with justification.

## 11. Acceptance checklist
- [ ] New tests red on master, green after fix (both OS)
- [ ] Goldens unchanged (or `[golden]` justified)
- [ ] M1-M4 recorded in `docs/progress/W-03.md`
- [ ] PR #12 reconciled (merged/closed with reference)
- [ ] Manual: REAPER and one variable-block host, loop + seek script M-6.6 passes
