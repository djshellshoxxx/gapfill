# F-L25 Live safety and recovery suite

Status: specified, not approved. Size L. Category LIVE/ESS, value H.
Overlap: `10` section 4.3 "Safety and status" defines panic, last-good recovery, telemetry honesty; `11` sets placement and the rule that panic is never delayed by quantisation or automation smoothing; F-S12 (S tier, `F-S12-safe-return-panic.md`) is the minimal panic and owns `PanicController`, the `CancellableFx` registry (8 entries) and the shared `CommandBindings`/`<Commands>` layer; F-X07 (XL) is graduated, benchmark-aware quality throttling. This ticket consolidates and completes on top of F-S12 (which ships first and is not re-implemented here) and adds a coarse overload ladder (not F-X07), an output guard, safe-bypass hardening and last-good recall. Existing today: `FillEngine::panic()` (4 ms voice fades, called on bypass, transport jump/stop), per-sample `sanitise`, 20 ms `bypassMix` ramp, `processBlockBypassed`.

## 1 Summary and user-visible behaviour
- **Panic** (button, keyboard, MIDI via F-L24): within one block all fills and previews fade out, wet effect tails (F-L15/L19/L21) clear with a 20 ms fade, queued commands/pending scene changes/momentary states are cancelled, and the engine stays closed for a hold-off (default 2 s, or until Resume). Dry signal, latency and all parameter values are untouched. Resume re-opens the engine.
- **Output guard**: runaway protection on the wet bus that is invisible in normal use. Optional stricter ceilings.
- **Fault recovery**: NaN/Inf or absurd levels anywhere in the wet path trigger an automatic panic plus state reset, and a footer message.
- **Overload ladder**: when the audio callback runs too close to its time budget, expensive wet stages are shed in steps and restored only after sustained headroom.
- **Safe bypass**: bypass during any fill/tail is click-free and leaves no tail state behind.
- **Last good**: a single action restores the last parameter snapshot known to be stable (not a pending or unvalidated one).

## 2 Success / quality metric
- Defaults, no fault: output bit-identical to baseline (guard threshold above all golden peaks; watchdog silent).
- Panic: wet output below -80 dBFS within 25 ms of the message at 44.1..192 kHz with every algorithm and every wet stage active; dry output sample-identical to a no-panic run.
- Guard: injecting a 1e6-amplitude and a NaN burst into the wet path never produces an output sample above +12 dBFS or non-finite; recovery within 50 ms.
- Overload ladder: simulated slow blocks (injected busy-wait) step down within 0.5 s, step back up only after 4 s below 60% load; transitions click-free (no step above -50 dBFS).
- Bypass storm: 1000 toggles in 1 s (host automation test) produce no sample jump above the bypass ramp's theoretical slope; tails cleared afterwards.
- Last good: recall restores bit-exact parameter values of the snapshot and is one Undo step.

## 3 Design
Files `Source/features/safety/`: `SafetyController.{h,cpp}` (state machine, audio-owned, atomics for UI), `WetGuard.{h,cpp}`, `LoadMonitor.{h,cpp}`, `LastGood.{h,cpp}` (message thread), `Gui.{h,cpp}`, `Help.cpp`.

### 3.1 Panic and states
F-S12's `PanicController` (button, `Ctrl/Cmd+.`, hold/resume latch, `panicGain` ramp, voice silence within 8 ms) is the entry point. `SafetyController` wraps it and adds the pieces below; do not create a second panic path. Every wet stage registers as a `CancellableFx` (F-L14 `WetFxHost` registers its stages), so stage tails clear through F-S12's registry; stage tails clear within 25 ms (20 ms fade), voices within 8 ms.
States: `Normal`, `Panicking` (fade running), `Held` (engine closed, hold-off running or waiting for Resume). Entry (`SafetyController::panic (reason)`, callable on the audio thread directly): 1) `engine.panic()` plus preview voice stop; 2) `wetFx.panic (20 ms)` (F-L14 seam); 3) discard pending commands in the F0-4 queue and mark scene/preview/duel/mutation-preview requests cancelled; 4) release all momentary states (`CommandBindings`, F-S12/F-L24); 5) hold the engine closed through F-S12's latch / auto-resume (`EngineSettings.fill01 = 0`, the mechanism bypass uses); 6) publish reason. Parameters, history, locks and latency are not modified. `Resume` (or F-S12 auto-resume expiry) returns to `Normal`. Panic during Panicking restarts the auto-resume timer. Panic is not subject to quantisation, smoothing or the 500 ms stale-command rule.

