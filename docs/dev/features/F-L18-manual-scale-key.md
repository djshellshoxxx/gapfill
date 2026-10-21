# F-L18 Scale and key awareness (manual)

Status: specified, not approved. Size M. Category MUS/VAL, value H.
Overlap: `10` section 5.4 mentions an optional "scale constrain with explicit user root/scale" for a comb resonator (F-M15, not this ticket). This file defines the shared root/scale model and applies it to the pitch-bearing algorithms that exist today. Automatic key detection is F-X03 and is out of scope; this ticket exposes a hook for it.

## 1 Summary and user-visible behaviour
The user picks a Root (C..B) and a Scale (Major, Minor, modes, harmonic/melodic minor, pentatonics, blues, whole-tone, Chromatic). With the feature on, fills that change pitch stay musical: Comb resonators are tuned to scale notes; pitch slides (PitchRise/PitchFall) end on an interval that keeps the material in the scale; granular grains and Stutter pitch steps use only scale-compatible intervals. Nothing else changes. Off by default. The status text shows "Key: D minor, scale lock on".

## 2 Success / quality metric
- Off: `VoiceSetup` and all voice output bit-identical to baseline (golden hashes), RNG draw counts identical.
- On: every Comb fundamental is within 5 cents of a scale note (measured by autocorrelation on rendered output); every constrained interval is in the allowed set; RNG draw count identical on/off (determinism of later decisions preserved).
- Pitch slide end interval `i` satisfies `fit (i) >= threshold` (3.2) in 100% of 10 000 seeded fills.

## 3 Design
Files: `Source/features/scale/Scale.{h,cpp}` (pure, no JUCE UI), `ScaleSetup` (POD in `Source/dsp/VoiceSetup` additions), `Gui.{h,cpp}`, `Help.cpp`.

### 3.1 Model
```cpp
struct ScaleSetup {                // neutral: enabled = false
    bool enabled = false; std::uint8_t root = 0;     // pitch class 0..11
    std::uint16_t mask = 0xFFF;                      // bit p set = pitch class p allowed (absolute, root already applied)
    std::uint8_t strictness = 1;                     // 0 strict, 1 loose
    std::uint8_t scope = 0;                          // 0 all, 1 comb, 2 slides, 3 granular
    int sourcePitchClass = -1;                       // hook for F-X03; -1 unknown
};
ScaleSetup makeScaleSetup (int root, int scaleIndex, int strictness, int scope) noexcept;
int  constrainSemitones (const ScaleSetup&, int semis, int range) noexcept;   // returns nearest allowed interval, ties toward 0
float constrainCombFrequency (const ScaleSetup&, float hz) noexcept;          // snaps to the nearest scale note, 55..880 Hz
```
Scale masks (intervals from root): Major 0,2,4,5,7,9,11; Natural Minor 0,2,3,5,7,8,10; Dorian 0,2,3,5,7,9,10; Phrygian 0,1,3,5,7,8,10; Lydian 0,2,4,6,7,9,11; Mixolydian 0,2,4,5,7,9,10; Locrian 0,1,3,5,6,8,10; Harmonic Minor 0,2,3,5,7,8,11; Melodic Minor 0,2,3,5,7,9,11; Major Pentatonic 0,2,4,7,9; Minor Pentatonic 0,3,5,7,10; Blues 0,3,5,6,7,10; Whole Tone 0,2,4,6,8,10; Chromatic all 12 (equals off).

### 3.2 Why intervals need a fitness rule
Playback-rate transposition by `i` semitones moves every note of the material by `i`. Without knowing the source pitch (F-X03), the only guarantee available is structural: notes in scale S land in S again. Define `fit (i) = popcount (mask & rotl (mask, i mod 12))` and `n = popcount (mask)`. Allowed intervals: strict `fit == n` (only 0 and +-12 for most scales, plus +-5/+-7 are `n-1`, so strict allows octave only); loose `fit >= n - 1` (adds fourths and fifths for diatonic scales; pentatonic `fit >= n-2`). Intervals are chosen as the nearest allowed to the one the algorithm already drew, so a fill keeps its direction and rough size. If `sourcePitchClass >= 0` (F-X03 later) the rule becomes exact: allowed `i` iff `(sourcePitchClass + i) mod 12` is in the mask.

