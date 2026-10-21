# F-L24 Controller integration and feedback

Status: specified, not approved. Size L. Category LIVE/VAL, value H.
Overlap: `10` section 4.3 ("Macros and controllers", "Momentary/latch commands", "Safety and status") defines the product rules; `04` C-2 defines macro learn; `05` U-3 defines the audition note trigger; F-M10 defines momentary/latch effect behaviour; F-L11 (MIDI output from detection) shares the MIDI-output declaration. Existing code (`Source/MidiLearn.{h,cpp}`): CC to parameter only, all channels, one CC per parameter, coalesced per CC, bounded 10 s learn, mappings in a `MIDIMAP` child tree. This file adds what is missing and keeps `MIDIMAP` untouched.

## 1 Summary and user-visible behaviour
1. **Command bindings** (new): a Note, CC or Program Change can fire a named command (Panic, Resume, Bypass, Capture, Recapture, Audition, Lock, Surprise, Undo, Last-good recall, Scene next/previous/select, and later tickets' commands) in Trigger, Toggle or Momentary mode, learnable by right-click on the control or in a Controllers panel.
2. **Channel filter and conflict warnings**: parameter CC mappings may be limited to a MIDI channel (default Omni, as today); learning a CC already used shows a visible duplicate warning instead of silently stealing it.
3. **Takeover**: Jump (today's behaviour, default) or Pickup (ignore a CC until it crosses the current value) per mapping.
4. **Feedback**: values and indicators are sent back to the controller (CC echo for knobs/faders, Note/CC for LEDs) when a MIDI output is available.
5. **Profiles**: named sets of the above, saved as files, with a Generic profile.
6. **Disconnect recovery**: Standalone detects device loss/return, keeps mappings, releases held states safely, resyncs on return. In plug-in mode the host owns devices; held states time out safely.
7. **Monitor**: "last message received" readout for diagnosing mappings.
Everything is off or equal to today until the user loads a profile or adds a binding.

## 2 Success / quality metric
- No profile and no bindings: audio and MIDI behaviour identical to current build (existing MIDI-learn tests unchanged and green; golden unchanged).
- Panic via MIDI acts within the same audio block that contains the message (test measures voice fade starts at the message's sample offset block).
- Feedback: no more than 30 messages/s per CC and 512/s overall; no feedback loop: with a loopback test device, 10 s of continuous CC input produces no sustained oscillation (echo count after input stops is 0 within 300 ms).
- Reconnect: after simulated loss and return, full resync completes within 500 ms and every mapping works without user action.
- Profile file round-trip and invalid-file rejection never alter current mappings (atomic).

## 3 Design
Files under `Source/features/controller/`: `ControllerModel.{h,cpp}` (pure data + validation), `CommandRouter.{h,cpp}` (audio-thread table + dispatch), `Takeover.{h,cpp}`, `FeedbackEngine.{h,cpp}` (message thread diff + rate limit), `MidiSink.{h,cpp}` (plug-in and Standalone adapters), `DeviceWatcher.{h,cpp}` (Standalone), `ProfileStore.{h,cpp}` (files), `Gui.{h,cpp}`, `Help.cpp`.

### 3.1 Model
```cpp
enum class MsgType : std::uint8_t { Note, CC, ProgramChange };
enum class BindMode : std::uint8_t { Trigger, Toggle, Momentary };
enum class ControlCommand : std::uint8_t { Panic, Resume, Bypass, Capture, Recapture, Audition, LockToggle,
    Surprise, Undo, LastGood, SceneNext, ScenePrev, SceneSelect, Hold, Count };  // availability follows shipped tickets
struct CommandBinding { ControlCommand cmd; MsgType type; std::uint8_t channel /*0 omni,1..16*/, number;
                        BindMode mode; std::uint8_t arg /*scene index*/; };
struct ParamBinding   { char id[32]; std::uint8_t channel, cc, takeover /*0 jump,1 pickup*/; };
struct Indicator      { ControlCommand state; MsgType type; std::uint8_t channel, number, onValue /*127*/, offValue /*0*/; };
struct ControllerProfile { char name[24]; std::vector<CommandBinding> cmds /*<=32*/; std::vector<ParamBinding> params /*<=64*/;
                           std::vector<Indicator> indicators /*<=16*/; bool feedback; std::uint8_t txChannel; char deviceName[32]; };
```
Existing `MIDIMAP` entries are the implicit "legacy" parameter bindings (omni, jump); profile `ParamBinding`s are applied on top through `MidiLearnManager::setMapping` plus two new per-CC atomic arrays (`ccChannel[128]`, `ccTakeover[128]`, default 0 = omni/jump). Minimal hooks in `MidiLearn.cpp`: channel check before the table lookup and a pickup check in `applyPendingChanges` (message thread, where the current parameter value is known): pickup arms a CC as "waiting", accepts it when `|cc/127 - normalised| <= 2/127` or when consecutive values straddle the parameter value; any non-MIDI change of that parameter re-arms waiting.

### 3.2 Command router (audio thread)
Lookup table `std::atomic<std::uint8_t> table[3][17][128]` (type x channel(0 omni) x number), value `0` none else `(command+1)` with mode bits in a parallel table; written by the message thread only. `processMidi` additionally scans notes, program changes and CCs: Trigger fires on rising edge `value >= 64` (CC) / note-on, re-arms below 48 (CC) / note-off; Toggle flips an audio-owned bool; Momentary sets true on press and false on release, with a timeout `momentaryTimeoutMs` (default 8000, range 1000..30000, checked per block against `samplesProcessed`) that forces release. `CC 123` and `CC 120` (all notes/sound off) release every momentary state. Commands that only the audio engine needs (Panic, Resume, Bypass, Capture, Recapture, Audition, Hold) are executed directly in `processBlock` (as F0-4 specifies for MIDI triggers, no queue); commands that need the message thread (Undo, LastGood, Surprise, Scene, Lock via history) are pushed to a new `SpscQueue<UiCommand, 32>` drained by the processor's timer; full queue drops the newest and counts. Panic is never delayed by quantisation or smoothing (rule in `11`).
Conflicts: a (type, channel, number) bound to a command cannot also be a parameter CC; the learn UI refuses it with text ("CC 74 is used by Density"), unless the user confirms replace.

### 3.3 Feedback
`FeedbackEngine` runs on the message thread at 60 Hz. Sources: `AudioProcessorValueTreeState` listener flags for bound parameters (dirty bit set in `parameterChanged`, no work in the callback), and indicator atomics published by the audio thread (panic active, bypass, capture, hold, scene index, safe state). Each tick it builds `OutEvent {status, d1, d2}` for dirty entries: knobs/faders as CC on `txChannel` (value `round (127 * normalised)`), indicators as Note On/Off or CC per `Indicator`. Rules: per-CC at most 30 Hz and only when the 7-bit value changed; global cap 512 msgs/s (token bucket); echo suppression: nothing is sent to a CC for 250 ms after a value was received on it from the controller (prevents motorised-fader fights); full resync on profile load, device connect and editor open, spread over 200 ms. Colour/LED semantics are device specific (many pad grids use Note On velocity as a colour index, e.g. the Novation Launchpad MK2 programmer's reference); the engine only sends the `onValue/offValue` the profile defines, so any device is supported by data, not code.
Sinks: **plug-in** (`JucePlugin_ProducesMidiOutput` builds): `SpscQueue<OutEvent, 256>` message to audio, appended to the host `MidiBuffer` at sample 0, at most 64 events per block. **Standalone**: `juce::MidiOutput` opened on the message thread; `sendMessageNow` called from the same timer (never from the audio thread). Host support for plug-in MIDI output varies (many hosts do not route effect-plug-in MIDI to hardware; AU effects cannot output MIDI): feedback is best-effort there and Standalone is the reliable path. Build flag `GAPFILL_MIDI_FEEDBACK` (CMake option) controls `NEEDS_MIDI_OUTPUT` / `producesMidi()`; see owner decision in section 10.

### 3.4 Disconnect recovery (Standalone)
`DeviceWatcher` uses `juce::MidiDeviceListConnection` (JUCE 8) on the message thread. Loss of the profile's device: set status "Controller lost: <name>", release momentary states through the router, keep every binding. Return: reopen input/output, full feedback resync, status cleared. Plug-in mode: no device concept; the monitor shows time since the last message and the momentary timeout covers a lost note-off.

### 3.5 Profiles
`.gfcontroller` XML in `<Documents>/GapFill/Controllers/`, <= 16 KiB, version attribute, all numbers range-checked, unknown commands dropped individually, duplicates rejected, atomic apply (validate all, then swap). Factory "Generic 8-knob (CC 70-77)" maps eight creative parameters; no device-specific profile is shipped without hardware verification. Current profile name and bindings are stored in the session (`<Controllers>`, 3.6).

## 4 Parameters and state
No APVTS parameters (configuration, not automation). Subtree `<Controllers v="1">` registered in `StateSubtrees`, <= 6 KiB: `profile` name, `cmds`, `params` (channel/takeover only; the CC itself stays in `MIDIMAP`), `indicators`, `feedback`, `txChannel`, `momentaryTimeoutMs`, `deviceName` (an optional local device name; "Export without device name" is offered when saving profiles meant for sharing). Invalid subtree dropped to defaults while the rest of the state loads. Absent subtree: no command bindings, omni, jump, feedback off: today's behaviour.

## 5 Real-time safety
Audio thread additions: a bounded loop over MIDI events with O(1) table lookups, direct engine calls for the listed commands, queue pushes (fixed capacity, drop-newest with counter), appending at most 64 output events. No allocation, no strings, no locks, no device calls. Table updates are single-byte atomics. Output queue empty/full/wraparound tested. Timeout bookkeeping uses integer sample counts.

## 6 UI
Controllers panel in the Options view (slot `OptionsExtra`) plus a compact monitor in the Performance tab. Contents: profile combo with Save/Load/Reset, table of bindings (control, message, channel, mode, takeover, Delete), "Learn" per command row, monitor "Last: CC 74 ch 1 value 90", feedback toggle, TX channel, Momentary timeout. Parameter learn stays on right-click as today; its menu gains "Channel..." and "Takeover: Jump/Pickup". Tooltips and a duplicate-warning line ("CC 74 already controls Density: replace?"). Help section order 1240 "Controllers" including the hardware-feedback limitations per host. Accessible names for every row and button; table is keyboard navigable (arrows, Enter to learn, Delete to remove). Learn still times out at 10 s with a visible countdown (existing). Reduced motion: countdown as text. Narrow: table becomes a list with detail popover.

## 7 MIDI / automation / CLAP / host
Input handling does not change latency. Output declaration is a host-visible capability: VST3 event output bus, CLAP note-port out, AU MIDI output is not available for effect-type plug-ins. Hosts may apply different routing rules (unverified; QA matrix: REAPER, Bitwig, Ableton Live, FL Studio, Logic, Cubase). MIDI Program Change reception depends on host forwarding. Bindings for commands never create host automation lanes.

## 8 Tests (`Tests/features/TestController.cpp`; existing MIDI-learn tests unchanged)
`no_profile_identical_behaviour` | `command_table_lookup_omni_and_channel` | `trigger_rising_edge_and_rearm_hysteresis` | `toggle_and_momentary_modes` | `momentary_timeout_releases` | `cc123_cc120_release_all` | `panic_executes_in_same_block` | `ui_queue_overflow_counts_drops` | `duplicate_binding_refused_or_confirmed` | `channel_filter_param_cc` | `pickup_arms_and_crosses` | `pickup_rearms_on_external_change` | `feedback_value_mapping_and_rate_limits` | `echo_suppression_250ms` | `loopback_no_oscillation` | `resync_on_connect_spread` | `output_queue_empty_full_wrap_64_per_block` | `device_loss_release_and_reconnect_resync` (mock device list) | `profile_roundtrip_and_malformed_rejected_atomically` | `profile_size_cap` | `subtree_roundtrip_and_invalid_dropped` | `legacy_midimap_state_loads` | `no_audio_thread_allocation`. Manual: hardware pad controller and a motorised-fader controller on at least one OS (unverified in CI).

## 9 Dependencies and order
F0-4, F0-6, F0-9. Commands become available as their tickets ship (E-1, C-10, C-5, U-4/F-S11, F-L07 scenes, F-L25 panic/last-good, F-L17/F-L23/F-L16 optional). Build order: model + router + profiles, then takeover/channel, then feedback sinks, then device watcher. F-L25 depends on Panic/Resume bindings. Coordinate `producesMidi` with F-L11.

## 10 Effort and risks
L. Owner decision: enabling MIDI output changes host-visible capabilities of the shipping plug-in; recommended default is a CMake option, ON for Standalone, OFF for official VST3/CLAP/AU builds until host QA proves it harmless. Risks: motor-fader/LED fighting (echo suppression and rate limits), vendor-specific LED protocols (data-driven profiles only), hosts that swallow Program Change, privacy of device names in shared sessions.

## 11 Acceptance checklist
- [ ] With no profile, behaviour and existing tests identical
- [ ] Command modes, timeouts and all-notes-off verified; Panic same-block
- [ ] Duplicate warnings and channel/takeover work with MIDI Learn
- [ ] Feedback rate limits, echo suppression and loopback tests pass
- [ ] Device loss/return handled (mock) and checked on real hardware where available
- [ ] Profile files validated atomically; sizes capped
- [ ] Plug-in MIDI-output declaration decision recorded; host results labelled verified/unverified
- [ ] UI keyboard/screen-reader review at minimum size