### 3.2 Output guard (`WetGuard`)
Applied on the wet bus after Center/Width and before the mix (`wetL/R` loop in `processChunk`). Per sample: `a = max (|l|, |r|)`; zero-latency gain computer `g = a > ceiling ? ceiling / a : 1`; attack instant, release one-pole 60 ms; `out = clamp (x * gSmoothed, -hard, +hard)` with `hard = ceiling * 1.0`. Modes in `<Safety>`: **Runaway** (default): ceiling +6 dBFS (2.0), never active for legitimate wet levels; **Wet -1 dBFS**; **Output 0 dBFS** (applies the same computer to the final output bus after the mix; this alters the dry path only when the sum would exceed the ceiling and is opt-in). No lookahead means no latency/PDC change; instant attack can distort fast overs, so the Help text calls it a safety net, not a mastering limiter. A fault scan runs on the wet bus before the guard: `!(x * x < 1e12f)` for any sample means NaN/Inf/huge; result: zero the block's wet, call `panic (Fault)`, reset every stage (`reset()`), increment `faultCount`; rate-limited to one automatic panic per second.

### 3.3 Safe bypass hardening
Keep the 20 ms `bypassMix`. Add: on the first block with `bypassed == true` call `wetFx.panic (20 ms)` and clear stage tails after the ramp; `processBlockBypassed` and the parameter path share one code path (already `processChunk (.., true)`); bypass while a preview is running stops the preview. No behavioural change when bypass is never used.

### 3.4 Overload ladder (`LoadMonitor`)
Around `processChunk`: `t0 = juce::Time::getHighResolutionTicks()` before and after (lock-free clock read, allowed). `load = busySeconds / (n / fs)`; smoothed with a 8-block one-pole. Disabled when `isNonRealtime()` so offline renders stay deterministic. Levels: **L1** `smoothed > 0.80` for 0.5 s: `StageContext.quality = 1` (F-L21 truncates tail, formant stage F-L20 and Spectral algorithm not selected for new fills: `EngineSettings.allowSpectral = false`); **L2** `smoothed > 0.92` for 0.25 s or two single blocks above 1.5x budget within 1 s: quality 2 (all wet stages off with 20 ms fade), max concurrent voices 2; **L3** `smoothed > 1.0` for 0.25 s: `panic (Overload)` and Held. Step down one level only after `smoothed < 0.60` for 4 s. Each level change is a crossfade/fade of at most 50 ms. Telemetry (atomics): level, smoothed load, "estimated, sampled" label (never a host-independent CPU claim, per `10`). Not F-X07: no per-algorithm quality tuning, no benchmark.

### 3.5 Last good (`LastGood`, message thread)
A 15 Hz timer in the processor (already exists) calls `LastGood::tick`. A snapshot (all parameter values as normalised floats, about 120 x 4 B, plus `sceneIndex` when F-L07 exists, -1 otherwise) is captured when the sound is *healthy and settled*: no guard activation, fault, overload level > 0 or panic in the last 5 s, no parameter change in the last 2 s, and at least 10 s since the previous capture. Ring of 2 (last and previous). `recallLastGood()`: `pushUndoSnapshot()`, then apply with the existing Undo/preset apply path (`setValueNotifyingHost` inside gestures), skipping `bypass`, `dry`, `wet`, `output`, `lookahead` (global safety, mix and latency are preserved, per `10`), and `performanceParameterIDs()` that are runtime-only. If `sceneIndex >= 0` and scenes exist, recall requests that scene instead. Not serialised (runtime), so a project never reopens into a snapshot nobody validated. Recall never plays anything by itself.

## 4 Parameters and state
No APVTS parameters. Hold-off/auto-resume reuses F-S12's option in `<Perf>` (no second setting). Subtree `<Safety v="1">` (<= 256 B, validated, dropped to defaults when invalid): `guardMode` (Runaway|Wet|Output, default Runaway), `overloadLadder` (on|off, default on). A bool automatable "Panic" parameter is deliberately not added (a persistent parameter value would re-trigger on load); hosts reach panic via MIDI bindings (F-L24). Runtime only: state machine, counters, snapshots. Neutral proof: default build produces identical audio (guard threshold above any golden peak asserted by a test that scans all golden renders, watchdog inactive in nonrealtime tests).

## 5 Real-time safety
Audio thread: state machine with atomics, `WetGuard` per-sample arithmetic, block-level fault scan (`findMinAndMax` is not NaN-safe, so use the explicit comparison loop), two clock reads per chunk, bounded ladder logic. No allocation, locks, IO or logging; fault and overload messages are atomics read by the editor timer. `panic()` is idempotent and O(voices + stages). Message thread: snapshots, recall, UI. Queue interactions follow F0-4 (full = drop newest + count).

