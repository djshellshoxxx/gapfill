# F-L11 MIDI output from gap and event detection
Status: implementation spec, Gate C candidate (backlog `08` F-L11). Category VAL/MUS, value H, size L. No full spec exists: this is the canonical spec. It changes a plugin-wide declaration (MIDI output), so one decision is reserved for the owner (section 10).

## 1 Summary & user-visible behaviour
- GapFill can send MIDI notes to the host so the detector can trigger other instruments, samplers or lights: a **Hit** note on every detected onset (velocity follows hit strength), a **Gap** note held for the duration of each eligible gap, a **Fill** note held for the duration of each started fill, and optionally a **CC** carrying gap confidence (0-127).
- Everything is **off by default**. Master switch **MIDI OUT** (`fl11_on`), channel (default 10), a note number per event type (defaults 36, 37, 38), Hit note length (10-500 ms, default 60), velocity mode (Follow strength / Fixed 100), timing (Output-aligned / Detection-early), CC number (Off or 1-119).
- Output never contains the incoming MIDI (CC learn traffic is not echoed). Disabling, bypass, panic, transport stop and unloading send note-offs for anything still sounding.
- If the build or host cannot output MIDI the control is disabled with an explanation ("This build does not output MIDI", "Host did not provide a MIDI output").

## 2 Success/quality metric
- Timing: with Output-aligned timing the note-on sample offset equals the detection event position mapped into the output block exactly (0 samples error) for 100 synthetic hits at block sizes 32, 64, 512, 4096, 8192 and lookahead 0-250 ms. Detection-early notes land at the input position (<= 1 hop = 32 samples late).
- No stuck notes: after 1000 randomised sequences of toggling `fl11_on`, transport stop/start, bypass, panic, `reset()`, sample-rate change, the number of note-ons equals the number of note-offs per note/channel at every quiescent point (invariant test).
- Bounds: <= 32 events per block, <= 66 CC messages per second; MIDI work costs < 0.2 % of a 64-sample block.

