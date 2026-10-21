# F-M10 Momentary and latch effect controls (also delivers F-S07 One-click stutter storm)

Status: specified (this pass). Size L. Depends on F0-2/3/4, F-S12 (release on panic), F-L07 (QuantClock), E-1 (dedicated auxiliary voice pattern). Refines `10` section 4.3 "Momentary/latch commands".

## 1. Summary and user-visible behaviour
Buttons (and MIDI notes) that play an effect **while held** (Hold mode) or until pressed again (Latch mode): **Stutter**, **Storm**, **Reverse**, **Tape stop**, **Freeze**. They sound immediately (or quantised to a note division), cut from the live source, stay out of the way of the original hits through the normal protection duck, and end with a short fade on release. **Storm** is a stutter whose slices get shorter the longer it is held (1/8 -> 1/16 -> 1/32, never below 8 ms). A lost release (stuck note, lost mouse-up) times out after a configurable hold limit. Nothing is saved in the project except the settings.

## 2. Success / quality metric
| # | Metric | Target |
|---|---|---|
| M1 | Press-to-sound latency (Immediate) | <= one engine chunk (<= preparedBlockSize) |
| M2 | Quantised start accuracy (1/16 at 120 BPM) | error <= one chunk |
| M3 | Release fade | 10 ms, click-free (max step bound) |
| M4 | Stuck-note timeout | releases within `maxHold` +- 1 chunk; message shown |
| M5 | Protection | wet around protected transients is ducked exactly as for natural fills (same duck value) |
| M6 | Pool safety | natural fill pool (4 voices) unchanged; perform voice is separate; memory <= 1 extra voice (measured, recorded) |
| M7 | Panic | all perform effects released within 10 ms |

