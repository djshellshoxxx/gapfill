# F-L03 More Like That
Status: implementation spec, Gate C candidate (backlog `08` F-L03, existing ticket C-11). Category FUN/VAL, value H, size L. Canonical behaviour: `04-SPEC-CREATIVE.md` C-11 (weight bias for 4 started fills; algorithm and band only). This file adds the "retain timing and core character" part the backlog asks for and the exact numbers C-11 leaves open. Defaults reproduce C-11 exactly.

## 1 Summary & user-visible behaviour
- **MORE LIKE THIS** (History chip menu, F-L02; also a Creative-tab button and a MIDI-learnable command) biases the next N started natural fills toward the selected (or latest) recipe: same algorithm much more likely, same family somewhat more likely, same band in Spectral mode. It biases; it never forces and never creates eligibility.
- New in this ticket, both opt-in: **Keep timing** retains the recipe's stutter/delay division and pulls fill length toward the recipe's length. Result: variations that feel rhythmically related.
- Indicator `LIKED x3` counts down per started natural fill. Pressing again restarts the count with the new recipe. Ignored while Fill Lock (F-L01) is on; indicator reads "Lock active" (greyed).

## 2 Success/quality metric
- Over 1000 seeded gaps (drum loop, Auto type, all weights 50 %), share of the liked algorithm during the Like window rises from p to between 2p and 3.2p and stays < 100 %; the share of the liked family is >= 1.2x baseline.
- With Keep timing on, >= 90 % of liked fills use the recipe's division (when the algorithm uses a division) and lengths fall within +-35 % of the recipe length when the gap allows.
- Zero change to later decisions: RNG draw count per gap identical with and without Like.

## 3 Design
Files (`cmake/sources/like.cmake`, `Tests/features/TestLike.cpp`):
- `Source/features/common/AlgoFamily.{h,cpp}` (created here, shared with F-L10): `enum class Family : std::uint8_t { Rhythmic, Textural, PitchTape, Destructive, Count }`; `constexpr Family familyOf (Algo)` from the C-11 table (Rhythmic: Stutter/Scramble/Delay; Textural: Granular/Freeze/Texture/Spectral/Comb; PitchTape: PitchRise/PitchFall/TapeStop/Filter; Destructive: Crush/Distort/Reverse); a `static_assert` loops over all `numAlgos` so a new algorithm cannot ship without a family.
- `Source/features/like/LikeBias.{h,cpp}`: 
  ```cpp
  struct LikeSettings { float strength01; int count; bool keepTiming; };      // from params, block stage
  class LikeState {
  public:
      void onCommand (const Command&, const LikeSettings&) noexcept;         // LikeLatest / LikeRecipe: stores recipe, remaining=count
      bool active() const noexcept;                                          // remaining > 0 and !lockOn
      void biasWeights (float (&w)[numAlgos]) const noexcept;                // multiplies in place, zero stays zero
      float bandWeight (int band) const noexcept;                            // 2.0 for same band else 1.0 (Spectral only)
      void adjustRecipe (FillRecipe&, VoiceSetupHints&, const GapContext&) const noexcept;   // keepTiming
      void onNaturalFillStarted() noexcept;                                  // --remaining
      void onTransportJump() noexcept;                                       // remaining = 0
  };
  ```
