# F-M19 UI-controlled Freeze & Scrub
Status: implementation spec, Gate C candidate (08). Category FUN/LIVE, value M, size L. Canonical context: `10-SPEC-LIVE-CREATIVE-EFFECTS.md` s4.3 (momentary/latch commands for "freeze/scrub", lost note-off timeout, panic), s5.8 (bounded snapshot handoff). **Exists already and is not changed:** `Algo::Freeze` (an automatic, gap-triggered fill that loops a short region), Capture v1 (`capture` latches the history writer so fills read frozen material) and Capture v2 (`captureLength`, Recapture, C-10). This ticket adds a **performer-driven voice**: hold a button, the last N ms of input are frozen, and a Position control scans through them.

## 1 Summary & user-visible behaviour
- **FREEZE** button (mouse: hold = momentary; latch toggle for hands-free), MIDI-learnable, keyboard operable. On press GapFill snapshots the last `Window` ms (100-2000) of the main input and starts a dedicated voice that plays it into the wet bus.
- **Hold mode:** loops a short region (`Grain`, 20-300 ms, equal-power cross-faded loop) around **Position**, giving a sustained freeze texture; moving Position slides the loop through the captured audio.
- **Scan mode:** varispeed tape-scrub of the snapshot: audio plays only while Position moves, at the speed and direction you move it (up to 8x, reverse allowed), silent when still.
- **Follow: Protected** (default) ducks the voice around detected hits with the same protection duck as fills; **Always** ignores protection (performer override, labelled).
- Release (or Panic, bypass, Reset) fades out in 20 ms (Panic 5 ms). The snapshot is never saved with the project.
- Works with Capture on: the snapshot is taken from the latched history, so it freezes the captured material.

## 2 Success/quality metric
- Engage to first audio: <= 100 ms wall time at all rates and block sizes <= 8192 (snapshot copy is capped at 4096 samples per chunk; worst case 2 s at 192 kHz = 94 chunks).
- Bounds: scrub index stays inside `[0, N-1]` for any Position automation (random-walk fuzz, 1 M samples); speed never exceeds 8x; output finite, peak <= 1.5 x snapshot peak x Level.
- Click: max |x[n]-x[n-1]| during engage, release, mode switch and 100 % to 0 % Position jumps <= 0.1 (full-scale ref) with Level 100 %.
- Idle cost: engine idle adds < 0.1 % mean block time; active (Scan, 192 kHz): <= +3 %. Zero audio-thread allocation. Normal operation with the feature unused is bit-identical (golden A-I).

