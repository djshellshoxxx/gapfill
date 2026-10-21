# F-L07 Performance scenes
Status: implementation spec, Gate C candidate (backlog `08` F-L07; related F-M11 quantized changes). Category LIVE/VAL, value H, size L. No full spec exists: this is the canonical spec. Requirements source: `10-SPEC-LIVE-CREATIVE-EFFECTS.md` section 4.3 "Scenes" (8 scenes, approved parameters only, immediate/beat/bar/phrase, visible and cancellable pending state, explicit no-transport fallback, click-free, safety state preserved). The backlog says 8-16; v1 ships **8**, the file format reserves 16.

## 1 Summary & user-visible behaviour
- Eight scene slots per instance. **Store** captures the current sound (creative and weight parameters, macro values, Lock on/off) into a slot; **Recall** returns to it. Slots are named (<= 16 chars), saved with the project, not saved in presets or `.gapfill` files in v1.
- Recall timing (`Quantize`): **Immediate**, **Next beat**, **Next bar**, **Next phrase** (phrase length from F-L08, default 4 bars if unset). A pending recall shows slot highlight and text "Bar in 1.4 beats" and can be cancelled (button or Esc). A newer request replaces the pending one (single pending slot).
- Without usable transport (stopped, no PPQ, unknown meter for Bar/Phrase) the configured fallback applies: **Apply now** (default) or **Cancel**; status line explains ("No tempo: applied now").
- A scene never stores or changes: Dry/Wet/Output, Bypass, Lookahead (latency), detector settings, protection, seed, Panic/Held state, MIDI mappings, audio, or recipes. Lock stores only the on/off state, not the locked recipe.
- Recalling moves the knobs (they follow within 0.15 s) and is visible to host automation as normal parameter changes.

## 2 Success/quality metric
- Boundary accuracy: with Quantize = Bar at 90-180 BPM, the first scheduler decision at or after the bar line uses the new scene and none before it, at host block sizes 32-8192 (decision timestamp error 0 hops).
- No click: output difference around the switch has the same max sample-to-sample delta (+3 dB) as the same render without a switch, because voices already running keep the setup they started with.
- Panic safety: a pending recall is cancelled by panic (F-S12) and never applies afterwards.
- Zero cost when unused: no pending scene and no overlay -> identical audio and one atomic load per block.

