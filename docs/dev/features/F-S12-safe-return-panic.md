# F-S12 Instant safe return / panic
Status: implementation spec, Gate C candidate. Category LIVE/ESS, value H, size M. Canonical context: `10-SPEC-LIVE-CREATIVE-EFFECTS.md` section 4.3 "Safety and status" and `11-GUI-PLACEMENT-AND-FEATURE-PLAN.md` rule 5. Those state the requirement; this file is the implementation spec. It also defines the shared **command-binding module** that F-S04, F-S06, F-S07, F-S08 and F-S11 reuse.

## 1 Summary & user-visible behaviour
- **PANIC** button (always visible on every tab), keyboard `Ctrl/Cmd + .`, and an optional MIDI binding. One press: all active fills fade out (4 ms, existing `FillEngine::panic`), queued previews/commands are discarded, Stutter Storm / Glitch Roulette are cancelled, and GapFill enters **Held** mode: no new fills are scheduled and the wet bus is muted. Dry audio and output trim are untouched (the signal the performer already hears keeps flowing). The button becomes **RESUME**; status line: "PANIC: fills held. Press Resume".
- **RESUME** (button, same key, or same MIDI binding pressed again) ramps the wet bus back in over 30 ms; fills restart on the next qualifying gap. Optional auto-resume after Off / 2 s / 5 s / 10 s (default Off).
- Panic never waits for quantization, transport, or the editor being open (MIDI path works with the editor closed).
- MIDI channel-mode **All Sound Off (CC 120)** performs a non-latching cancel (voices fade, no Held mode); CC 123 is ignored (GapFill plays no notes). Option "Respond to All Sound Off" default on.
- Latch is **not saved** with the project; loading a project or preset always starts Normal, so nothing ever reopens muted.

## 2 Success/quality metric
- From request to wet bus <= -60 dB: <= 8 ms at every sample rate (44.1-192 kHz), at any host block size up to 8192 (request is honoured at the start of the next processing chunk).
- Zero audible click: max |sample delta| of the output in the 20 ms after panic is <= that of the same run without panic + 6 dB on a dry sine, wet at 100 %.
- Dry path bit-identical to a run with wet = 0 during Held mode. No change to reported latency.