## 3 Design
Module `Source/features/scrub/` (`ScrubEngine.h/.cpp`, `ScrubMath.h/.cpp` pure, `Gui.*`, `Help.cpp`). Owned by the processor (not by `FillEngine`, so fills, E-1's preview voice and this voice cannot interfere).
```cpp
enum class ScrubMode : std::uint8_t { Hold, Scan };
struct ScrubSettings { ScrubMode mode; float windowMs, pos01, grainMs, level01, smoothMs; bool followProtection; };
class ScrubEngine : public CancellableFx {                  // F-S12 interface
public:
    enum class State : std::uint8_t { Idle, Copying, Playing, Releasing };
    void prepare (double sr, int maxBlock);                  // snapshot 2 x (ceil (2.0 * sr) + 8) floats, peaks[256]
    void engage() noexcept;  void release() noexcept;        // audio thread, from drained commands
    void cancelFromAudioThread() noexcept override;          // 5 ms fade, sets Idle
    void process (juce::AudioBuffer<float>& wet, int start, int n, const SourceHistory&,
                  float protectionDuck, const ScrubSettings&) noexcept;
    State getState() const noexcept;                         // relaxed atomic, UI
    float getPeak (int bin) const noexcept;                  // 256 published bins, UI
    float getPlayhead01() const noexcept;
};
```
**Engage/copy.** `engage()` is accepted only in `Idle` (during `Releasing` it sets a coalesced `pendingEngage`). In `Copying`, each `process` call copies <= 4096 samples: `snap[ch][i] = history.read (ch, snapEnd - snapLen + i)` with `snapEnd = history.getWritePosition()` captured at engage, `snapLen = round (windowMs * sr / 1000)` clamped to `min (history size - 2, 2.0 s)`. When complete it computes the 256-bin peak summary (max abs per bin, stored in `std::atomic<float>[256]`; the UI never touches the snapshot) and enters `Playing` with a 10 ms fade-in. Silent snapshot (peak < 1e-6) => returns to `Idle` and sets `MeterState::scrubStatus = NoSignal`.
**Position smoothing and limits.** `posS += k * (pos01 - posS)` per sample, `k = onePoleCoeff (sr, smoothMs)` (5-200 ms, default 20); `idx = posS * (N - 1)`; `vel = clamp (idx - idxPrev, -8, 8)` samples/sample; idx re-derived from `idxPrev + vel` so the 8x limit is exact.
**Scan.** `y = hermite4 (snap, idx)` (4-point cubic), gain `g = smoothstep (0, 0.02, |vel|)` so stillness is silent without clicks; when `|vel| > 1` apply the existing `SVF` low-pass at `fc = 0.45 * sr / |vel|` (anti-alias for fast scrubs). **Hold.** Two readers A/B on `[s, s + G)` with `G = grainMs * sr / 1000` (clamped to `[64, N/2]`), `s = posS * (N - G)`, each advancing 1 sample/sample; every `G - X` samples (`X = G/4`) the idle reader restarts at the current `s` with an equal-power cross-fade over `X` (`equalPower`, existing util); `s` is read at each restart, so sliding Position glides without clicks.
**Output.** `out = y * level01^2 * duckS * fade`, `duckS` = 3 ms one-pole of `protectionDuck` (= `FillEngine::getCurrentDuck()`, or 1.0 when `Always`), added to `wetBuffer` after `engine.process` and before Low Protect/Center/Width (3-line hook in `processChunk`), so wet shaping applies. Mono wet bus gets the L+R mean.
**Commands.** `Command::ScrubEngage/ScrubRelease` (F0-4 queue, message-thread producer) and `CommandId::ScrubHold` (F-S12 `CommandBindings`: press = engage, release = release, honours F-M10 latch semantics when it exists; until then `fm19_latch` toggles on press). Lost note-off: a MIDI-held engage auto-releases after `kMaxHeldSeconds = 30` (Options-page value 5-60 s, `<Perf>` state, 10 s4.3). Bypass, Panic and transport-independent: a host transport stop does not release (performer control), `processChunk(.., bypassed=true)` calls `cancelFromAudioThread()`.

## 4 Parameters & state
Group `GAPFILL_PARAMS_FM19(X)` (`Source/params/fm19.h`, `tools/param_ids/fm19.lock`); `_` regex note in F-M13 s4. All in `performanceParameterIDs()`.

| ID | Type | Range | Default | Unit |
|---|---|---|---|---|
| `fm19_mode` | choice | Hold, Scan | Hold | - |
| `fm19_window` | float, skew centre 500 | 100-2000 | 500 | ms |
| `fm19_pos` | float | 0-100 | 50 | % (0 oldest, 100 newest) |
| `fm19_grain` | float, skew centre 100 | 20-300 | 120 | ms (Hold) |
| `fm19_level` | float | 0-100 | 80 | % |
| `fm19_smooth` | float | 5-200 | 20 | ms |
| `fm19_follow` | choice | Protected, Always | Protected | - |
| `fm19_latch` | bool | - | off | - |

No subtree; the held/engaged state and the snapshot are **never serialised** (F0-6). Engage is a command, not a parameter, so loading a project can never start playing. **Neutral proof:** `Idle` returns before touching `wetBuffer`; nothing can engage without a command; defaults produce no output (`idle_is_bit_identical_golden`). Old sessions load defaults.

## 5 Real-time safety
Allocation only in `prepare()` (snapshot `2 x ceil (2 s * sr)` floats: 0.77 MB at 48 kHz, 3.1 MB at 192 kHz; plus 256 atomics). Per chunk: <= 4096 history reads while copying; per sample O(1) after (two Hermite/linear reads). No locks; UI reads only atomics (`state`, `playhead`, `peaks`). Snapshot is written only in `Copying` and immutable afterwards; a new engage cannot start until `Idle`, so no reader/writer overlap. Reset table (06 s1.6): `reset`/`releaseResources`/sample-rate change (re-prepare, state -> Idle), bypass (cancel), transport jump (ignored), Panic (cancel).

## 6 UI
Slot `PerformanceStrip` (F0-9; Performance tab when it exists, 11). Controls: FREEZE button (min 96 x 28; hold with mouse/touch, `Space` held or `Enter` to toggle latch when focused), MODE segment, 256-bin waveform "scrub bar" (draggable Position, 28 px tall), GRAIN and LEVEL knobs, WINDOW combo (250/500/1000/2000 ms, Advanced adds free value), FOLLOW toggle. Status text: "Freezing 0.5 s", "Copying...", "No signal to freeze". Tooltips: FREEZE "Hold to freeze the last moment of input and play it. Release to fade out."; POSITION "Where in the frozen audio to play. In Scan mode, moving it scrubs like a tape."; FOLLOW "Protected: stays out of the way of drum hits. Always: ignores protection." Help "Freeze & Scrub" (order 1190). Accessible: titles "Freeze (hold)", "Scrub position, percent", live region for state changes; arrow keys on the scrub bar move 1 %, Shift 10 %, Home/End to ends. Reduced motion: playhead jumps without easing; no engage glow. Narrow: scrub bar keeps >= 160 px, knobs move to an overflow row.

## 7 MIDI/automation/CLAP/host impacts
Seven automatable params (`fm19_pos` is the live scrub control: MIDI-learnable, host-automatable). `ScrubHold` binds to CC or note via F-S12 bindings (hosts that do not route MIDI to effects, such as Ableton audio tracks, use the on-screen button or automation of a mapped helper). The voice adds no latency. CLAP/VST3 see only parameters. Offline render: engage is a command, so an offline render never engages unless a MIDI binding fires; document "automate `fm19_pos` and MIDI note to reproduce a performance".

## 8 Tests (`Tests/features/TestScrub.cpp`)
`idle_bit_identical_golden_A_to_I` | `copy_bounded_4096_per_chunk` | `copy_matches_history_window` | `engage_to_audio_under_100ms_192k` | `scan_index_in_bounds_fuzz` | `scan_speed_limit_8x_exact` | `scan_still_is_silent_no_click` | `hold_loop_crossfade_equal_power` | `hold_position_slide_glides` | `position_jump_click_bound` | `release_fade_20ms_and_panic_5ms` | `engage_during_releasing_coalesces` | `silent_input_refused_no_signal` | `capture_held_snapshot_from_latched_history` | `follow_protected_ducks_with_engine_duck` | `follow_always_ignores_duck` | `bypass_cancels` | `transport_stop_does_not_release` | `lost_noteoff_timeout_30s` | `sample_rate_change_to_idle` | `snapshot_never_serialised_state_roundtrip` | `thread_race_ui_reads_atomics_only` (TSan) | `cpu_idle_and_active` | `no_audio_thread_allocation`. Golden `FM19-A` (scripted engage + Position ramp) is allowed per platform.

## 9 Dependencies & build order
Needs F0-4 (commands), F0-6, F0-9, B4 (realtime communication), F-S12 (`CommandBindings`, `CancellableFx`, Panic) first. Soft: C-10 (Capture v2 window semantics), F-M10 (momentary/latch generalisation, preferred to land first; this ticket otherwise carries its own `fm19_latch`), F-M17 (wet-only render includes this voice), F-M21/F-M23 (macro target `fm19_level` later). Independent of the FX seam.

## 10 Effort & risks
L. Risks: 2 s snapshot memory at 192 kHz (3.1 MB, acceptable, constant `kMaxWindowSeconds`); users expecting scrub to be audible when still (Scan is silent by design, Hold exists); unprotected "Always" mode can bury a mix (labelled, never default, not saved as Always by Surprise since excluded); MIDI routing differences between hosts; UI drag at 15 Hz polling feels laggy (use the parameter path for Position so the audio thread gets the value immediately).

## 11 Acceptance checklist
- [ ] Idle golden bit-identical; nothing can engage from state load
- [ ] Copy bounded; engage <= 100 ms; index/speed bounds under fuzz
- [ ] Click bounds met for engage, release, jumps, mode switch
- [ ] Panic/bypass/Reset cancel; lost note-off timeout works
- [ ] No audio-thread allocation; TSan job clean
- [ ] Keyboard, screen-reader names, reduced motion, minimum-size screenshot reviewed
- [ ] Manual QA in one DAW per OS with MIDI controller; unverified hosts listed
