# WP4 - Audition, recipes, export (E-1, U-3, C-5, U-1, C-11, U-2, F-S06) progress

Branch: `worktree-agent-a8709bc4b978eb938` (not pushed). Integration branch merged first: `claude/inspiring-fermi-pv9gv5`.
Neutral defaults keep the golden scenarios and the 0.0.1 state fixtures byte-for-byte unchanged (golden tests pass untouched).

## Status per ticket

| Ticket | Status | Notes |
|---|---|---|
| E-1 Fill Audition | Done (engine, CC trigger, UI, help, tests) | see Deviations E-1 |
| U-3 MIDI note trigger | Done | settings live in `<Perf>`; menu on the AUDITION button instead of the Options page (see Deviations) |

## Files (WP4 owned)

* `Source/features/audition/` - `PreviewChannel.*` (preview voice mailbox, status packing), `AuditionEngine.cpp` (`FillEngine::startPreview` and friends, defined outside the hot file), `Audition.*` (command handler, stale rule), `MidiTrigger.*` (note / CC trigger, learn, state), `AuditionGui.cpp` (AUDITION button + footer status slot components), `Help.cpp`.
* `Source/features/recipes/` - `RecipeServices.*` facade held by the processor (one member, a few hook calls), `RecipeState.cpp` (`<Perf>` subtree row).
* `cmake/sources/audition.cmake`, `cmake/tests/audition.cmake`, `Tests/features/TestAudition.cpp`.
* Help section orders reserved for WP4: 5010 audition, 5020 recipes, 5030 export, 5040 A/B match.

## Hot-file touches (hook lines only, all justified by "code reality")

* `FillEngine.cpp`: preview prepare/reset/panic/render lines, `servePendingPreview` call, protection lambda shared with the preview voice, preview flag in the viz feed, recipe records the voice's modifier mask and pitch (`startVoice`).
* `PluginProcessor.cpp/.h`: one `RecipeServices` member and accessor; `recipeServices.onMidi` after the MIDI-learn scan; `recipeServices.tick`; `onFillEvent` in `drainFillEvents`; `engineSettings.bypassed`; `meters.previewStatus` store.
* `FeatureHooks.cpp`: one `audition::handleCommand` line in `dispatchCommand`.
* `StateSubtrees.cpp`: the `Perf` row. `EngineEvents.h`: `Command::issuedMs`, `FillEvent::lengthSamples`, `FillEventType::LockSet/PreviewStarted`. `EngineSettings.h`: `bypassed`. `FillVoice.h`: `getModifierMask/getPitchSemis` getters. `WaveformGapView.cpp`: preview colour and "PREVIEW" caption.

## Deviations (E-1, U-3)

* **Restart fade uses a tail buffer, not a second voice.** One `previewVoice` plus a 5 ms stereo tail: on restart the old voice is rendered into the tail with a 5 ms fade and mixed over the next 5 ms while the new voice starts. Keeps the extra memory under the 1 MB budget (about 0.6 MB at 192 kHz stereo: 2 x 0.32 s capture + 2 x 0.06 s comb floats + tail).
* **Preview recipe replay is character-exact, not sample-exact** (recipes store decisions, not the 64-bit voice seed). A fresh seed per press is intended ("restart with a new variation").
* **Sample-rate change hard-stops the preview** (the voice buffers are reallocated and the host flushes anyway). `panic()`, `reset()`, bypass and transport stop fade in 4 ms / 48 samples.
* **Status code packing**: `MeterState::previewStatus` carries status | algo | sequence (see `PreviewChannel.h`), so the footer can show `Preview: <Algorithm>` and a 2 s rejection even when the same reason repeats.
* **UI placement**: AUDITION lives in `Slot::CreativeExtra` (order 110, shares the capture cell with CAPTURE) and the status line in `Slot::FooterStatus`. The U-3 Trigger note / Channel rows and the CC learn are in the AUDITION button's right-click menu because `Slot::OptionsExtra` has no reserved rectangle (reserving it edits `PluginEditor.cpp`, owned by WP11). WP11 can add the Options rows later; the settings API is `RecipeServices::trigger`.
* **MIDI scan** is a separate pass (`RecipeServices::onMidi`) next to `midiLearn.processMidi`, not merged into the same loop; bounded and allocation free (raw bytes, no `MidiMessage`).
* **CC learn on the button** is handled by `MidiTrigger` (command ids are not parameters, so `MidiLearnManager` cannot hold them). X-1 (WP6) can migrate it into the command map.
* **Spectral preview** claims the shared `SpectralSmear`; a natural fill that starts while a Spectral preview plays falls back to Granular (same rule as two natural fills). Documented limitation; not hit in the "does not change next natural fill" test.
* `TestSlots` count assertion for `CreativeExtra` relaxed from `== 1` to `>= 1` because AUDITION now sits beside CAPTURE (the CAPTURE placement test is unchanged).
