# F-L07 Performance scenes and quantized changes (also delivers F-M11 and G-03 setlist bank)

Status: specified (this pass). Size XL, split L07a data+overlay, L07b quantised engine, L07c UI+MIDI. Depends on F0-4/5/6/9, C-2 (macros), C-7 (phrase bars), W-03 (transport correctness), F-S12 (panic). Refines `10` section 4.3 and `11` setlist candidate.

## 1. Summary and user-visible behaviour
Eight **scenes** per instance, each a saved set of creative settings (fill character, algorithm weights, macros, personality, performance options). The performer recalls a scene with a button, MIDI note/CC, or Next/Previous, **Immediately** or quantised to the **next beat, bar or phrase**. A pending change is shown with a countdown and can be cancelled. When the boundary arrives the sound changes at that musical position without a click; the knobs then follow. **Last good scene** restores the last fully applied scene. Without valid transport, quantised recalls use the configured fallback (Immediate, or Hold = wait for transport) and say so.

## 2. Success / quality metric
| # | Metric | Target |
|---|---|---|
| M1 | Boundary accuracy: recall requested mid-bar with Next bar at 60-200 BPM, block sizes 32-8192 | scene active from the first engine chunk whose end >= boundary, i.e. error <= preparedBlockSize samples (<= 11 ms at 512/48k) |
| M2 | Deterministic: same project, same recall script (ppq-stamped) | bit-identical output offline vs realtime chunking |
| M3 | No discontinuity: wet/dry output step at the switch vs a no-switch render | no increase in max sample difference (running voices keep their `VoiceSetup`) |
| M4 | Knobs catch up | APVTS equals scene values <= 100 ms after activation; host sees automation |
| M5 | Bounded | scene bank state <= 8 KiB; command queue overflow counted, never blocks |
| M6 | Safety | scene load never changes `dry`, `wet`, `output`, `bypass`, `lookahead`, latency, Panic state (denylist test) |

## 3. Design
### Modules (`Source/features/scenes/`)
`SceneBank.h/.cpp` (message thread data + validation), `SceneOverlay.h` (POD), `QuantClock.h/.cpp` (pure boundary maths, shared with F-M10/F-M18/F-S08/F-M21), `SceneEngine.h/.cpp` (audio thread), `SceneMidi.cpp`, `Gui.*`, `Help.cpp`, `Info.cpp`; common `Source/features/common/SnapshotBox.h` (single-writer double buffer).
### Scene content
`sceneParameterIDs()` (allow-list, append-only, test-covered): `fill, intensity, complexity, chaos, density, fillType, seed (optional, off by default in capture), stutterDiv, delayDiv, pingPong, width, all 15 weights, xoverLM/MH and band lane settings, macro values (macroActivity..), macrosOn, personality, fillShape, fillShapeAmount, densityBreath*, phraseBars, phraseBias, mistakes, fillQuant, modSync, bandLow/Mid/HighType` plus every `*_` feature parameter flagged `sceneSafe` in its param group. **Excluded:** `dry, wet, output, bypass, lookahead, capture, fillLock, captureLength, protect*` (protection is safety), `fl25_*`, `fs12_*`, `fx06_group/slot`, detection thresholds. Stored sparsely: only values differing from defaults, as `{idIndex u8, value f32}` (<= 96 entries), scene name <= 24 chars. `<Scenes>` subtree (<= 8 KiB; 8 scenes, per-scene validation; a bad scene is dropped and reset, others load).
### Overlay: exact-boundary application without touching APVTS from the audio thread
1. Message thread: `recall (index, quant)` copies the scene into a `SceneOverlay { std::uint8_t n; struct { std::uint8_t paramIdx; float value; } e[96]; std::uint32_t generation; }`, publishes it in a `SnapshotBox<SceneOverlay>`, then pushes `Command::SceneRecall { index, quant, immediate }` through `commands` (SPSC; overflow -> counted, UI shows "busy, try again").
2. Audio thread (`SceneEngine::onChunk`, before `gatherEngineSettings`): on the command compute `boundaryPpq = QuantClock::next (ppqNow, quant, tsNum, tsDen, phraseBars)` (`Immediate` = now). Pending state `{index, boundaryPpq}`. At each chunk with valid transport, when `ppqAtChunkEnd >= boundaryPpq`, **activate**: the overlay values replace the `ParamCache` reads for the listed parameters inside `gatherEngineSettings` (one lookup layer: `value = overlay.has (idx) ? overlay.get (idx) : pv (param)`), from this chunk on. Publish `FillEvent::SceneActivated { index, generation }`.
3. Message thread (processor timer, 15 Hz): on `SceneActivated` writes the scene values into APVTS with host notification, pushes an Undo snapshot first, then sends `Command::OverlayRelease (generation)`. The audio thread drops the overlay when `ParamCache` equals the overlay for all entries (checked per chunk, <= 96 compares) or after 250 ms, and then `lastGoodScene = index`.
4. Quantised with no valid transport: `fl07_fallback` Immediate -> treat as Immediate; Hold -> stay pending (shown "Waiting for transport"), cancelled by Cancel or when `fl07_holdTimeout` (30 s, fixed) elapses.
5. Transport jump/stop while pending: pending is kept; boundary recomputed from the new ppq (Next bar from the new position); stop -> still pending (Hold) or immediate (Immediate fallback).
### QuantClock (pure)
`next (ppq, quant, tsNum, tsDen, phraseBars)`: beat: `ceil (ppq)`; bar: `ceil (ppq / beatsPerBar) * beatsPerBar` (`beatsPerBar = tsNum * 4 / tsDen`, default 4/4 if unknown); phrase: next multiple of `phraseBars * beatsPerBar` (default 4 bars). If `ppq` is exactly on a boundary (within 1e-9) the **next** boundary is returned unless `Immediate`. Tested for 3/4, 4/4, 6/8, 7/8, 7/4, 5/4, negative or NaN ppq (rejected -> fallback).
### Recall flow extras
* **Next/Previous** wrap or stop (setting `fl07_wrap`); selected slot index is state (`<Scenes>/@current`).
* **Save to slot** snapshots current effective base values (message thread) with a confirm on overwrite; **Rename**, **Clear**, **Copy**.
* **Last good scene:** F-L25 action restores `lastGood` immediately (`Immediate`, ignores quantisation).
* Scene load is excluded from Random/Surprise/Undo noise: it pushes one Undo snapshot per activation.
* Panic (F-S12) cancels pending recalls.