## 6 UI
The **PANIC**/**RESUME** button and `Ctrl/Cmd+.` come from F-S12 (visible on every tab). This ticket adds, in slot `PerformanceStrip` (Performance tab): a secondary "Last good" button and the guard/overload indicators; Safety status chip in `FooterStatus` showing only the highest priority message (priority: Overload > Fault > Guard active > Held > none), always with text and reason and next action, e.g. "Overload: effects paused. Raise buffer size or reduce effects." Keyboard: as F-S12 (Panic in tab order, `Ctrl/Cmd+.` while the editor has focus; Esc is not used because it closes popups); "Last good" is next in tab order. Tooltips: Panic "Stop all fills and effect tails now. Your own audio keeps playing." Last good "Go back to the last sound that was running stably." Help section order 1250 "Safety and recovery" including bypass behaviour and the estimate wording for load. Accessible names and a live region announcing state changes once. Reduced motion: no pulsing; Held state shown by solid colour plus text. Narrow: Panic keeps full size and is never hidden behind a menu.

## 7 MIDI / automation / CLAP / host
Panic/Resume/Last-good bind through F-L24 commands (audio-thread direct for Panic/Resume). Host bypass parameter unchanged (still the plug-in's bypass parameter). `getTailLengthSeconds` unchanged by this ticket. Offline rendering disables the overload ladder; fault handling stays on. CLAP/VST3/AU: no declaration changes. Standalone: the same code path.

## 8 Tests (`Tests/features/TestSafety.cpp`)
`defaults_bit_identical_golden` | `goldens_stay_below_guard_threshold` | `panic_wet_silent_25ms_all_algos` | `panic_clears_all_stage_tails` | `panic_dry_sample_identical` | `panic_not_delayed_by_quantisation_or_stale_rule` | `panic_cancels_pending_commands_and_momentaries` | `hold_off_and_resume` | `panic_during_panic_restarts_holdoff` | `guard_runaway_never_exceeds_12dbfs` | `guard_nan_and_huge_burst_recovers_50ms` | `fault_panic_rate_limited_1hz` | `guard_modes_thresholds` | `output_guard_only_acts_above_ceiling` | `bypass_storm_1000_toggles_no_jump` | `bypass_clears_tails_and_stops_preview` | `ladder_steps_down_and_up_with_hysteresis` (injected load) | `ladder_disabled_in_nonrealtime` | `ladder_transitions_click_free` | `l3_panics_and_holds` | `last_good_capture_conditions` | `last_good_recall_exact_one_undo_skips_mix_and_latency` | `last_good_prefers_scene_when_present` | `safety_subtree_roundtrip_invalid_dropped` | `no_audio_thread_allocation` | `sample_rates_44k1_to_192k`. Manual: panic during live REAPER playback with hardware controller; overload test with a deliberately heavy host session (unverified in CI).

## 9 Dependencies and order
PR 0: F-S12 (panic, button, shortcut, bindings). PR 1: `SafetyController` wrapper with hold-off modes and the Safety status chip. PR 2: guard + fault recovery. PR 3: bypass hardening + tests. PR 4: ladder (needs F-L14 seam `quality`; stages may not exist yet, so only voices/Spectral shedding at first). PR 5: last good (needs the Undo apply path; scene hook optional F-L07). Panic/Resume MIDI bindings need F-L24 (until then GUI/keyboard only). Uses F0-4, F0-6, F0-9, F-L14 seam.

## 10 Effort and risks
L total, S for PR 1 once F-S12 has shipped. Risks: guard threshold set too low would change sound (test over all goldens and presets); ladder timing is host dependent so behaviour under overload cannot be bit-reproduced (documented, disabled offline); clock reads cost about 25 ns each (negligible); last-good snapshots could capture a bad but quiet sound (conditions in 3.5 are about stability, not taste; Undo always available); keyboard shortcut collision with host key capture (provide the button as the primary path).

## 11 Acceptance checklist
- [ ] Defaults bit-identical; all goldens below the guard threshold
- [ ] Panic meets the 25 ms silence target across algorithms and stages; dry untouched
- [ ] Fault and runaway injection recover without NaN or over +12 dBFS output
- [ ] Bypass storm and tail clearing verified
- [ ] Overload ladder steps with hysteresis; disabled in offline render
- [ ] Last good exact, one undo step, preserves mix/latency/bypass
- [ ] Panic reachable by mouse, keyboard and (with F-L24) MIDI; never hidden at minimum size
- [ ] Status wording honest (estimated load, no green checkmarks)
- [ ] Manual DAW and hardware checks recorded, unverified items labelled
