# F-S04 MIDI note trigger for Audition
Status: implementation spec, Gate C candidate. Category LIVE/UX, value H, size S. **Canonical spec: `05-SPEC-USABILITY-VALUE.md` U-3**, which builds on `03-SPEC-ESSENTIALS.md` E-1 (Fill Audition) and revision R4 in `02-FOUNDATIONS.md`. Read those first. This file lists what U-3 leaves open and fixes where the setting lives now that the shared command module exists.

Name warning: the existing APVTS parameter `audition` ("Audition Detector", monitors what the detector hears) is unrelated to E-1's fill preview. All text and code for this ticket says **Fill Audition** / `CommandId::Audition`.

## 1 Summary & user-visible behaviour
A MIDI note (and, per E-1, a CC rising edge) fires one Fill Audition from the current settings. U-3 behaviour stands: NoteOn with velocity > 0 on the learned note/channel triggers; NoteOff ignored; preview amplitude `lerp (0.5, 1.0, vel / 127)`; no MIDI is output; CC handling of `MidiLearnManager` unchanged. Additions:
1. Triggers are **sample-accurate within the host block** (preview starts at the event offset), not just per block.
2. Triggers are ignored, with a status message, when: plugin or host bypassed ("Bypassed"), Panic Held ("Held by Panic"), no usable audio ("Nothing to sample - play some audio", E-1 table).
3. Several NoteOns in one block collapse to one preview (the last one's velocity). A new accepted trigger while a preview plays restarts it with a new variation (E-1).
4. Options row: **Trigger note** (Off / Learn / note name + number) and **Channel** (Any / 1-16), **Velocity sensitivity** (On default / Off = fixed 1.0).

## 2 Success/quality metric
- 1000 randomly timed NoteOns across block sizes 32-8192 each start exactly one preview, with start sample error 0 (event offset honoured) in offline tests.
- Wrong note/channel never triggers (0 false positives in a 10 000 message fuzz).
- Latency from NoteOn to audible preview start <= one `preparedBlockSize` plus the plug-in's reported latency (documented; sample-accurate relative to the host timeline).

## 3 Design
- **Binding storage:** not a bespoke `<Perf>` field. Uses F-S12's `CommandBindings` with `Binding::Kind::Note` for `CommandId::Audition` (U-3's `<Perf>` mention is superseded; `<Perf>` only keeps `noteVelocity` and `noteOctaveForMiddleC`). F-M01 and F-M10 currently expect U-3's `<Perf>` keys (`triggerNote`, `triggerChannel`, `holdNotes`). **Owner decision:** either keep one store (recommended: `<Commands>`) or mirror the Audition note into `<Perf triggerNote triggerChannel>`; because `<Commands>` is new, mirroring is a trivial read/write adapter and old files need no migration. If E-1 ships first, E-1 adds a minimal CC-only binding and F-S04 migrates it to `CommandBindings` without changing the saved format (`<Commands>` is new and optional).
- **F-S12 API extension used here:** `struct PressInfo { std::uint8_t velocity; int sampleOffset; };` and `bool takePressed (CommandId, PressInfo*) noexcept`. `processMidi` records the **first** accepted press offset in the block per command and the **last** velocity (both atomics). In `processBlock`'s chunk loop the audio thread consumes a press when `offset` falls inside `[startSample, startSample + n)`; an offset beyond the current chunk is kept for the chunk that contains it (cleared at block end, so nothing leaks across blocks).
- **Module:** `Source/features/audition/NoteTrigger.{h,cpp}` (E-1's `Audition.{h,cpp}` module hosts it; `Gui.cpp` extension):
  ```cpp
  struct NoteTriggerConfig { std::uint8_t velocityMode; /* 0 fixed, 1 scaled */ };
  float previewAmplitudeFromVelocity (std::uint8_t velocity, bool scaled) noexcept;   // lerp 0.5..1.0 or 1.0
  juce::String describeNote (int note, int octaveForMiddleC);                        // "C3 (60)"
  ```
  The trigger calls the existing E-1 entry `FillEngine::startPreview (velocity, outputPos, analysis, history, settings)` with `outputPos = blockOutputStart + max(0, offset - startSample)`; amplitude scaling is applied in the E-1 setup (`VoiceSetup::amp`), never in the processor.
- **Learn:** `CommandBindings::startLearn (CommandId::Audition)` accepts the next NoteOn (or CC press) within 10 s, countdown on the status line (`MIDI LEARN | Fill Audition | play a note | 7s`, same wording pattern as CC learn). Learn is mutually exclusive with parameter CC learn.
- **Hooks in existing files:** `PluginProcessor.cpp`: the same `commands.processMidi (midi)` lines as F-S12 (both processBlock variants); a gating `if` before `startPreview` (bypass/held checks reuse `PanicController::isHeld`). Nothing in `MidiLearn.cpp` unless `processMidi` mishandles notes: verify it ignores `isNoteOn` (it reads controllers only) and add a regression test.

## 4 Parameters & state
No APVTS parameters (command; not automatable, per E-1 and creative spec section 8). Reserved prefix `fs04_` unused. State: bindings in `<Commands>` (F-S12 format), `<Perf noteVelocity="1" noteOctaveForMiddleC="3"/>`. Defaults: no binding => feature inert => identical output and identical saved state to today. Invalid note numbers (> 127), channels, or velocity-mode values are clamped/dropped by subtree validation, rest of state loads.

## 5 Real-time safety
Scanning the buffer is O(events) with early exit when `Audition` has no binding and no learn is active. Only atomics are written. `startPreview` is the E-1 audio-thread path (preallocated `previewVoice`). No allocation, locks, logs. A MIDI flood cannot grow anything: one pending bit and one offset/velocity pair per command.

## 6 UI
- Placement: Options page `OptionsExtra`, one row "Fill Audition trigger"; plus right-click on the AUDITION button -> "Learn MIDI note/CC", "Clear". Nothing new in the header.
- Tooltip: "Play a MIDI note to hear a fill. Velocity sets how loud the preview is. Needs audio playing through GapFill." Help: addition to the Audition section ("Triggering from a pad or keyboard", order 1050).
- Accessibility: combo titles "Trigger note", "Trigger channel"; Learn is a button with live countdown text; keyboard operable; note names announced as "C 3, note 60".
- Reduced motion: no flash on trigger; the Audition button state change is steady. Narrow: row wraps in Options, never in the main surface.

## 7 MIDI/automation/CLAP/host impacts
- Needs the host to deliver notes to an effect: Ableton Live (audio tracks) does not; Logic requires an AU MusicEffect (`aumf`, produced by JUCE when `NEEDS_MIDI_INPUT TRUE`; confirm codes in A3); FL Studio needs the MIDI input port set for the effect slot; REAPER and Bitwig deliver MIDI to effects when routed. Document in Help and TROUBLESHOOTING.
- Notes are consumed, never passed through (`producesMidi()` false). MPE or per-note expression events from VST3/CLAP hosts arrive as ordinary NoteOn/off in `MidiBuffer`; per-note expression is ignored.
- Octave naming: default middle C = C3 (60), selectable C3/C4; the stored value is the MIDI number only.

## 8 Tests (`Tests/features/TestNoteTrigger.cpp`)
U-3 list: `note_trigger_fires_once_per_noteon` | `wrong_note_or_channel_ignored` | `velocity_scales_amplitude` | `learn_times_out` | `trigger_roundtrip_state` | `note_ignored_when_bypassed` | `coalesces_with_ui_request`. Added: `noteon_velocity_zero_is_noteoff` | `noteoff_ignored` | `sample_offset_honoured_in_chunk` | `offset_in_later_chunk_waits` | `two_noteons_one_block_one_preview_last_velocity` | `trigger_ignored_when_panic_held_with_status` | `trigger_without_audio_reports_nothing_to_sample` | `velocity_mode_fixed_gives_unity` | `channel_any_vs_specific` | `notes_do_not_disturb_cc_learn_mappings` | `learn_note_and_cc_mutually_exclusive` | `invalid_binding_dropped_state_loads` | `no_midi_output` | `no_audio_thread_allocation_during_flood` | `describe_note_octave_conventions`. Golden: no binding => output unchanged.

## 9 Dependencies & build order
Hard: E-1 Fill Audition (not yet implemented; U-3 says "depends E-1"), F-S12 `CommandBindings`. Soft: F-S02 status messages. Order: E-1 -> F-S12 -> F-S04.

## 10 Effort & risks
S once E-1 and F-S12 exist. Risks: host MIDI routing (largest, documented only); sample-offset consumption across sub-chunks (tested); confusion with the detector `audition` parameter (naming rule above); double-trigger from hosts that send NoteOn with both a MIDI and an event stream (test with duplicated events).

## 11 Acceptance checklist
- [ ] U-3 tests plus added tests green on Windows, Linux, macOS
- [ ] Sample-accurate start verified offline; velocity mapping exact
- [ ] Bypass, Panic Held, no-audio cases show the specified status and do not trigger
- [ ] Learn bounded to 10 s, exclusive with CC learn, persisted in `<Commands>`
- [ ] No new APVTS IDs; old sessions and golden output unchanged
- [ ] Help/TROUBLESHOOTING host-routing notes written; one real host per OS tried with hardware or a virtual keyboard
- [ ] Options row screenshot at minimum size; accessible names present