## 3 Design
Files (`cmake/sources/midiout.cmake`, `Tests/features/TestMidiOut.cpp`; `Source/params/midiout.{h,cpp}`):
- `Source/features/midiout/EngineEvents.h` (small shared header, also usable by F-L13/F-S07): `struct OutEvent { enum Type : std::uint8_t { Hit, GapOpen, GapClose, FillStart, FillEnd, Confidence } type; std::int64_t pos; float value; }`; `struct EngineEventBuffer { OutEvent ev[64]; int count = 0; bool push (const OutEvent&) noexcept; void clear() noexcept; }`. `FillEngine::process` receives an optional `EngineEventBuffer*` (nullptr when MIDI output is off, so there is no cost) next to `VisualisationFifo*`. Emission points in `controlUpdate`: `Hit` where `mainEventNow || detection event` (strength = `frame.transient`), `GapOpen/GapClose` on `inGap` transitions, `FillStart` after a successful `startVoice` (value = length samples; the processor schedules FillEnd = pos + length), `Confidence` once per hop only when the CC is enabled.
- `Source/features/midiout/MidiScheduler.{h,cpp}` (audio thread, fixed arrays):
  ```cpp
  struct MidiOutSettings { bool on; int channel; bool hitOn, gapOn, fillOn; int noteHit, noteGap, noteFill; bool velFollow; float hitLenMs; int cc; bool early; };
  class MidiScheduler {
  public:
      void prepare (double sr) noexcept;  void reset() noexcept;
      void beginBlock (std::int64_t outStart, int n, std::int64_t inStart) noexcept;
      void consume (const EngineEventBuffer&, const MidiOutSettings&) noexcept;       // maps events to timed MIDI messages
      void scanEarlyHits (const AnalysisEngine&, std::int64_t from, std::int64_t to, const MidiOutSettings&) noexcept;
      void flushAllNotes (int sampleOffset) noexcept;                                  // panic / disable / bypass / stop
      int  render (juce::MidiBuffer& out, int maxEvents = 32) noexcept;               // writes in time order, returns count
  };
  ```
  Internals: `Msg { std::int64_t absPos; std::uint8_t bytes[3]; std::uint8_t len; }` in a 64-slot pending array sorted by `absPos` on insert (insertion into a tiny fixed array), plus an active-note bitmap for the three notes. Rules: (a) a note-on is emitted only if two slots are free (the note-off is scheduled at once: Hit `absPos + round (hitLenMs*sr/1000)`, Gap at `GapClose`, Fill at `pos + length`); if the buffer is full the **note-on is dropped, never the note-off**. (b) Retrigger: if the same note is still active, emit its note-off at the same sample just before the new note-on. (c) Gap/Fill notes shorter than 1 ms are lengthened to 1 ms. (d) `GapOpen` for a gap that never closes (stop, bypass) is closed by `flushAllNotes`. (e) CC: value `round (127*conf)`, sent only when the value changed by >= 2 and >= 15 ms since the last send.
  Timing: engine events carry output-timeline positions; the sample offset inside the host block is `startSample + (absPos - outputStartOfChunk)` (same coordinate the audible dry hit uses: the dry is delayed by the lookahead exactly like the engine's output position). Output-aligned notes therefore coincide with the audible hit and with the audio the host will compensate by the reported latency. **Detection-early** (Hit only) scans the analysis event queue for new events with position in `[cursor, analysisPos)` and places them at `max (0, pos - inputBlockStart)`; it exists for hosts that do not delay MIDI by plugin latency. Gap and Fill notes are decided by the scheduler at output time, so they are Output-aligned only; the UI states this.
- Processor integration: `processBlock` passes the incoming `MidiBuffer` to `midiLearn.processMidi` as today, then **clears it** (`midi.clear()`) when output is compiled in, then `scheduler.render (midi)`. Without this the host would receive the learn CCs back as plugin output. If MIDI output is disabled at runtime the buffer is still cleared (no echo).
- Build gate: CMake option `GAPFILL_MIDI_OUT` (default OFF until the real-host matrix passes, see 7 and 10). ON sets `NEEDS_MIDI_OUTPUT TRUE` in `juce_add_plugin` and `producesMidi()` returns `true`; OFF keeps today's metadata, `producesMidi()` false, and the feature shows "unavailable in this build" (code still compiles and is unit-tested through the scheduler API). Reference: JUCE CMake API documents `NEEDS_MIDI_OUTPUT` as "Whether the plugin should provide a midi output" (https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md).
- `Gui.{h,cpp}`, `Help.cpp`, `Info.cpp`; Options page "MIDI Output" group (not the main tabs).
Minimal hooks: `producesMidi()`, `process()` signature gains the event buffer pointer, three emission blocks in `controlUpdate`/`startVoice`, `processBlock` MIDI tail, CMake option.

## 4 Parameters & state
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fl11_on` | bool | off | - |
| `fl11_chan` | int 1-16 | 10 | channel |
| `fl11_hit_on` | bool | on | - |
| `fl11_gap_on` | bool | off | - |
| `fl11_fill_on` | bool | off | - |
| `fl11_note_hit` | int 0-127 | 36 | MIDI note |
| `fl11_note_gap` | int 0-127 | 37 | MIDI note |
| `fl11_note_fill` | int 0-127 | 38 | MIDI note |
| `fl11_vel` | choice: Follow strength, Fixed 100 | Follow strength | - |
| `fl11_len_ms` | float 10-500 | 60 | ms |
| `fl11_cc` | int 0-119 (0 = Off; 120+ are channel-mode messages and refused) | 0 | CC |
| `fl11_timing` | choice: Output-aligned, Detection-early | Output-aligned | - |
Appended in `params/midiout.h`, locked in `tools/param_ids/midiout.lock`, all in `performanceParameterIDs()`. Note-name display follows the DAW convention setting? JUCE displays `MidiMessage::getMidiNoteName (n, true, true, 3)` (C3 = 60); the Help text states the numbering used. State subtree: none. Old sessions: absent -> `fl11_on=false` -> nothing emitted. Neutral proof: with `fl11_on=false` the engine event buffer pointer is null, the scheduler never writes, only `midi.clear()` runs (when the build outputs MIDI); audio bit-identical; with `GAPFILL_MIDI_OUT=OFF` the plugin metadata is unchanged.

## 5 Real-time safety
Audio: fixed arrays (`EngineEventBuffer` 64, pending 64, <= 32 events/block rendered); `juce::MidiBuffer::addEvent` can allocate if the host's buffer is too small, so rendering is capped, event bytes are tiny, and tests run under AllocGuard with a buffer pre-sized like the JUCE wrappers do (`ensureSize (2048)`; confirm the wrappers pre-size in the pinned JUCE 8.0.15 sources before shipping). No locks, strings or logging. Overflow behaviour is the drop rule in section 3(a). Resets: `reset()`, sample-rate change, bypass, transport stop, disabling `fl11_on`, `releaseResources` all call `flushAllNotes`; `prepareToPlay` clears pending. Output order is strictly non-decreasing sample offset.

## 6 UI
Per `11` this belongs in a drawer/Options group, not the header or main tabs: Options page > "MIDI Output". Master toggle with text "MIDI OUT", event checkboxes, note steppers with names ("36 = C1"), channel stepper, timing combo, CC stepper. When unavailable: controls disabled and a one-line reason. Tooltip: "Send MIDI notes from GapFill's detector to the host. Route this plugin's MIDI output to another track in your DAW." Accessible names per control; keyboard operable. A small activity indicator blinks (static outline under Reduced Motion). Help section "MIDI output" (order 1200) with the per-host table filled from QA and a note that some hosts ignore MIDI from effect plugins.

## 7 MIDI/automation/CLAP/host impacts
- Declaration is **build-time and plugin-wide**: VST3 adds an event output bus, AU metadata and CLAP note ports follow `producesMidi`/`acceptsMidi`. GapFill already declares MIDI input (`NEEDS_MIDI_INPUT TRUE`) for CC learn; adding output changes how hosts present the plugin and must go through the full format matrix (pluginval level 10, auval, clap-validator) on every OS.
- VST3: notes travel as `Vst::Event`; the VST3 spec has no native MIDI CC message, so CC output relies on the JUCE wrapper's legacy CC event path and host support varies (community discussion: https://forum.juce.com/t/proposal-extension-for-native-vst3-midi-support/65277/16). Treat CC out as best-effort and verify per host; notes are the primary deliverable.
- AU: output needs a host that accepts MIDI from an AU effect (`aumf`); Logic Pro and GarageBand are not expected to route it. CLAP: verify clap-juce-extensions advertises an output note port when `producesMidi()` is true and test in Bitwig/REAPER. Standalone: no MIDI output device in v1.
- Host support is unverified until tested: required rows REAPER (Windows, Linux, macOS), Bitwig, FL Studio, Ableton Live (VST3 effect), Cubase or Logic where available. Results go to `docs/progress/F-L11.md`; unsupported hosts are listed in Help.
- Latency/PDC: unchanged. Hosts that do not delay MIDI by reported plugin latency need `fl11_timing = Detection-early`.
- Automation: all parameters automatable; changing note numbers mid-note flushes the old note first.

## 8 Tests
Group `MidiOut`: `midiout_off_no_events_and_audio_identical` | `hit_noteon_offset_exact_all_block_sizes` | `hit_velocity_follows_strength_range_1_127` | `fixed_velocity_100` | `gap_note_spans_gap` | `fill_note_spans_fill_length` | `retrigger_emits_off_before_on_same_sample` | `pending_overflow_drops_noteon_not_noteoff` | `event_cap_per_block_32` | `cc_rate_limited_and_deduplicated` | `cc_refuses_channel_mode_numbers` | `early_mode_hit_offset_within_hop` | `no_input_echo_midi_cleared` | `flush_on_disable_bypass_stop_reset_samplerate` | `no_stuck_notes_random_sequences` (1000) | `note_change_midnote_flushes_old` | `output_sorted_by_offset` | `midi_params_roundtrip_old_state` | `midiout_no_audio_thread_allocation` (presized buffer) | `producesMidi_follows_build_option`. Host rows: pluginval 10, clap-validator, auval (if AU), manual per-host matrix above.

## 9 Dependencies on other tickets
Hard: B3 (stages; emission points), B4, build gate in B1 (CMake registration). Soft: F-L13 (sidechain events as the Hit source), F-S12 (panic flushes notes), F-L09 (none). Independent of F-L01..F-L10. Gate A must be signed off first for every format (the declaration alters VST3/AU/CLAP metadata). Build order: after Gate B and the full format matrix is green.

## 10 Effort & risks
Effort L. **Owner decision needed**: enable `GAPFILL_MIDI_OUT` in release builds (single plugin, hosts may show an extra MIDI output; some hosts mis-classify effects with MIDI out) or ship it only as a separate build/variant. Recommendation: keep the option OFF in releases until REAPER + Bitwig pass and Ableton/FL behaviour is documented, then enable. Risks: host variance (largest), possible AU type/category side effects, `MidiBuffer` allocation behaviour, users routing output back into GapFill's own input (feedback loop; ignore own notes by design because GapFill only reads CC), latency alignment confusion.

## 11 Acceptance checklist
- [ ] Twelve IDs appended, locked, classified; CC 120+ refused
- [ ] Off by default; build option documented; metadata unchanged when OFF
- [ ] Exact note timing at all block sizes; no stuck notes in the random invariant test
- [ ] Input MIDI never echoed; learn CCs still work
- [ ] AllocGuard green with a presized host-style buffer
- [ ] Format validators (pluginval 10, clap-validator, auval) green with the option ON
- [ ] Host matrix recorded with unsupported hosts listed in Help
- [ ] Options-page UI, tooltips, accessibility and disabled-state explanations done
