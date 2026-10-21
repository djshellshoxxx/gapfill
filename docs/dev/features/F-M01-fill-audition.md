# F-M01 Fill Audition (enhancement spec over E-1 / U-3)

Status: implementation-completion spec. Canonical behaviour: `03-SPEC-ESSENTIALS.md` E-1 (engine, threading, tests) and `05-SPEC-USABILITY-VALUE.md` U-3 (MIDI note trigger = F-S04). This file does NOT restate them; it lists gaps found against the code at master and fixes them. Read E-1 first.

## 1 Summary & user-visible behaviour
- A button plays one representative fill now from the last ~0.5 s of audio through a dedicated preview voice, without touching gap counters, history, Lock, Like or later natural fills. Everything in E-1 "Does / Does NOT" table applies unchanged.
- Added/clarified here:
  - Label. A parameter `audition` already exists and means "Audition Detector" (`Parameters.h` line 62, `Panels.cpp` `GapToggle audition`, Help). The new command must not reuse that ID, and the two labels must not collide on screen. Button text: `AUDITION FILL`; accessible name "Audition fill (play one preview fill)". Existing toggle keeps `AUDITION DETECTOR`. Help sections are titled separately ("Audition fill" vs "Audition detector"). (Owner decision D1 below.)
  - Preview ignores the probability stages only: it does not apply `fill01` or `density01` or the Breathing/Phrase/Evolve decision stage (those are probabilities or schedule shapers, not character). It does apply block-stage effective settings (personality F-M06, macros), Fill Type, weights, complexity, chaos, intensity, divisions, band settings, wet/dry/width/output/protection duck. Preview at Fill = 0 still plays. Wet = 0 is silent (E-1 test).
  - Bypassed: command is drained and discarded in BOTH `processBlock` and `processBlockBypassed` (the latter currently has no command drain) so a stale press cannot fire after un-bypass.

## 2 Success / quality metric
- Latency: first non-zero preview sample is emitted within 1 analysis hop (32 samples) of the audio thread draining the command (offline test, 48 kHz, block sizes 32/512/4096).
- Isolation: scenario A rendered with 3 auditions in the middle has natural-fill start positions and wet samples identical (bit-exact) to the no-audition render outside the preview voice's own output.
- First-run usability (manual, 5 testers, no docs): >= 4 of 5 hear a fill within 15 s of opening the plugin on a playing loop.
- No audio-thread allocation; extra memory < 1 MB at 192 kHz stereo (E-1 budget; record in `docs/progress/F-M01.md`).

## 3 Design
Follows E-1 "Engine design" 1-6. Gap-fixes and exact interfaces:
- Files: `Source/features/audition/{Audition.h,Audition.cpp,Gui.h,Gui.cpp,Help.cpp}`, `Tests/features/TestAudition.cpp`, `cmake/sources/audition.cmake`, `cmake/tests/audition.cmake`.
- `Command` (F0-4) gains `std::uint32_t issuedMs`. Staleness test uses unsigned subtraction `(uint32_t)(nowMs - issuedMs) > 500u`, so the 49.7-day `getMillisecondCounter()` wrap is safe.
- `bool FillEngine::startPreview (std::uint8_t velocity, std::int64_t outputPos, const AnalysisEngine&, const SourceHistory&, const EngineSettings& blockStageSettings) noexcept` (E-1 signature, `EngineSettings` is the block-stage result, NOT `sLocal`).
- `chooseAlgorithm` reads `recentAlgos` and `lastEventTransient`; the preview calls it with a private `Rng` and MUST NOT write `recentAlgos`, `lastEventTransient`, `lastFillStartPos`, `gapIndex`, `fillsStarted`. Add `const`-correct overload or a `PreviewScope` RAII asserting those fields are unchanged in debug builds.
- Source anchor: the history may be latched (Capture) so use the same expression as natural fills `outputPos - s.historyOffset`, clamped; with F-M08 active the active source view is used (see F-M08).
- Spectral algorithm already claimed by a natural fill: `startVoice` falls back to Granular exactly like natural fills (shared stage); status stays "Playing".
- Processor hooks (minimal): top of `processBlock` and `processBlockBypassed`: `commandDrain.drain (...)`; `MeterState` already gets `previewStatus` and `blockCounter` (F0-4). Editor hook: slot component in `CreativeExtra`.
- Data flow: GUI click -> heartbeat check (`blockCounter` advanced in last 400 ms) -> `commands.push(Preview)` -> audio drain (coalesce, stale-discard) -> `startPreview` -> `previewVoice` renders in hop loop -> `previewStatus` atomic -> editor timer shows status text 2 s.

