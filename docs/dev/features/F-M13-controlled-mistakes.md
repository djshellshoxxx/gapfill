# F-M13 Controlled Mistakes
Status: implementation spec, Gate C candidate (08). Category FUN/MUS, value M, size M on top of C-3. **Canonical spec: `04-SPEC-CREATIVE.md` C-3** (bounds, the five types, `MistakeStage`, hook lines, 11 tests). This file does not repeat it. It lists the gaps that stop C-3 being implementation-complete and adds the "flams, slips and seed-preserving variations" the backlog asks for.

## 1 Summary & user-visible behaviour
- **Mistakes** knob (0-100 %): per fill, chance `p = 0.20 * amount` that exactly one small human-style imperfection is applied (C-3, unchanged). At 0 nothing happens and no random number is drawn.
- Two added types: **Flam** (a quiet doubled echo of the fill's opening hit, 10-32 ms behind it, like a grace note) and **Slip** (one short stretch of the fill drags 4-24 ms late, then snaps back, like a hit played behind the beat). Both are wet-only, never extend the fill and never cross the gap or protection boundary.
- **Types...** popover: tick which of the 7 types may occur. Default = the five C-3 types, so a session saved under C-3 sounds identical.
- **Variation** (0-99): re-rolls only the mistake decision for the same seed and position. Same fills, same algorithms, same lengths; only which mistake (or none) changes. 0 = exactly C-3.
- Fill History chips (U-1) and tooltips name the mistake ("Stutter 1/16 - 180 ms - Brake").

## 2 Success/quality metric
- `amount=100`: share of fills with a mistake over 10 000 seeded gaps is 0.20 +/- 0.02; `amount=0`: bit-identical to golden A-I and the RNG probe is untouched.
- Zero boundary violations: 0 samples of mistake-altered output beyond the voice's planned end or into the protected region in 100 000 randomised fills (all 15 algorithms, 44.1-192 kHz).
- Click metric: max |x[n]-x[n-1]| of the mistake render is <= 1.5 x the same fill's no-mistake render + 0.02 (full-scale), every type.
- CPU: stage on, 5 voices, scenario E: <= +2 % mean block time vs stage off (`Tests/Perf.cpp`).

## 3 Design
Files (all new, `Source/features/mistakes/`, the C-3 location): `Mistakes.h/.cpp` (C-3 `MistakeStage`), `MistakeTypes.h` (enum + applicability table), `Gui.h/.cpp`, `Help.cpp`, `cmake/sources/mistakes.cmake`, `Tests/features/TestMistakes.cpp`. Hooks (C-3, plus 1 line): `FillVoice` member + `start()/process()/rateMultiplier()`; `adjustRecipe` sets `FillRecipe::mistakeType/mistakeAmt`; `gatherMistakes (EngineSettings&, const ParamCache&)` in `FeatureHooks.h`.

```cpp
enum class MistakeType : std::uint8_t { None, Wobble, OctaveDisplace, MissedRepeat, Brake, StereoBounce, Flam, Slip, Count };
constexpr std::uint8_t kDefaultTypeMask = 0b0011111;         // bits 0..6 = types 1..7
bool isApplicable (MistakeType, Algo, int fillLengthMs, bool monoOut) noexcept;   // constexpr table, tested vs every Algo
struct MistakeRoll { MistakeType type; float p0, p1, p2; };     // type-specific params, drawn once
MistakeRoll rollMistake (std::uint64_t key, float amount01, int variation, std::uint8_t typeMask,
                         Algo, int fillLengthMs, bool monoOut) noexcept;
class MistakeStage {
public:
    void prepare (double sr, int channels);            // ring = ceil(0.040*sr)+8 per channel (shared with Wobble)
    void start (const MistakeRoll&, int totalLength, int fadeOut) noexcept;
    void process (float& l, float& r, int elapsed) noexcept;       // per sample, early-out when type==None
    float rateMultiplier (int elapsed) const noexcept;             // Brake/Octave only, else 1.0f
};
```
**Roll (frozen draw order).** `Rng sub; sub.seed (hashCombine (key, variation == 0 ? kSalt_Mistake : hashCombine (kSalt_Mistake, (uint64_t) variation)))`. Draws: (1) `chance (0.20 * amount01)`; (2) candidate set = types in `typeMask` that are applicable and not skipped, ordered by id; `nextInt (n)` picks one (uniform over the set; equals C-3's re-roll distribution); (3) that type's parameters, in the order below. `n == 0` => `None`. `amount01 == 0` returns before draw (1).

| Type | Params drawn | Rendering (ramps >= 2 ms everywhere) | Applicable / skipped when |
|---|---|---|---|
| 6 Flam | `d = range (10, 32) ms`, `g = range (0.45, 0.70)` | window `W = min (90 ms, 0.35 * len)`; `y = (x + g * w(t) * x[n-d]) / (1 + 0.5 g)`, `w = 1` until `W - 10 ms`, then raised-cosine to 0. `x[n-d]` read from the ring, 0 before `d` | Stutter, Delay, Scramble, Granular, Reverse, Crush, Distort, Comb, TapeStop, PitchRise/Fall; skip Freeze, Texture, Spectral, Filter; skip fill < 120 ms |
| 7 Slip | `d = range (4, 24) ms`, `ts = range (0.25, 0.65) * len`, `hold = min (range (60, 200) ms, 0.30 * len)` | for `ts <= t < ts+hold` output = `x[n-d]`, 3 ms cosine crossfade in and out; outside the window output is the untouched `x[n]` (alignment restored). Late only, never early (cannot read the future) | all except Freeze, Texture; skip fill < 150 ms; require `ts+hold+3 ms <= 0.90 * len` |

Types 1-5: exactly C-3's table; Wobble reuses the same 40 ms ring (C-3 only needed a few ms). Data flow: `trySchedule -> adjustRecipe (rollMistake -> recipe) -> toVoiceSetup (carries MistakeRoll) -> FillVoice::start -> MistakeStage`. With Lock on (C-5) the locked recipe's mistake is ignored and a fresh roll happens per fill (C-3).

## 4 Parameters & state
Format note: IDs below contain `_`; `tools/validate_repo.py` currently allows `^[a-z][A-Za-z0-9]*$` (02 F0-1). Extend it to `^[a-z][A-Za-z0-9_]*$` in the first ticket that lands with these IDs (see report: owner decision).

| ID | Type | Range | Default | Unit | Notes |
|---|---|---|---|---|---|
| `mistakes` | float | 0-100 | 0 | % | **Already registered by 02** (C-3). Not new; do not duplicate or rename. |
| `fm13_variation` | int, step 1 | 0-99 | 0 | - | Re-roll index. Appended after `mistakes`. |
| `fm13_types` | int, step 1 | 1-127 | 31 | bitmask | bit0 Wobble ... bit6 Slip; text shown as type names. |

Group `GAPFILL_PARAMS_MISTAKES(X)` in `Source/params/mistakes.h` (C-3 group; two lines appended), lock file `tools/param_ids/mistakes.lock` gains the two IDs. Both new IDs go in `performanceParameterIDs()` (excluded from Random/Surprise/Undo snapshots, 02) and are not changed by presets. No state subtree; `FillRecipe` already carries `mistakeType` (0-7 fits the byte) and `mistakeAmt`. **Neutral proof:** `mistakes = 0` => stage not entered, zero draws; `variation`/`types` are read only after `chance` passes. An old session lacks the two IDs and loads them at 0 / 31, which reproduces C-3 bit for bit. Test `old_state_loads_c3_identical`.

## 5 Real-time safety
Audio thread: one roll per started fill (4 RNG draws), then O(1) per sample. Ring (`40 ms` at 192 kHz = 7 688 samples x 2 ch x 5 voices ~ 300 KB) allocated in `prepare()` only, cleared in `start()`. No `rateMultiplier` outside Brake/Octave. Reset table (06 s1.6): `reset()`, `releaseResources`, sample-rate change (re-prepare), bypass (`panic` fade), transport jump: stage holds only per-fill state, so all five events end the fill and `type = None`.

## 6 UI
- Slot `CreativeExtra` (F0-9), Creative tab, inside the Advanced disclosure (U-5): `GapKnob` "MISTAKES" plus a 24 px "TYPES" button. Not in the header (11).
- Popover: 7 checkboxes (each with a one-line description), "Variation" stepper with a re-roll button (increments, wraps at 99). Minimum one box stays ticked.
- Tooltips: Mistakes: "Chance of one small human slip per fill: wobble, octave jump, missed repeat, brake, stereo bounce, flam or slip. 0 = never." Variation: "Re-roll which mistake happens without changing the fills themselves."
- Help section "Controlled Mistakes" (order 1130): what each type sounds like, the 20 % cap, why a fill is never longer because of a mistake.
- Accessible: knob title "Mistakes amount", button "Mistake types", checkboxes titled by type, stepper announces value. Tab order: knob, button, popover items; Esc closes. Reduced motion: popover opens without fade; no flash when a mistake occurs, the History chip text carries it.
- Narrow: at minimum size the TYPES button collapses to an icon with the same accessible name; Advanced stays collapsed by default.

## 7 MIDI/automation/CLAP/host impacts
All three params are automatable and MIDI-learnable through `allParameterIDs()`; `fm13_*` are stepped int params (VST3 step count 99 / 126, CLAP `STEPPED` flag via clap-juce-extensions). Changing them mid-fill affects only the next fill. Offline render with the same seed and PPQ is reproducible. No latency or PDC change. Bypass: voices fade, stage idle.

## 8 Tests (`Tests/features/TestMistakes.cpp`)
C-3 list (11 tests) plus: `flam_peak_bounded` (<= 1.0 + 0.5g of source peak, normalised) | `flam_only_inside_window` | `flam_skipped_short_or_sustained` | `slip_restores_alignment_outside_window` (render minus baseline is 0 outside `[ts-3ms, ts+hold+3ms]`) | `slip_never_early` | `slip_window_inside_fill` | `variation_zero_equals_c3` | `variation_changes_only_mistake` (fill start positions, algos, lengths identical for variation 0 vs 7) | `typemask_default_is_c3_set` | `typemask_single_type_forced` | `candidate_set_uniform` (chi-square on 10k rolls) | `ring_size_192k` | `no_audio_thread_allocation` | `mono_stays_mono_all_types` | `state_roundtrip_and_old_state`. Golden: scenario A-I unchanged at `mistakes=0`; new golden `M13` (scenario A, `mistakes=100`, each type forced via mask) tagged `[golden]` in its own PR.

## 9 Dependencies & build order
Needs F0-2 (`adjustRecipe`), F0-3 (`FillRecipe`), F0-5 (settings gather), F0-6, F0-9, and C-3 (this ticket completes C-3; same PR train, C-3 first). Soft: C-5 (Lock), U-1 (chips). Independent of every other F-M ticket.

## 10 Effort & risks
M (C-3 itself is L; this adds two types, one param pair and the UI). Risks: Flam/Slip add audible cost to every fill if mask widened (default off); ring memory x5 voices; Slip re-reads earlier audio, so on very sparse material it can repeat a transient (mitigation: skipped when `reference` energy of the window is below the fill's start level, test `slip_skipped_on_silence`).

## 11 Acceptance checklist
- [ ] `mistakes=0` golden A-I bit-identical; RNG probe unchanged
- [ ] 7 types finite, bounded, click-free on all 15 algorithms
- [ ] Variation preserves the fill schedule; variation 0 == C-3
- [ ] Old (pre-F-M13 and C-3-era) sessions load unchanged
- [ ] No audio-thread allocation; ring sized for 192 kHz
- [ ] UI reviewed at minimum, default, large; keyboard-only operation verified; Help and tooltips merged
- [ ] Validator regex change landed, lock file updated, hot files untouched except hook lines