## 3 Design
Files (`cmake/sources/scenes.cmake`, `Tests/features/TestScenes.cpp`; `Source/params/scenes.{h,cpp}`):
- `Source/features/scenes/SceneParams.{h,cpp}`: frozen, append-only table `constexpr SceneParam kSceneParams[]` = `{ id, EngineSettings field setter }` in this order: `fill, intensity, complexity, chaos, density, fillType, wReverse ... wTapeStop (15), pingPong, fl06_on, fl06_m1..m4, fl01_lock` (29 entries; `personality` is appended when C-1 ships). Divisions, `width`, sync flags are excluded in v1 (width is post-engine and smoothed at block granularity; divisions need tempo conversion; both are v1.1 candidates).
- `Source/features/scenes/SceneStore.{h,cpp}` (message thread): `struct Scene { juce::String name; bool used; float norm[kMaxSceneParams]; std::uint32_t presentMask; }`; `void store (int slot, const juce::AudioProcessorValueTreeState&)`, `void clear (int slot)`, `void rename (int slot, juce::StringRef)`, `bool snapshotFor (int slot, SceneSnapshot&) const`. `SceneSnapshot` is a POD (<= 32 floats + mask) copied into commands.
- `Source/features/scenes/SceneScheduler.{h,cpp}` (audio thread):
  ```cpp
  class SceneScheduler {
  public:
      struct Transport { bool valid; double ppq; double samplesPerBeat; int tsNum, tsDen; };
      void onCommand (const Command&) noexcept;                       // RecallScene{slot,quant,snapshot}, CancelScene, ReleaseOverlay, AckOverlay
      int  samplesUntilApply (const Transport&, int numSamples, const PhraseInfo&) noexcept;   // <= numSamples; splits the chunk at the boundary
      bool beginChunk (const Transport&, const PhraseInfo&) noexcept; // true when overlay became active this chunk
      const SceneSnapshot* overlay() const noexcept;                  // nullptr when none
      void noteTransportJump (const Transport&) noexcept;             // keeps pending, recomputes boundary from new ppq
      void cancelPending() noexcept;                                  // panic / sample-rate change
  };
  ```
  Boundary rule (decision-key PPQ expression, `03`/`04`): request received at chunk with `ppq0`: Immediate -> now; Beat: `B = floor (ppq0 + 1e-9) + 1`; Bar: `bar = tsNum * 4.0 / tsDen`, `B = (floor (ppq0 / bar + 1e-9) + 1) * bar`; Phrase: `len = phraseBars * bar`, anchored at `offsetBars * bar`: `B = off + (floor ((ppq0 - off) / len + 1e-9) + 1) * len`. Chunk index `i* = ceil ((B - ppqAtChunkStart) * samplesPerBeat)`; if `i* < numSamples` the processor shortens the current chunk to `i*` samples (the existing chunk loop and `updateTempoInfo (startSample)` already support arbitrary chunk sizes) and the overlay becomes active at the start of the next chunk. Because the engine's decisions land on an absolute hop grid, the first decision with `outputPos >= boundary` sees the new scene irrespective of how the host sliced the block (decision accuracy: 0 hops; the request-to-boundary choice itself depends on which block the request arrived in, which is unavoidable).
  Meter unknown (`tsNum/tsDen` invalid or not a power of two) for Bar/Phrase, or transport invalid: apply `fl07_nt_mode`.
  Overlay mechanism: while active, `gatherEngineSettings()` replaces the base fields listed in `kSceneParams` from the snapshot **before** `Effective::applyBlock` (so personality/macros still process the scene's values in the right order). No APVTS write from the audio thread.
  Sync to parameters: on apply the audio thread pushes `FillEvent::SceneApplied {slot, generation}`; the processor timer writes the scene's normalised values to the APVTS (`beginChangeGesture/setValueNotifyingHost/endChangeGesture`, guarded by an `applyingScene` flag so the parameter listener ignores them), updates `fl07_scene`, then sends `AckOverlay(generation)`; the audio thread drops the overlay (values now identical). If the user edits a scene parameter while an overlay is active (listener sees it with `applyingScene` false) the processor sends `ReleaseOverlay` and the user's edit wins. A stalled message thread leaves the overlay active, which is audibly correct and safe.
- `Source/features/scenes/Subtree.{h,cpp}`: `<Scenes v="1" active="2"><S i="1" n="Verse" m="1fffffff" v="0.35,0.50,..."/>...</Scenes>`; <= 6 KiB; validation: `v==1`, `i` in 1..8, name printable <= 16, value count <= 40, each finite in [0,1], mask only for known entries; a bad slot is dropped individually, a bad root drops the subtree; fewer values than the current table means missing parameters are left unchanged on recall. Never saves pending state or overlay.
- `Source/features/scenes/Gui.{h,cpp}`, `Help.cpp`, `Info.cpp`.
Minimal hooks in existing files: processor chunk loop (`samplesUntilApply`/`beginChunk`, ~6 lines), `gatherEngineSettings` (one overlay call), `parameterChanged` for `fl07_scene`, command drain, subtree registry entry.

## 4 Parameters & state
| ID | Type / range | Default | Unit | Notes |
|---|---|---|---|---|
| `fl07_scene` | int 0-8 | 0 | - | 0 = none; automation/MIDI-learn selects a scene (quantized by `fl07_quant`); acts only on value change |
| `fl07_quant` | choice: Immediate, Next beat, Next bar, Next phrase | Immediate | - | append-only choices |
| `fl07_nt_mode` | choice: Apply now, Cancel | Apply now | - | no-transport / unknown-meter fallback |
All three go into `performanceParameterIDs()`. Old sessions: no subtree, `fl07_scene=0` -> nothing applied on load; scenes never auto-apply at load (loading the project must not change the sound). Neutral proof: with no scene recalled the scheduler returns `numSamples` and `overlay()==nullptr` (golden A-F bit-identical, test below). `fl07_scene` restored at load only sets the displayed active slot, no overlay.

## 5 Real-time safety
Audio: one atomic load per block; on a pending request a few double operations per chunk; overlay copy of <= 32 floats once at apply; no allocation (snapshots are POD inside the command), no locks. Bounded queues: `commands` 16 entries (RecallScene collapses to the latest; CancelScene wins), `fillEvents` 64 (`SceneApplied` dropped -> the processor re-sends on the next block while `overlay()` is non-null, so sync self-heals). Resets: `reset()`/sample-rate change cancel pending but keep an active overlay until acked; bypass keeps pending (it applies while bypassed, sound is silent anyway); panic cancels pending.

## 6 UI
Placement per `11`: Performance tab (proposed), slot `PerformanceStrip`; before that tab exists a collapsed "SCENES" row in the Creative tab. Eight numbered buttons (1-8): filled = stored, ring = pending, solid highlight = active; each also shows its name in a tooltip. Context menu: Store here, Rename, Clear. Keyboard: arrows move, Enter recalls, Shift+Enter stores, Delete clears (confirm via popup), Esc cancels pending. Quantize combo with text labels. Pending status uses text plus a segmented progress, never colour alone; Reduced Motion removes the countdown animation (text only). Accessible names "Scene 3, Verse, stored, active". Narrow window: slots wrap into two rows of four. Help section "Scenes" (order 1160) with a table of what scenes do and do not store. MIDI: CC mapped to `fl07_scene` through existing learn; scene recall commands in F-S12's `CommandBindings` are an optional follow-up (it needs eight new `CommandId` values).

## 7 MIDI/automation/CLAP/host impacts
`fl07_scene` is a stepped automatable parameter (check CLAP stepped hint behaviour with clap-juce-extensions). Host automation or loops that replay scene changes are supported; each recall pushes normal parameter changes, so hosts with write automation will record the scene's parameter moves. No latency or PDC change. Tempo changes mid-wait: boundary uses the current `samplesPerBeat` per chunk (recomputed each chunk, so ramps converge); transport loop/seek: pending is kept and the boundary recomputed. Offline render: deterministic from PPQ given the same request time.

## 8 Tests
Group `Scenes`: `scenes_idle_is_bit_identical` | `store_recall_roundtrip_all_29_params` | `immediate_applies_next_chunk` | `beat_bar_phrase_boundaries_exact` (90-180 BPM, 4/4, 3/4, 6/8, 7/8) | `boundary_block_size_invariant` (32/64/512/4096/8192; decision timestamps equal) | `chunk_split_is_inert_when_no_boundary` | `no_transport_applies_now_or_cancels` | `unknown_meter_uses_fallback` | `loop_wrap_recomputes_boundary` | `tempo_change_while_pending` | `newer_request_replaces_pending` | `cancel_wins` | `panic_cancels_pending` | `overlay_precedes_macros_and_personality_order` | `overlay_released_on_user_edit` | `overlay_acked_and_params_match` | `stalled_message_thread_keeps_correct_sound` | `scene_does_not_touch_excluded_params` | `no_click_across_switch` | `subtree_roundtrip` | `subtree_bad_slot_dropped_rest_kept` | `subtree_old_table_shorter_leaves_params` | `load_never_applies_scene` | `scenes_no_audio_thread_allocation`. Manual: bar-quantized recall in REAPER on Windows and Linux with a looping region; Ableton Live session loop.

## 9 Dependencies on other tickets
Hard: B3/F0-5, B4 (queues + `SceneApplied` event), B6, B5. Soft: F-L06 (macro entries in the table), F-L01 (Lock boolean), F-L08 (phrase length/offset), F-S12 (panic cancels pending), F-L25 later builds "last good scene" from `lastApplied`. Build order: F-L06 -> F-L08 -> F-L07 (F-L07 can ship earlier with Immediate/Beat/Bar only).

## 10 Effort & risks
Effort L. Risks: overlay-then-sync protocol (race with user edits; covered by release/ack tests); block splitting must not disturb `startSample == 0` transport-jump detection in `updateTempoInfo` (test with every split position); host writing automation for 29 parameters on each recall; table order is permanent (hash test); meter detection quality varies by host; phrase semantics depend on F-L08.

## 11 Acceptance checklist
- [ ] Three IDs appended and locked; table order frozen by hash test
- [ ] Idle path bit-identical; golden A-F unchanged
- [ ] Boundary exact and block-size invariant for beat, bar, phrase
- [ ] No-transport and unknown-meter fallbacks implemented and surfaced in the status line
- [ ] Pending visible and cancellable; panic cancels
- [ ] Overlay/ack/release protocol race-tested (TSan)
- [ ] Project load never applies a scene; corrupt slots dropped individually
- [ ] Keyboard, screen-reader, Reduced Motion, narrow layout verified
- [ ] Real-host loop test on Windows and Linux REAPER recorded