## 4 Parameters & state
- NEW APVTS IDs: none. Audition is a command (E-1, creative spec 8), deliberately not automatable. Do not create `fm01_*` parameters.
- State: nothing serialised (no preview/command state). MIDI trigger note/channel live in `<Perf>` (U-3, owned by F-S04). MIDI-CC learn on the button uses the existing CC edge rule (rising >= 64, re-arm < 48).
- Neutral proof: with no command ever pushed, `previewVoice.isActive()` is false, the hop loop adds one predictable branch, and golden scenarios A-F are bit-identical (test `audition_idle_golden_unchanged`).
- Old sessions: no state change; `stateVersion` untouched.

## 5 Real-time safety
- Audio thread: command drain (<= 16 items, collapse to one), `startPreview` (same cost as one natural `trySchedule` start incl. <= 320 ms `captureSource` memcpy), preview voice render (one extra voice per hop), two relaxed atomic stores.
- Bounds: preview length <= min(maxFill, 2000 ms), hard cap 5 s; one preview voice; restart fades old voice over 5 ms.
- Forbidden and absent: allocation, locks, strings, logging. `AllocGuard` test mandatory.
- Reset table (06 s1 rule 6): `reset()`, `releaseResources`, bypass, sample-rate change, transport jump (preview NOT cut by transport jump; natural `panic()` on jump currently fades all pool voices; preview voice keeps playing unless stopped transport, decision D2), all use `requestStop(48)`.

## 6 UI
- Placement: `CreativeExtra` slot next to CAPTURE (docs/dev/11: no new header row). At minimum width the slot wraps below CAPTURE; both keep >= 24 px hit height.
- Tooltip: "Play one fill right now from the last few seconds of audio, using the current settings. Needs audio playing through the plugin."
- Help (registered order 1100): "Audition fill plays a one-off preview. It does not count as a real fill, is not saved in History, and is ducked by hit protection exactly like a real fill. For hearing what the detector hears, use Audition Detector."
- Status texts: exactly the E-1 table (5 rows) shown in `FooterStatus` for 2 s.
- Accessibility: name above; description = tooltip; state announced via `AccessibilityHandler` title change to "Audition fill, playing" while active. Keyboard: Tab-focusable only when keyboard navigation is enabled (reuse the existing `applyKeyboardNavigationSetting` mechanism), Space/Enter fires. Reduced Motion: active state is a static filled outline, no pulse.
- Visualiser: preview drawn in preview colour with hatch pattern (not colour only), via `VizFrame::flagPreview`.

## 7 MIDI / automation / CLAP / host
- Not a host parameter: no automation lane, no CLAP param. CC learn through the existing right-click menu stores the mapping in `MIDIMAP` using a reserved command pseudo-ID (`cmd.audition`), validated on load, unknown IDs ignored (forward compatible). `MidiLearnManager` is parameter-index based; extend with a small command table (<= 8 entries) in one place and share with F-M10/F-M12. This extension is the shared prerequisite "command learn", build it once.
- `producesMidi()` stays false. Hosts that stop calling `processBlock` when transport stops (REAPER default option) show "Start playback to audition".
- Standalone: identical path; no device -> same message.

## 8 Tests (`Tests/features/TestAudition.cpp`)
All 16 tests listed in E-1 plus: `audition_idle_golden_unchanged` | `audition_discarded_in_bypassed_path` | `audition_stale_uint32_wrap` | `audition_plays_at_fill_zero` | `audition_ignores_density_and_breathing` | `audition_uses_personality_weights` | `audition_spectral_claimed_falls_back` | `audition_leaves_recent_algos_untouched` | `audition_midi_command_learn_roundtrip`. Fixtures: scenario A; drum loop with 1 s silence prefix (NoSource path); spectral scenario B with all bands off (NoBand).
Edge: command pushed while queue full (returns false, UI shows "busy"); two presses in one block; press during sample-rate change.

## 9 Dependencies & build order
Needs F0-2, F0-3, F0-4, F0-9. Unlocks F-S04 (MIDI note trigger), F-M10 (shares dedicated-voice pattern and command-learn), F-L04/F-L23. Build FIRST among F-M tickets. Shares `Command` struct growth with F-M08 (Recapture).

## 10 Effort & risks
Effort L (E-1 estimate; M if the shared command-learn extension already exists). Risks: (1) extra `FillVoice` memory (measure); (2) `previewVoice` and pool `spectral` claim race (single audio thread, no race, but ensure release on all stop paths); (3) naming confusion with Audition Detector (D1); (4) transport-jump panic semantics (D2).
Owner decisions: D1 button label `AUDITION FILL` vs renaming the old toggle to `LISTEN`; D2 whether transport jump cuts a running preview.

## 11 Acceptance checklist
- [ ] All E-1 tests plus the 9 added tests pass; zero skipped
- [ ] Golden A-F unchanged; fuzzed pluginval level 10 passes
- [ ] No new APVTS IDs; `audition` (detector) untouched
- [ ] AllocGuard clean; memory delta recorded
- [ ] Works in REAPER (playing + stopped), Ableton, standalone (manual script M-3)
- [ ] Label, tooltip, Help, accessible name reviewed at min size 1020x620