- Bias numbers (`s = strength01`): same algorithm `x (1 + 3 s)` (s=0.5 -> 2.5, C-11); same family other algorithms `x (1 + 0.8 s)` (s=0.5 -> 1.4); same band `x (1 + 2 s)` capped at 3. Zero weights stay zero; if every biased weight is zero fall back to the unbiased weights for that gap.
- Keep timing (only when `keepTiming` and the recipe's algorithm is the one chosen): (a) if `recipe.division != 0xFF` and the algorithm is Stutter or Delay, write `VoiceSetup::divisionOverride` (new neutral field, `0xFF` = derive as today, same pattern as `modifierOverride` in F0-3) and convert via the same `clampToDivision` of E-2; (b) `lengthSamples = lerp (lengthSamples, min (recipe.lengthMs in samples, usable), 0.5)` clamped to `[min (minGapSamples, usable), usable]`. Never lengthens beyond `usable`; never crosses the protected boundary.
- Hook order: `chooseAlgorithm` receives the biased weights (same draws, different thresholds); `adjustRecipe` applies (b) after Lock (Lock wins, skip). `onNaturalFillStarted` is called only for started natural fills (not rejected gaps, not previews).
- Mapping into `EngineSettings`: `PerformanceSettings::likeActive` already exists (F0-5); extend with `float likeStrength; bool likeKeepTiming;` (neutral 0/false).
- Data flow: `LikeRecipe` carries the chip's recipe; `LikeLatest` uses the audio thread's `lastNatural` slot (same slot as F-L01).
Minimal hooks in existing files: `chooseAlgorithm`/`chooseBand` call `biasWeights`/`bandWeight` (one line each), `adjustRecipe` line, `VoiceSetup` gets `divisionOverride`, `FillVoice::start` honours it only when set.

## 4 Parameters & state
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fl03_strength` | float 0-100, step 1 | 50 | % |
| `fl03_count` | int 1-16 | 4 | fills |
| `fl03_keep_timing` | bool | off | - |
Defaults reproduce C-11 (x2.5, x1.4, 4 fills). The like recipe and counter are runtime-only and **not saved** (momentary state, F0-6 "never serialise"); a loaded session starts with Like inactive. State subtree: none. Old sessions: IDs absent -> defaults. Neutral proof: `likeActive` is false until the command; `LikeState::active()` is a single compare; `fl03_*` are read only on the command, so changing them alone changes nothing. Add all three to `performanceParameterIDs()` (excluded from Random/Surprise/Undo). IDs appended in `params/like.h`, locked in `tools/param_ids/like.lock`.

## 5 Real-time safety
Audio thread: <= 15 float multiplies per gap, one compare per fill, command drain. No allocation or RNG draws added. The `LikeSettings` snapshot is copied when the command is processed, so parameter edits mid-window do not change an active Like. Resets: `reset()`, bypass, transport jump (`noteTransportJump`), sample-rate change all set `remaining = 0`; bypass leaves the recipe but not the counter.

## 6 UI
History chip menu item "More like this" and a Creative-tab "LIKE" button (slot `CreativeExtra`, same row as Lock) enabled when a recipe exists. Indicator text `LIKED x3`, plus a thin 4-segment pip row (shape, not colour-only). Controls in an "advanced" disclosure: Strength knob, Count stepper, Keep timing toggle. Tooltips: "Make the next few fills resemble this one. It nudges choices, it never forces a fill." / "Keep timing: reuse the fill's division and length." Accessible names "More like this", "Like strength", "Like count", "Keep timing". Keyboard operable; Reduced Motion removes the pip countdown animation (numbers only). Narrow: pips hidden, text kept. Help section order 1120.

## 7 MIDI/automation/CLAP/host impacts
The three parameters are automatable but meaningful mainly as settings; the action is a command (not host-automatable) and MIDI-learnable via F-S12 `CommandBindings` when present, otherwise through a dedicated `MidiLearnManager` button-learn entry (same pattern as AUDITION). No latency change. CLAP: parameters appear normally; `fl03_count` is stepped (clap-juce-extensions loses stepped hints by default; test that automation snaps to integers).

## 8 Tests
Group `Like`: `like_off_is_bit_identical` | `like_biases_but_not_forces` | `like_family_table_complete` (static_assert + runtime) | `like_expires_after_count_started` | `rejected_gaps_dont_count` | `preview_doesnt_count` | `transport_jump_resets` | `zero_weight_stays_zero` | `all_zero_falls_back` | `like_ignored_when_locked` | `draw_count_unchanged` (rng state equality after gap) | `strength_zero_is_identity` | `defaults_match_c11_numbers` | `keep_timing_uses_recipe_division` | `keep_timing_length_within_gap` | `keep_timing_off_unchanged` | `like_state_not_serialised` | `like_params_roundtrip` | `like_no_audio_thread_allocation` | `like_block_size_invariant`. Fixtures: drum loop 8 bars, seed 7, weights flat.

## 9 Dependencies on other tickets
Hard: B3 (`FillRecipe`, stages), B4 (queues), F-L01 (codec, `lastNatural`, precedence rule), E-2 (`clampToDivision`) for Keep timing. Soft: F-L02 (chip entry point). Required by F-L10 (shares `AlgoFamily`). Build order: F-L01 -> F-L02 -> F-L03.

## 10 Effort & risks
Effort M-L (L as in C-11 because of the stage hook; the extra Keep timing adds a `VoiceSetup` field). Risks: Keep timing at extreme tempos (division halved by E-2 clamp, so recipe division may not be reproduced; document); family table taste (tunable only in its own PR); users expecting "exact repeat" (that is Lock).

## 11 Acceptance checklist
- [ ] IDs appended, locked, excluded from Random/Surprise/Undo
- [ ] Draw-count and golden tests prove neutrality; defaults equal C-11 behaviour
- [ ] Statistical bias tests meet the stated bands with fixed seeds
- [ ] Keep timing never lengthens beyond the gap or crosses protection
- [ ] Indicator, tooltips, accessible names, narrow layout done
- [ ] Like state not serialised; old states load unchanged
- [ ] AllocGuard green; CLAP stepped-parameter check done
