# F-M22 Smart preset suggestions
Status: implementation spec, Gate C candidate (08). Category UX/VAL, value M, size M. Overlaps and builds beside: `05-SPEC-USABILITY-VALUE.md` U-4 (preset browser, categories), U-6 (Auto-Calibrate, which *changes detector settings*). This ticket never changes settings: it **measures** a few simple properties of the incoming audio with the existing detector and **recommends** existing factory presets. Opt-in, deterministic, offline, no machine learning, nothing recorded or sent.

## 1 Summary & user-visible behaviour
- **Suggest presets...** (preset browser header, or the File menu until U-4 lands). First use shows a plain consent line: "GapFill will listen to your input for about 10 seconds on this computer. Nothing is recorded or sent anywhere." Consent is remembered and can be revoked in Options. Every measurement starts only when the user clicks **Listen**.
- Progress bar for 4-30 s (default 10). Cancel, closing the editor or bypass-free silence handling: "Not enough signal, play something and try again".
- Result card: up to 3 presets with a one-line reason ("dense hits, 7.2 per second, short gaps: rhythmic family"), a confidence word (Good / Fair / Weak match), **Load** (existing validated path, pushes Undo) and the measured numbers. Nothing is applied automatically.
- Measurements use your current detector settings (stated on the card); after Auto-Calibrate (U-6) they are more meaningful.

## 2 Success/quality metric
- Determinism: same input buffer => identical `InputProfile` (bit-equal) and identical ranking, at block sizes {32, 64, 512, 4096, 8192} and rates 44.1-192 kHz (accumulation uses absolute hop positions, not block boundaries).
- Archetype accuracy on the synthetic corpus (8 signals, below): intended family ranked first for >= 7 of 8, and in the top 3 for 8 of 8.
- Cost while listening: <= +1 % mean block time; idle: one relaxed atomic load per chunk. No audio retained: memory use is a fixed 128-byte accumulator.
- Privacy: zero network or file writes during measurement (test with a recording filesystem/network stub; only the consent flag is persisted).