## 3 Design
New modules (cmake fragments `cmake/sources/commands.cmake`, `cmake/sources/safe.cmake`):
**3a `Source/features/commands/CommandBindings.{h,cpp}` (shared)**
```cpp
enum class CommandId : std::uint8_t { None, Panic, Resume, Audition, StutterStorm, GlitchRoulette,
                                      RouletteRestore, PresetNext, PresetPrev, AbToggle, NumCommands };
struct Binding { enum class Kind : std::uint8_t { None, CC, Note } kind = Kind::None; std::uint8_t number = 0; std::int8_t channel = -1; /* -1 any, 0-15 */ };
class CommandBindings {
public:
    void processMidi (const juce::MidiBuffer&) noexcept;             // audio thread
    struct PressInfo { std::uint8_t velocity = 0; int sampleOffset = 0; };
    bool takePressed (CommandId, PressInfo* = nullptr) noexcept;     // consume coalesced press (offset used by F-S04)
    bool isHeld (CommandId) const noexcept;                          // momentary state
    void setBinding (CommandId, Binding); void clearBinding (CommandId); Binding getBinding (CommandId) const; // message thread
    void startLearn (CommandId); void cancelLearn(); bool isLearning() const noexcept;
    bool advanceLearnTimeout() noexcept;                              // 15 Hz tick from processor timer, 10 s, as MidiLearnManager
    juce::ValueTree toValueTree() const; bool validate (const juce::ValueTree&) const; void restore (const juce::ValueTree&);
};
```
Rules: press = CC >= 64 (re-arms below 48, the E-1 rule) or NoteOn velocity > 0; held = between press and release (NoteOff/vel 0/CC < 48); presses coalesce to one pending bit per command in a `std::atomic<std::uint32_t>`; velocity in `std::atomic<std::uint8_t>[NumCommands]`. `CommandId` is append-only and other tickets add values (for example F-M19 `ScrubHold`, F-M18 `BattleStore1/2`); the table therefore holds 32 `std::atomic<std::uint32_t>` words (kind|number|channel packed) so the audio thread never takes a lock. Learn is a separate session from parameter CC learn: starting one calls the other's `stop`. Conflicts: assigning a CC already mapped to a parameter shows "CC 20 also controls Fill, replace?" (needs the small hook `int MidiLearnManager::getParameterIndexForCC (int cc) const`); one control per CC, deterministic. Message-thread commands (PresetNext, AbToggle, RouletteRestore) are drained by `GapFillAudioProcessor::timerCallback` via `takePressed`.
**3b `Source/features/safe/PanicController.{h,cpp}`**
```cpp
class PanicController {
public:
    void requestPanic() noexcept; void requestResume() noexcept; void requestCancelOnly() noexcept;  // any thread
    struct Block { bool holdFills; float wetGainTarget; bool cancelNow; };
    Block update (int numSamples, double sr) noexcept;                // audio thread, once per chunk
    bool isHeld() const noexcept;                                      // relaxed atomic
    void setAutoResumeSeconds (int) noexcept; void clearLatch() noexcept;
};
struct CancellableFx { virtual void cancelFromAudioThread() noexcept = 0; };   // storm, preview
```
`processChunk` hooks (3 lines): `const auto pc = panic.update(...)`; if `pc.cancelNow` call `engine.panic()` and every registered `CancellableFx` (fixed array of 8); if `pc.holdFills` set `engineSettings.fill01 = 0` (same mechanism the bypass path uses today); multiply the wet term in the mix loop by a `panicGain` `juce::SmoothedValue<float>` (5 ms down, 30 ms up). `processBlock` and `processBlockBypassed` both call `commands.processMidi (midi)` next to the existing `midiLearn.processMidi`.
**3c Audit of host bypass and state (findings must be re-verified in code before merge)**
1. Host bypass (`getBypassParameter` -> `processBlockBypassed` -> `processChunk(..., true)`) already calls `engine.panic()` each chunk; Held mode is orthogonal: a latch set before bypass is still set after un-bypass (test).
2. Plugin `bypass` parameter and host bypass share one path; commands are processed in both, so Resume works while bypassed.
3. `reset()`/`prepareToPlay` (sample-rate change, transport stop/start in some hosts) must not clear the latch; only `setStateInformation` and the constructor do. Transport jump/stop already calls `engine.panic()` (PluginProcessor.cpp ~l.319); unchanged.
4. Parameters keep applying while Held (user can still edit); undo stack and preset loads unaffected.
5. Panic also calls `HoldController::panic()` / `MomentaryEngine::panic()` (F-M10) and `ScrubEngine` (F-M19, a `CancellableFx`); the cancellable registry holds 8 entries.
5b. Message-thread side of Panic: the request also sets a flag drained by the processor's 15 Hz timer, which calls `RouletteSession::restore()` (F-S08) and cancels its auto-revert; audio-thread cancellation never waits for this.
6. Any new voice class (preview voice from E-1, hold voices from F-M10 used by F-S07, scrub from F-M19) must implement `CancellableFx`; a test iterates the registry.

## 4 Parameters & state
No APVTS parameters: panic is a command and not host-automatable (10-SPEC 4.3; E-1 rule). Bindings persist in new versioned subtree `<Commands version="1">` (children `<B id="Panic" kind="CC" number="20" channel="-1"/>`; <= 32 entries, <= 2 KiB, ids must be known, numbers 0-127, channel -1..15, no duplicate source; invalid subtree dropped and reset per F0-6, the rest of the state still loads). Options in F0-6 `<Perf>`: `panicAutoResume` (0/2/5/10, default 0), `respondAllSoundOff` (bool, default true). `StateSubtrees` array gains the `Commands` entry (the only shared-file edit). Old sessions: subtrees absent => defaults => Normal mode, no bindings, identical audio. Reserved prefix `fs12_` not used.

## 5 Real-time safety
Audio thread: one pass over the `MidiBuffer` (O(events), early out when no bindings and no learn active), constant-time `PanicController::update`, a loop over <= 8 cancellables. Only atomics, no allocation, locks, logging or IO. `CommandBindings::processMidi` parses the raw status/data bytes from `MidiMessageMetadata` (`data`, `numBytes`) instead of constructing a `juce::MidiMessage`, because copying a long SysEx message allocates (the existing `MidiLearnManager::processMidi` constructs one per event; a test with a large SysEx guards the new path). Overflow behaviour: a press is a bit, so floods collapse; learn results go to the message thread through a one-slot atomic.