### 3.3 Application sites (all in `FillVoice::configureAlgorithm`/`genGranular`, guarded by `if (setupData.scale.enabled && inScope)`)
- **Comb** (`FillVoice.cpp` ~line 576): replace the fixed `110 * 2^(note/12)` pick. Draw the same `rng.nextInt` values as today, then map: `midi = 45 + root-relative degree`; build the note list from the scale (degrees within two octaves above A2 = 110 Hz), pick `list[drawnIndex % size]`; `freq = 440 * 2^((midi - 69) / 12)`; `combDelay` formula unchanged. Comb is the only site that is absolute-pitch exact.
- **PitchRise/PitchFall** (~line 459): `amount' = constrainSemitones (round (amount))` (range 5..26); the slide still glides continuously; only the end point changes.
- **Granular** (~line 805): `kRatios` entries are converted to semitones `round (12 log2 r)` (note 1.25 maps to +4 and 1.1892 to +3); the drawn index is mapped to the nearest allowed ratio. The fine detune term (+-3%) is kept.
- **Stutter** rate step (~line 391, 0..3 semitones): quantised to the nearest allowed interval, 0 allowed always.
- **Reverse** octave jumps are already octaves: untouched. Delay, Freeze, Texture, Spectral, Crush, Distort, Scramble, TapeStop are not pitch-bearing: untouched.
Scheduler: `FillEngine::trySchedule` copies `ScaleSetup` from `EngineSettings` into `VoiceSetup` (one assignment).

## 4 Parameters and state
Group `FL18`, `tools/param_ids/fl18.lock`.
| ID | Type / range | Default |
|---|---|---|
| `fl18_enable` | bool | off |
| `fl18_root` | choice C, C#, D, D#, E, F, F#, G, G#, A, A#, B | C |
| `fl18_scale` | choice (14 scales above) | Major |
| `fl18_strictness` | choice Strict, Loose | Loose |
| `fl18_scope` | choice All pitched, Comb only, Slides only, Granular only | All pitched |
No subtree. Neutral proof: `enabled == false` => `makeScaleSetup` returns the default struct and every site takes the existing code path (branch not taken, no extra RNG draws); golden hash unchanged. Root/scale are musical context and are not randomised or touched by Surprise (`performanceParameterIDs()`).

## 5 Real-time safety
`ScaleSetup` is 8 bytes copied per fill; tables are `constexpr`; `constrainSemitones` is a bounded loop over <= 25 candidates; `constrainCombFrequency` uses `std::log2`/`std::exp2` on a single value. No allocation or locks. RNG draw counts must not depend on the setting (test).

## 6 UI
Slot `CreativeExtra`, small card "Key": Root combo, Scale combo, Strict/Loose segmented toggle, Scope combo, shown after Enable. Tooltip Root "The tonic of your song. Fills that change pitch stay inside the scale built on this note." Help section order 1180 "Key and Scale" including: this is a manual setting, it does not listen to the music, and why Strict allows few intervals. Accessible names "Key root", "Key scale". Keyboard: combos by arrow keys. Reduced motion: n/a. Narrow: card wraps to two rows. Footer status shows the key while enabled.

## 7 MIDI / automation / CLAP / host
Root and scale are automatable (useful for key changes in a song) and MIDI-learnable. Changing them mid-fill affects only the next fill. A future option "follow host key" is not part of this ticket (no host standard). No latency, tail or bus change.

## 8 Tests (`Tests/features/TestScale.cpp`)
`masks_have_expected_note_counts` | `disabled_voicesetup_identical_golden` | `rng_draw_count_identical_on_off` | `fit_table_for_major_minor_pentatonic` | `constrain_semitones_nearest_ties_to_zero` | `constrain_range_bounds` | `comb_frequency_within_5_cents_of_scale_note` (render + autocorrelation, all 14 scales x 12 roots) | `slide_end_interval_always_allowed_10000_seeds` | `granular_ratios_mapped_to_semis` | `scope_limits_effect` | `chromatic_equals_off_output_for_slides` | `source_pitch_hook_exact_mode` | `state_roundtrip_old_session`.

## 9 Dependencies and order
F0-1, F0-3 (`toVoiceSetup`), F0-6. Independent of the wet stages. F-X03 later supplies `sourcePitchClass`. F-M15 (comb resonator module) should reuse `Scale.h`.

## 10 Effort and risks
M. Risks: users expect pitch correction (Help must state it is interval-constraint, not tuning of the source); `fit` rule is conservative and may feel restrictive in Strict; Comb change alters sound only when enabled, but any future refactor of the comb draw order would break determinism (guard test).

## 11 Acceptance checklist
- [ ] Off path bit-identical; RNG draw counts equal on/off
- [ ] Comb fundamentals within 5 cents for all scales and roots
- [ ] All constrained intervals pass the fitness rule
- [ ] Help text states the manual, interval-only behaviour
- [ ] UI reviewed at min size and keyboard-only
- [ ] IDs locked; legacy state loads unchanged
