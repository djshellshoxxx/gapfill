# F-S12 Panic and safe return (also delivers 11-GUI "Panic and recover" and G-04)

Status: specified (this pass). Size M. Depends on F0-4 (queues), F0-9 (slots). P1 in `11`. Build before any other live-performance feature. Quantisation never applies to Panic. F-L25 builds the wider safety suite on top.

## 1. Summary and user-visible behaviour
One always-reachable **PANIC** control stops GapFill's contribution immediately and cleanly: active fills fade out in 10 ms, any audition preview, pending scene change, held momentary effect (F-M10) and effect tails (delay/dub/ratchet) are cancelled or flushed, and the wet path is muted. The dry signal is not touched, no click, no latency change. The plugin then sits in a visible **Safe** state ("WET MUTED - Resume") until the user resumes (click, key, or MIDI) or an optional auto-resume timer expires. Panic is available from: the PANIC button (Performance tab and a compact footer button), a keyboard shortcut when the editor has focus, and a MIDI-learnable action. Safe state is never saved in projects or presets.

## 2. Success / quality metric
| # | Metric | Target |
|---|---|---|
| M1 | Wet energy after the request: level in the 10-30 ms after the block that processes the request | < -80 dBFS (all 15 algorithms, spectral and F-L15 tails active) |
| M2 | Latency from request to first attenuation sample | <= one host block (+ <= 1 engine chunk) when requested from UI; same block when requested by MIDI |
| M3 | Dry path | bit-identical to a run without panic |
| M4 | Click-free | max |sample difference| on the wet bus during the ramp <= the ramp's analytic bound (linear 10 ms) + epsilon |
| M5 | Never lost | 1000 rapid requests from UI thread while the command queue is full: every request observed |
| M6 | Resume | ramp-up 20 ms, no stuck voices, normal fills resume at the next gap |

## 3. Design
`Source/features/safety/Panic.{h,cpp}`, `PanicGain.h`, `Gui.*`, `Help.cpp`, `Info.cpp`.
* **Request channel (never queued):** `struct PanicLatch { std::atomic<std::uint32_t> panicSeq {0}, resumeSeq {0}; }` owned by the processor. Any thread increments `panicSeq` (UI, MIDI learn handler, shortcut). It is a counter, not an event, so a full SPSC queue cannot drop it.
* **Audio thread, top of `processChunk`:** `if (latch.panicSeq != seenPanic) { seenPanic = latch.panicSeq; enterSafe(); }` and the same for resume. MIDI-triggered panic is detected in the same block by the MIDI scan (as U-3) and calls `enterSafe()` directly before rendering.
* **enterSafe():** `engine.panic()` (existing quick kill), `previewVoice.stop()` (E-1), `performer.releaseAll()` (F-M10), `sceneEngine.cancelPending()` (F-L07), `rouletteOverlay.cancel()` (F-S08), `wetModules.flushTails (fadeMs = 20)` through the `IWetModule::panic()` contract (every effect module F-M14..F-L20 implements it: fade its feedback/tail state to zero within 20 ms, no discontinuity), then `panicGain.setTarget (0, 10 ms)` and `state = Safe`, `resumeAtSample` set if auto-resume is configured.
* **PanicGain:** a per-sample linear ramp multiplier on the **wet bus only**, applied after protection duck and before the final wet/dry mix; ramp 10 ms down, 20 ms up. Dry gain, bypass mix and output gain are untouched. Reported latency is unchanged.
* **Resume:** `resumeSeq` increments; target 1 over 20 ms; `state = Live`. Auto-resume timer counts **samples** (deterministic) not wall time. A transport jump while Safe stays Safe.
* **Recovery of the last good scene** is a separate action (F-L07), not part of resume.
* Panic is idempotent; panic while bypassed only sets the flag (nothing audible to stop).

## 4. Parameters and state
Group `fs12`, file `Source/params/fs12.h`, lock `tools/param_ids/fs12.lock`, appended to `GAPFILL_PARAMS_EXT`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fs12_resume` | choice: Manual, 2 s, 5 s, 10 s | Manual | - |
| `fs12_midiReset` | bool: treat MIDI CC 120 (All Sound Off) and CC 123 (All Notes Off) as panic | off | - |
* The Panic action itself is not a parameter; its MIDI mapping lives in the MIDI action table (`<Perf>`/MIDIMAP). Safe state is runtime-only; `performanceParameterIDs()` contains both IDs; a project always opens in Live.
* Neutral proof: with no panic request `PanicGain` is exactly 1.0f and skipped (`if (gain == 1.0f)` branch), `latch` comparison is two integer compares per chunk, goldens A-F bit-identical.

## 5. Real-time safety
Atomics only; fixed arrays; no allocation. Reset table: `reset()` leaves Safe/Live as is but clears ramps to the steady value; `releaseResources` -> Live; sample-rate change recomputes ramp lengths; bypass: panic latch still observed; transport jump: unchanged; offline render: a panic request in a project cannot exist (runtime-only).

## 6. UI (docs/dev/11 placement)
Large **PANIC** button in the Performance tab (min 48 x 48 px) and a compact **PANIC** text button always visible in `FooterStatus` (>= 24 px hit target; text, not colour only). Safe state replaces the footer message with the highest priority line: "PANIC - wet muted. Resume (R)". Keyboard: Ctrl/Cmd+. while the editor has focus (customisable later) and Tab-reachable; screen reader name "Panic, silence fills", state announced. Reduced Motion: no flashing; state is text. No popup, no confirm dialog. Tooltip: "Stops all fills and effect tails now. Your dry signal keeps playing. Press Resume to bring fills back." Help section "Panic" order 1500.

## 7. MIDI / automation / CLAP / host
Action mappable by MIDI Learn (note or CC >= 64); also optional CC 120/123 handling. Not a host-automatable parameter (avoids an automation lane that could silence a track unintentionally); host "stop" already panics via the existing transport logic. CLAP/VST3/AU identical. Standalone: shortcut works when the window is focused.

## 8. Tests (`Tests/features/TestPanic.cpp`, group `Panic`)
`panic_silences_wet_each_algorithm_within_15ms` (loop over 15) | `panic_flushes_effect_tails_all_modules` | `dry_bit_identical_during_panic` | `ramp_is_linear_10ms_click_free` | `panic_cancels_preview_and_pending_scene_and_momentary` | `request_never_lost_when_queue_full` | `midi_panic_same_block` | `cc120_cc123_when_enabled_only` | `resume_ramps_20ms_and_fills_return` | `auto_resume_counts_samples` | `panic_idempotent` | `panic_while_bypassed_no_effect_no_crash` | `transport_jump_while_safe_stays_safe` | `safe_state_not_saved_or_loaded` | `latency_unchanged` | `no_audio_thread_allocation` | `panic_during_spectral_freeze_releases_claim` | `neutral_golden_A_to_F`.

## 9. Dependencies and build order
Earliest of the live features. Needs F0-4 commands only for non-critical UI messages; the latch avoids dependence. `IWetModule::panic()` contract is added to the effect module base in F-M14 (or earlier as an interface-only PR).

## 10. Effort and risks
~3 ideal days. Risks: an effect module forgetting to implement `panic()` -> tails leak; the contract test iterates a registry of all modules and fails if any lacks coverage. A muted wet path could be mistaken for a bug: the footer message is mandatory.

## 11. Acceptance checklist
- [ ] M1-M6 recorded in `docs/progress/F-S12.md`
- [ ] Keyboard path and MIDI action verified; hardware MIDI listed unverified if not tried
- [ ] Panic reachable at minimum window size (T-02 screenshots)
- [ ] Every wet module covered by the contract test