## 3. Design
`Source/features/perform/{Performer,PerformSetup,StormProfile}.{h,cpp}`, `PerformMidi.cpp`, `Gui.*`, `Help.cpp`, `Info.cpp`.
* **PerformVoice:** a sixth `FillVoice` owned by `Performer` (separate from the 4-voice pool and from E-1's preview voice; if memory is tight F-X07 Light may share it with the preview voice, with perform taking priority).
* **Inputs, all converging on a bitmask:** `holdMask` (bit per effect). UI writes `std::atomic<uint8_t> uiHold` from the message thread on mouse down/up (and keyboard key down/up) with a 100 ms heartbeat counter; MIDI notes/CCs update `midiHold` directly on the audio thread during the MIDI scan (same block). Effective mask = `uiHold | midiHold | latched`. Edge detection per chunk gives press/release.
* **Hold vs Latch:** `fm10_mode`: Hold or Latch (latch bits toggle on press edge).
* **Chained fills:** on press, `Performer` starts a fill of the mapped algorithm with link length `L = clamp (division of fm10_linkDiv, 80 ms, 1500 ms)`; when a link ends and the effect is still active it starts the next link. Per link: `VoiceSetup` built like `toVoiceSetup` with `amp = 0.8 * dbToGain (fm10_level)`, `sourceAnchor` = now, `rngSeed = hashCombine (hashCombine (seed, kSalt_Fm10), linkCounter)` (sub-RNG only; the legacy RNG is never touched; the counter resets on each press so repeated presses sound the same for a given seed and ppq). Mapping: Stutter -> `Algo::Stutter` (stutterSeconds from the user stutter division), Storm -> Stutter with `stutterSeconds = base * stormFactor (link)`, `stormFactor = 1, 1, 0.5, 0.5, 0.25, 0.25, 0.125 ...` bounded so `slice >= 8 ms` and scaled by `fm10_stormDepth`, Reverse -> `Algo::Reverse` with link length = division, Tape stop -> `Algo::TapeStop` (one link, no chain; holds silence after), Freeze -> `Algo::Freeze` loop slice of `linkDiv` (time-domain freeze; F-M19 adds scrub).
* **Quantised start:** `fm10_quant` Off or 1/16, 1/8, 1/4, 1/2, 1 bar via `QuantClock`; without transport -> Immediate. A press released before its quantised start cancels silently.
* **Protection:** the Performer's output enters the same wet bus before the protection duck (same path as natural voices; `FillEngine::process` gets `performerRender (wet, ...)` as one added call), so ducking and `PanicGain` apply unchanged.
* **Release:** `voice.requestStop (10 ms)`; Tape stop and Freeze fade out similarly; chain ends.
* **Hold limit:** `fm10_maxHold` (2/4/8 s): a sample counter forces release and posts event `HoldLimit`.
* **Eligibility:** perform effects ignore `Fill`, `Density` and the gap detector (they are deliberate), but obey `Wet`, protection, Panic, F-L25 guard, bypass (inactive while bypassed).
* **Level:** `fm10_level` -12..0 dB default -3.

## 4. Parameters and state
Group `fm10`, file `Source/params/fm10.h`, lock `tools/param_ids/fm10.lock`, appended to `GAPFILL_PARAMS_EXT`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fm10_mode` | choice: Hold, Latch | Hold | - |
| `fm10_quant` | choice: Off, 1/16, 1/8, 1/4, 1/2, 1 bar | Off | - |
| `fm10_linkDiv` | choice: Division list (1/32..1/2) | 1/8 | - |
| `fm10_stormDepth` | float 0-100 | 60 | % |
| `fm10_maxHold` | choice: 2 s, 4 s, 8 s | 4 s | - |
| `fm10_level` | float -12..0 | -3 | dB |
Latched/held state is never saved. All six IDs are in `performanceParameterIDs()`. Neutral proof: with no press the Performer is idle (`holdMask == 0`, one branch per chunk), goldens A-F identical.

## 5. Real-time safety
Preallocated voice; masks are atomics; no allocation or locks; the heartbeat is a relaxed atomic counter read per chunk. Reset table: `reset()` and `releaseResources` stop the voice and clear latches; sample-rate change re-prepares the voice; bypass: effects released; transport jump: chains continue (they are time based) unless quantised start pending, which is recomputed; Panic: `releaseAll()`.

## 6. UI (Performance tab, docs/dev/11)
Five large pads (STUTTER, STORM, REVERSE, TAPE STOP, FREEZE) with state text ("holding", "latched", "waiting for beat"), Mode/Quantise/Level/Storm depth controls. Mouse: down = on, up = off; keyboard: hold a key (assignable later; MVP: Q W E R T when the tab has focus, shown in tooltips). Pads are >= 48 px; labels never colour-only. Lost mouse capture (window deactivated) releases. Reduced Motion: pad highlight without animation. Help "Performance effects" order 1530.

## 7. MIDI / automation / CLAP / host
Actions mapped by MIDI Learn (note on = press, note off = release; CC >= 64 = on, < 64 = off); channel filter respected (U-3 style). Not host-automatable parameters (they are gestures). CLAP/VST3/AU: note-offs are part of the MIDI stream the plugin already receives. Lost note-offs handled by the hold limit and by CC 123/120 if `fs12_midiReset`.

## 8. Tests (`Tests/features/TestPerform.cpp`, group `Perform`)
`press_starts_within_one_chunk` (M1) | `quantised_start_accuracy` (M2) | `release_fades_10ms_click_free` (M3) | `latch_toggles` | `hold_limit_releases_and_posts_event` (M4) | `ui_heartbeat_loss_releases` | `midi_note_on_off_same_block` | `cc_threshold_on_off` | `storm_slices_shrink_and_floor_8ms` | `link_chain_seamless_no_click` | `rng_deterministic_per_press` | `legacy_rng_untouched` | `protection_duck_applies` (M5) | `bypass_inactive` | `panic_releases_all` (M7) | `pool_voices_unchanged` (M6) | `memory_extra_voice_recorded` | `no_audio_thread_allocation` | `neutral_golden_A_to_F`.

## 9. Dependencies and build order
F-S12 and QuantClock (F-L07a) first; E-1 for the auxiliary voice pattern; F0-2 hook for `performerRender`. F-S07 is the Storm button and needs nothing beyond this ticket.

## 10. Effort and risks
~6 ideal days. Risks: a sixth voice's memory at 192 kHz (measure; Light tier may share); users expecting perform effects to ignore protection (documented: they duck around hits; a later option could disable); key repeat on held keys (must ignore auto-repeat events).

## 11. Acceptance checklist
- [ ] M1-M7 recorded in `docs/progress/F-M10.md`
- [ ] Hardware MIDI pad test in a real host listed as verified or unverified
- [ ] UI checklist at minimum size; keyboard operation; Help text