## 4. Parameters and state
Group `fl07`, file `Source/params/fl07.h`, lock `tools/param_ids/fl07.lock`, appended to `GAPFILL_PARAMS_EXT`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fl07_quant` | choice: Immediate, Next beat, Next bar, Next phrase | Next bar | - |
| `fl07_fallback` | choice: Immediate, Hold | Immediate | - |
| `fl07_wrap` | bool | on | - |
Scene recall itself is a command (not a parameter). State: `<Scenes>` subtree as above; `stateVersion` unchanged (absent subtree = empty bank). Old sessions: empty bank, no overlay. Neutral proof: with no recall the overlay is empty (`n == 0`, one branch per gather), goldens A-F identical.

## 5. Real-time safety
Overlay lookup is O(1) via a 256-entry index table built on the message thread (`idx -> position`), 96 floats max; `SnapshotBox` is wait-free for the reader; commands are SPSC; no allocation or locks. Reset table: `reset()` cancels pending, drops overlay; `releaseResources` same; sample-rate change: none (ppq based); bypass: pending kept, activation deferred while bypassed; transport jump: pending recomputed; panic: pending cancelled.

## 6. UI (new Performance tab, docs/dev/11)
Performance tab (only new tab; if the strip is crowded at minimum size use the grouped More menu): 8 scene pads (name, number, "current" and "pending" shown by text label + border style, not colour alone), Quantise combo, Fallback combo, Previous/Next, Cancel pending, Save/Rename/Clear, countdown "Bar 3 - in 1.5 beats", PANIC and Last Good Scene beside them. Keyboard: 1-8 selects when the tab has focus, Space cancels pending (documented, no global hotkeys). Tooltips list the quantise rule; accessible names "Scene 3, Verse Fills, pending next bar". Reduced Motion: countdown text only. Help "Scenes" order 1520.

## 7. MIDI / automation / CLAP / host
MIDI actions (extend the action table used by Surprise): `SceneRecall1..8`, `SceneNext`, `ScenePrev`, `SceneCancel`, `RecoverScene`; notes or CCs >= 64; optional MIDI Program Change selects scene 1..8 (`<Perf>` option, off by default; hosts send PC during project load, so default off). Scene recall is not a host-automatable parameter (recall order is a performance gesture); the resulting parameter changes are visible as host automation. Quantisation uses host PPQ/time signature; loop wrap and tempo changes are handled by evaluating the boundary in ppq every chunk.

## 8. Tests (`Tests/features/TestScenes.cpp`, group `Scenes`)
`quant_next_beat_bar_phrase_all_time_signatures` | `quant_exact_boundary_returns_next` | `quant_invalid_ppq_falls_back` | `recall_immediate_applies_next_chunk` | `recall_next_bar_boundary_accuracy` (M1; blocks 32-8192) | `tempo_change_before_boundary` | `loop_wrap_before_boundary` | `seek_while_pending_recomputes` | `no_transport_fallback_immediate_and_hold` | `cancel_pending` | `overlay_precedes_apvts_then_catches_up` (M4) | `overlay_dropped_when_params_match_or_timeout` | `denylist_never_touched` (M6) | `scene_roundtrip_state_and_old_state` | `bad_scene_dropped_others_load` | `command_queue_overflow_counted` | `running_voice_unchanged_by_switch` (M3) | `determinism_realtime_vs_offline_chunking` (M2) | `last_good_updates_only_after_catch_up` | `panic_cancels_pending` | `program_change_option_off_by_default` | `no_audio_thread_allocation` | `scene_ids_allowlist_covers_all_scene_safe_params`.

## 9. Dependencies and build order
Order: SnapshotBox + QuantClock (pure) -> SceneBank/state -> overlay + engine hook -> UI -> MIDI actions. Needs W-03 (transport trust), F-S12 (cancel), C-2 value set and C-7 phrase length. F-M18, F-S08, F-M10 (quantised press), F-M21 reuse QuantClock/overlay.

## 10. Effort and risks
~12 ideal days. Risks: the overlay layer touches `gatherEngineSettings` for every scene-safe parameter (one lookup helper, tested by the allow-list test); APVTS write-back loops (guarded by generation numbers); hosts firing Program Change at load (default off). Decision for the owner: 8 scenes (spec) vs 16; whether scenes may include `seed` (spec: opt-in per scene).

## 11. Acceptance checklist
- [ ] M1-M6 recorded in `docs/progress/F-L07.md`
- [ ] Denylist and allow-list tests green; sparse storage <= 8 KiB
- [ ] Real DAW: map a pad controller, recall on the bar, cancel, loop/seek (unverified items listed)
- [ ] UI checklist at minimum size (no clipped pads)
