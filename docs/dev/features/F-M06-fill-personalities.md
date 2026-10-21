# F-M06 Fill Personalities (completion spec over C-1)

Status: canonical numbers and tests are `04-SPEC-CREATIVE.md` C-1 (overlay table, Unhinged rule, 10 tests) and the effective-settings pipeline in `04` "Two-stage effective settings" + `02` F0-5. This file closes the gaps found against the code and replaces the parameter ID.

## 1 Summary & user-visible behaviour
One combo (Off, Subtle, Percussive, Glitchy, Psychedelic, Dub, Aggressive, Ambient, Unhinged) applies a coherent character as a block-stage overlay on the algorithm weights and four scalars. It never writes the user's parameters, never touches protection, detector, mix, latency or Fill Type. Choosing Off restores exactly the user's own settings; knobs stay where the user left them. Gaps in C-1 closed:
1. Units. `EngineSettings::weights[]` are 0..1 (`gatherEngineSettings`: `pv(...) * 0.01f`) and `chooseAlgorithm` clamps to 0..1, but C-1 says "clamp to 0..100". With multipliers up to 2.0 and default weights 20-70%, clamping at 1.0 flattens boosts for anyone at >= 50%. Fix: **ratio-preserving normalisation**: `w'[i] = userW[i] * wMul[i]`; `m = max_i w'[i]`; if `m > 1` divide all by `m`. Zeros stay zero. Only relative weights matter to `chooseAlgorithm` (it picks proportionally), so ordering and ratios are what the user hears.
2. Table shape. C-1 says `kOverlay[8][19]` but only 7 table personalities + Unhinged (computed). Define `constexpr float kOverlay[7][19]` for Subtle..Ambient (15 weight multipliers then density, complexity, chaos, intensity) and a separate `applyUnhinged()`. Index `personality-1`; 8 = Unhinged.
3. Scalars: `density01`, `complexity01`, `chaos01`, `intensity01` multiplied then clamped 0..1. `fill01` untouched (it is the user's master amount).
4. Spectral-mode bands: personality does not alter `bandProb/Amt/Int`.
5. "Effective values" feedback is F-S05/C-0. Until it exists, the combo tooltip lists the dominant boosts ("Dub: more Delay and Filter, fewer Stutter and Crush") and the Help page shows the table in words.
6. `Unhinged` also sets effective `mistakes01 = max(mistakes01, 0.35)` only when F-M13 is present: leave a `// F-M13 hook` no-op now (`applyUnhinged` takes `float* mistakes = nullptr`).
7. Reproducibility: personality is a parameter, so it is in the decision inputs; same seed/PPQ/settings give the same fills. No RNG use in the overlay.

## 2 Success / quality metric
- Distinctness (C-1): pairwise L2 distance of normalised effective weight vectors > 0.15 for all 36 personality pairs at default weights.
- Intent (C-1 histogram tests, 500 seeded gaps per mode): Dub's largest share is Delay; Percussive Stutter+Scramble > 35%; Ambient Texture+Freeze+Spectral > 40%; Aggressive Distort+Crush+Stutter > 35%; Glitchy Scramble+Stutter+Granular+Crush > 45%.
- Neutrality: Off identical (`memcmp` of `EngineSettings`), golden unchanged.
- Preference (manual, 5 listeners, blind, same loop): >= 4 of 5 correctly match each of Dub, Aggressive, Ambient to a one-line description.

## 3 Design
- Files: `Source/features/personality/{Personality.h,Personality.cpp,Gui.h,Gui.cpp,Help.cpp}`, `Tests/features/TestPersonality.cpp`, `Source/params/fm06.{h,cpp}`, `tools/param_ids/fm06.lock`, cmake fragments.
  ```cpp
  enum class Personality : std::uint8_t { Off, Subtle, Percussive, Glitchy, Psychedelic, Dub, Aggressive, Ambient, Unhinged, Count };
  void applyPersonality (EngineSettings&, Personality, float* mistakes01 = nullptr) noexcept;   // identity for Off
  const char* personalityBlurb (Personality) noexcept;    // tooltip/help text
  ```
- Algorithm (in order): if `Off` return; `scalars`; if `fillTypeIndex > 0` skip weights (forced type wins); else apply multipliers, normalise per gap 1; if every effective weight is 0 restore the user's weights for that block (C-1 guard). Unhinged: for `userW[i] > 0`, `w'[i] = lerp (userW[i], meanNonZero, 0.7)`, scalars x1.2/x1.3/x1.5/x1.1 (density/complexity/chaos/intensity).
- Hook: one call inside `Effective::applyBlock` (F0-5), reading `perf.personality` set by `gather()`. `PerformanceSettings` gains `int personality = 0` (neutral). Preview (F-M01) sees it automatically (block stage).
- Does not draw from `Rng`; adds no salt.

## 4 Parameters & state
Replaces unreleased `personality` of the 02 registry (group PERSONALITY slot kept, same position):

| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fm06_personality` | choice: Off, Subtle, Percussive, Glitchy, Psychedelic, Dub, Aggressive, Ambient, Unhinged | Off | n/a |

- Group file `Source/params/fm06.{h,cpp}`, lock `tools/param_ids/fm06.lock`, append-only choices; `ParameterID` version hint 1. Not in the Random/Surprise allowlists. Included in Undo/A-B/presets automatically (normal parameter; changing it does not push an Undo snapshot, C-1).
- State: none beyond the parameter. Old sessions/`.gapfill`: key absent -> Off -> identical sound. Factory presets unchanged until the preset pass; the pass may set it (that PR is `[golden]`).
- Neutral proof: `personality_off_is_identity` (memcmp) + golden A-F.

## 5 Real-time safety
Block stage only: <= 19 multiplies, one max, one divide per block; no allocation/RNG/locks. Runs where `gatherEngineSettings()` already runs. `reset()`/bypass/sample-rate/transport jump: stateless, nothing to reset.

## 6 UI
- Placement: combo in the `CreativeExtra` slot beside Fill Type (docs/dev/11: core character stays on the primary surface). Visible in Simple and Advanced (F-M04 row `personality`).
- Control name "Personality". Tooltip: "Gives fills a character by nudging which effects are chosen and how busy they are. Your own knob settings are not changed. Off = your settings only." Per-item tooltips use `personalityBlurb`.
- Help (order 1500): one paragraph per personality, plus "A weight at 0 stays off, and a manually chosen Fill Type overrides the personality".
- Accessibility: combo labelled; selected item announced by JUCE; keyboard via normal combo handling when keyboard navigation is enabled. Reduced Motion: no effect.
- Narrow: the combo shrinks to 96 px with truncated text and full text in tooltip.
- A small "modified" tick appears on Fill Type, Density, Complexity, Chaos, Intensity labels once F-S05 exists (not required for acceptance).

## 7 MIDI / automation / CLAP / host
Host-automatable choice parameter and MIDI-learnable (CC value mapped to 9 steps by the existing mapping). Automation changes take effect from the next block; running fills unaffected. CLAP/VST3 expose it as a stepped parameter with names. No latency impact. Because it is automatable it can also be used by F-L06 macros later.

## 8 Tests (`Tests/features/TestPersonality.cpp`)
C-1 list plus: `normalisation_preserves_ratios_and_zeros` | `boost_not_lost_when_user_weight_high` (user 100% Delay, Dub: other weights shrink instead) | `table_dimensions_and_8_modes_defined` | `unhinged_flattens_nonzero_only` | `fill_type_forced_skips_weights_but_applies_scalars` | `all_zero_effective_falls_back` | `scalars_clamped_0_1` | `deterministic_across_block_sizes` | `preview_sees_personality` | `ids_locked_and_old_state_off` | `no_audio_thread_allocation`. Fixtures: drum loop (scenario A), 500-gap histogram harness from F0-7 TestRig.

## 9 Dependencies & build order
Needs F0-1, F0-5. C-0/F-S05 nice-to-have. Feeds F-M01 (preview realism), F-S08 (Glitch Roulette), F-L06 macros (both write block stage; order personality -> macros). Build after F-M05/F-M07 in any order; before the preset pass.

## 10 Effort & risks
Effort M. Risks: tuning table by numbers may not sound as intended (budget one listening pass inside this ticket's PR, tune only here); `chaosFlatten` exponent in `chooseAlgorithm` can mute multiplier effects at high Chaos (check histogram at chaos 80); two "Unhinged"-style paths if F-M13 lands later (hook already stubbed).

## 11 Acceptance checklist
- [ ] Off bit-identical; golden A-F unchanged
- [ ] Normalisation per gap 1 implemented and tested; units are 0..1
- [ ] Histogram/distinctness tests pass; listening check recorded in `docs/progress/F-M06.md`
- [ ] `fm06_personality` locked, automatable, MIDI-learnable, absent in old states
- [ ] Combo, tooltips, Help present; usable at 1020x620 and by keyboard
- [ ] No parameter written by the overlay (test)