## 6 UI
- Slot `PerformanceStrip` (F0-9), a 28 px row above the footer visible on all tabs; PANIC min 96x28 logical px. Not in the header (docs/dev/11). If the strip does not fit at 1020x620, raise `kMinHeight` in this PR only, with a before/after screenshot (decision flagged).
- Tooltip: "Panic: fade out all fills now and hold them until you press Resume. Your dry signal is not touched." Help section "Panic and Resume" (order 1040). Right-click on the button: Learn MIDI, Clear, "Auto-resume after".
- Accessible: title "Panic", button toggles to "Resume" and the accessible title changes; state announced. Keyboard: `Ctrl/Cmd+.` when the editor has focus; button reachable in Tab order, large focus ring. Held state is conveyed by text, outline and the status line, not colour alone. Reduced motion: no pulse, steady red outline. Narrow: strip never collapses; other strip items overflow first.

## 7 MIDI/automation/CLAP/host impacts
Hosts deliver MIDI to effects differently: Ableton Live does not route MIDI to audio-effect VST3 on audio tracks; Logic needs the AU to be a MusicEffect (JUCE builds `aumf` when `NEEDS_MIDI_INPUT TRUE`, already set in CMake; confirm the `auval` codes from the built bundle, backlog A3); FL Studio needs the MIDI-in port assigned. Record per-host results as manual QA, treating the editor button as the guaranteed path. Not exposed as a VST3/CLAP parameter by design. CLAP: note/CC arrive through the same `MidiBuffer` via clap-juce-extensions (verify in C-02).

## 8 Tests (`Tests/features/TestPanic.cpp`, `TestCommands.cpp`)
`panic_wet_silent_within_8ms_all_rates` | `panic_dry_path_bit_identical` | `panic_no_click_threshold` | `panic_during_each_algorithm` (all 15) | `panic_cancels_preview_hold_and_scrub` | `held_blocks_new_fills_golden_zero_wet` | `resume_ramps_30ms` | `auto_resume_exact_samples` | `latch_survives_host_bypass_toggle` | `latch_survives_reset_and_prepare` | `latch_cleared_by_state_load` | `latch_not_serialised` | `panic_not_delayed_by_quantize` | `all_sound_off_cancels_without_latch` | `cc_hysteresis_64_48` | `press_flood_coalesces` | `learn_times_out_10s` | `parameter_and_command_cc_conflict_detected` | `commands_subtree_roundtrip_and_corrupt_dropped` | `latency_unchanged_by_panic` | `no_audio_thread_allocation`. Fixtures: dry sine + noise bursts, 8-bar drum loop scenario A seed 1. Manual: one DAW per OS plus hardware pad/footswitch.

## 9 Dependencies & build order
Needs B3 (stages), B4 (queues), B5 (slots), F0-6 (subtrees). Build **first** among the performance tickets: F-S04, F-S06, F-S07, F-S08, F-S11 depend on `CommandBindings`; E-1 and F-S07 voices must register as `CancellableFx`. Soft: F-S02 for status messages, F-S03 for settings.

## 10 Effort & risks
M. Risks: a latched mute left on by mistake (mitigations: visible state, auto-resume, never saved); Ableton/Logic MIDI delivery gaps; CC collisions with existing MIDI learn; strip height at minimum window size. Decision needed from owner: confirm Held mode mutes only wet (proposed) rather than also fading dry; confirm `Ctrl/Cmd+.`.

## 11 Acceptance checklist
- [ ] Wet <= -60 dB in <= 8 ms, no click, dry bit-identical, latency unchanged
- [ ] Works with editor closed via MIDI, and while host-bypassed
- [ ] Latch never saved; loading state always Normal
- [ ] Every cancellable voice registered and tested
- [ ] `CommandBindings` unit tests (hysteresis, coalescing, learn timeout, conflicts, persistence) green
- [ ] Button, shortcut, accessible names, screenshot at minimum size reviewed
- [ ] Manual QA in one DAW per OS recorded; unverified hosts listed
- [ ] Help text, tooltips and `<Commands>` migration notes documented