## 3 Design
Module `Source/features/suggest/` (`Profile.h/.cpp` audio-thread accumulator, `Suggest.h/.cpp` pure ranking, `Families.h/.cpp` table, `Gui.*`, `Help.cpp`). Does **not** edit `Presets.cpp` (preset edits belong to the U-10 pass); the family table references presets by name.
```cpp
struct InputProfile { float eventsPerSec, gapFraction, levelDb, lowShare, highShare; float seconds; bool valid; };
class ProfileAccumulator {                           // owned by the processor
public:
    void prepare (double sr) noexcept;
    void startListening (float seconds) noexcept;    // message thread: sets target hops + generation (atomics)
    void cancel() noexcept;
    void accumulate (const AnalysisEngine&, std::int64_t fromPos, std::int64_t toPos) noexcept;   // audio thread, after analysis.processBlock
    enum class State : std::uint8_t { Idle, Listening, Done, NoSignal } getState() const noexcept;
    float getProgress() const noexcept;              // 0..1, relaxed atomic
    bool  takeProfile (InputProfile&) noexcept;      // message thread, seqlock read, valid once per run
};
struct Suggestion { int presetIndex; float distance; enum class Confidence : std::uint8_t { Good, Fair, Weak } confidence; int reasonId; };
int rankPresets (const InputProfile&, Suggestion out[3]) noexcept;          // pure; returns count (0..3)
juce::String describeReason (const InputProfile&, int reasonId);             // string table, message thread
```
**Accumulation** (hop = `AnalysisEngine::hopSize` = 32 samples, positions absolute): for each hop `h` in `[fromPos, toPos)`: `f = analysis.frameAt (h*hop)`; `hops++`; `gapHops += f.conf >= 0.55` (`kEnterGapConf`); `sumRef += f.reference`; `sumLow/Mid/High += f.bandEnergy[0..2]`; `signalHops += f.reference > 10^(-60/20)`. Events: `p = analysis.nextDetectionEvent (from)`; while `0 <= p < toPos`: `events++; from = p + 1` (bounded by the 256-entry event queue; must run **before** `discardEventsBefore`, so the hook sits directly after `analysis.processBlock`). When `hops * hop >= seconds * sr`: finalise.
**Finalise:** `seconds = hops*hop/sr`; `eventsPerSec = events/seconds`; `gapFraction = gapHops/hops`; `levelDb = 20 log10 (max (1e-6, sumRef/hops))`; `lowShare = sumLow/(sumLow+sumMid+sumHigh)`, `highShare` likewise; `valid = signalHops/hops >= 0.25` else state `NoSignal`. Published through a single-writer seqlock (`SeqSnapshot<InputProfile>`, shared utility).
**Ranking.** Normalise `n = { log2 (1 + eps)/log2 (17), gap, clamp ((db + 60)/60), low, high }`, each clamped to [0,1]; distance to each table row `d = sqrt (sum w_i (n_i - c_i)^2)`, `w = {2, 2, 1, 1, 1}`; sort ascending, ties by preset index; skip `Init`. Confidence: `Good` if `d1 < 0.20`, `Weak` if `d1 > 0.45`, else `Fair`; reason id = the largest-contributing feature (dense/sparse hits, long/short gaps, loud/quiet, bass-/treble-heavy).
**Family table** (`Families.cpp`; columns: events/s, gap fraction, level dB, low share, high share; starting values, tuned in this ticket's PR against the corpus and then frozen by test): Subtle Ear Candy 3.0/0.35/-18/.35/.25 | Drum Gap Glitch 5.0/.30/-14/.40/.25 | DnB Micro Fills 9.0/.20/-12/.45/.25 | Breakcore Destroyer 12/.15/-10/.35/.35 | Ghost Reverses 2.0/.45/-20/.30/.25 | Industrial Debris 6.0/.25/-12/.35/.30 | Vocal Ghosts 1.5/.45/-20/.15/.35 | Vocal Delay Fills 2.5/.40/-18/.15/.30 | Bass Between Kicks 3.5/.35/-14/.60/.10 | Granular Spaces 1.0/.55/-24/.25/.30 | Trance Transitions 8.0/.18/-10/.40/.30 | Trip Hop Dust 2.5/.45/-18/.45/.20 | Spectral High Fill 4.0/.30/-16/.20/.50 | Spectral Mid Mutation 4.0/.30/-16/.20/.20 | Sparse Texture 0.8/.60/-26/.30/.25 | Extreme Chaos 10/.20/-8/.35/.35. Rows resolve to indices by name at runtime (`getFactoryPresets()`); `family` label (Subtle, Rhythmic, Glitch, Ambient, Vocal, Bass, Destructive) matches U-4 categories when it lands.
Hooks (hot files, minimal): `GapFillAudioProcessor` member `ProfileAccumulator profile;` and 2 lines in `processChunk` (`if (profile.getState() == Listening) profile.accumulate (analysis, posBefore, posAfter);` with `posBefore/After = analysis.getAnalysisPosition()` around `processBlock`); bypass or `prepared == false` cancels. Consent flag in the U-4 global settings file (`suggest.consent`, bool) via the `settings` module (F-S03/U-4); until that exists, the `<Perf>` subtree.

## 4 Parameters & state
**No APVTS parameters (intentional):** this is a user-initiated analysis, not an automatable value, and nothing about it may change a session. State: `suggest.consent` in the global settings file (or `<Perf suggestConsent="1"/>` until U-4's file exists; versioned with `<Perf>`). Profiles and results are never saved. Old sessions load unchanged because nothing in the audio path changes when `State == Idle`. **Neutral proof:** idle fast path only; `idle_is_bit_identical_golden_A_to_I`.

## 5 Real-time safety
Audio thread: per chunk `n/32` frame reads plus the event walk (<= 256 iterations), only doubles/uint64 accumulators; no allocation, locks, logging. The seqlock publishes once per run. `rankPresets` and string building run on the message thread. Cancel from any thread via atomic. Reset table: `reset`/`releaseResources`/sample-rate change/bypass/transport jump => state `Idle` (run abandoned, progress 0). Transport stop does not cancel (input may still arrive); a silent stretch simply lowers `signalHops`.

## 6 UI
Slot: preset browser header (U-4) "SUGGEST..." button; interim: File menu item (11: occasional actions live in menus, no new header row). A modal-free popover: consent (first run), Listen with duration combo (4/10/20/30 s), progress bar with text "Listening 6 of 10 s", result rows. Tooltips: SUGGEST "Listen to your input for a few seconds and recommend presets that suit it. Nothing is recorded or sent."; LOAD "Load this preset (you can undo)." Help "Smart preset suggestions" (order 1220): what is measured (hits per second, gap share, level, bass/treble share), how matching works, privacy statement, relation to Auto-Calibrate. Accessible: progress as `AccessibilityRole::progressBar` with percent; result rows titled "Drum Gap Glitch, good match, dense hits"; keyboard Tab/Enter/Esc. Reduced motion: progress updates in whole-second steps, no easing. Narrow: rows stack, numbers collapse under "Details".

## 7 MIDI/automation/CLAP/host impacts
None: no parameters, no MIDI, no automation, no CLAP/VST3 surface. Requires an audio-processing host or the standalone with live input (an offline-render-only workflow can still run it by pressing Listen during playback). Loading a preset uses `selectPreset()` so host program-change/Undo behave as today. Works with sidechain-only detection (measures the detection lane's frames; the card notes it).

## 8 Tests (`Tests/features/TestSuggest.cpp`)
`idle_bit_identical_golden_A_to_I` | `profile_deterministic_same_input` | `profile_block_size_invariant` | `profile_rate_invariant_within_tolerance` (44.1/48/96/192, events/s +/-3 %) | `events_counted_before_discard` | `signal_gate_no_signal` | `cancel_midrun_and_editor_close` | `bypass_cancels` | `seqlock_stress` (TSan) | `rank_deterministic_and_tiebreak_by_index` | `rank_skips_init` | `table_resolves_all_names` | `every_factory_preset_has_row` (fails when presets change) | `archetype_ranking` (4-on-floor 120 BPM, sparse snare, dense hat loop, sustained pad, noise, bass+kick, vocal-like sustained mid phrases, breakbeat; expected family top-1 in >= 7/8, top-3 in 8/8) | `confidence_thresholds` | `reason_strings_all_ids` | `consent_roundtrip_and_default_off` | `no_file_or_network_io_during_measurement` | `load_goes_through_select_preset_with_undo` | `no_audio_thread_allocation`.

## 9 Dependencies & build order
Needs F0-4/B4 patterns (atomics, seqlock utility), F0-9 (slot for the button) and U-4 (browser, settings file) for the final placement; works with the File menu and `<Perf>` consent meanwhile. Soft: U-6 (shares the Listen UX; if U-6 lands first, reuse its progress widget), U-10 preset pass (may later add categories/tags, after which the table can read tags). Independent of every other F-M ticket.

## 10 Effort & risks
M. Risks: five coarse features may recommend poorly on unusual material (confidence word and "Weak" state are honest, ranking is explainable); depends on detector settings (stated on the card); table must be kept in sync with factory presets (enforced by `every_factory_preset_has_row`); privacy perception (consent text, no audio retained, tested). Out of scope: tempo/key detection, user presets, any automatic loading.

## 11 Acceptance checklist
- [ ] Idle golden bit-identical; no parameters or state added to sessions
- [ ] Deterministic profile and ranking across block sizes and rates
- [ ] Archetype accuracy met; table covers all factory presets
- [ ] Consent flow, cancel, silence and bypass handling verified; no I/O during measurement
- [ ] Load uses the validated path and is undoable
- [ ] Keyboard, screen-reader names, reduced-motion and minimum-size review; Help merged
