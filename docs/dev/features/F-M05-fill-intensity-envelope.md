# F-M05 Fill intensity envelope (completion spec over C-4)

Status: canonical definition is `04-SPEC-CREATIVE.md` C-4 (shapes, level and cutoff formulas, tests). This file fixes the integration points that C-4 leaves open and replaces the parameter IDs.

## 1 Summary & user-visible behaviour
A shape control plus amount knob make every fill evolve over its own length: Rise, Fall, RiseFall, RandomWalk or Flat. At amount 0 or Flat the output is bit-identical to today. With amount > 0 the voice gain follows `level = 1 - a*(1 - s(age))` and a one-pole low-pass follows `fc = 20000 * 0.1^(a*(1 - s))`, so a "Rise" fill also opens up in brightness. Gaps in C-4 closed:
1. Order in the voice chain is fixed: generator -> modifiers -> band isolation -> **envelope stage** -> existing `envelopeAt(elapsed) * amp` fade gain -> duck -> kill -> sanitise (`FillVoice::process` around line 655-690). The envelope never touches `totalLength`, fades, duck or `requestStop`.
2. Smoothers are initialised to the target value at voice start. C-4's 5 ms one-pole starting from 1.0 would make "Rise, amount 100" start with a 5 ms loud burst; start state = `level(s(0))`, `fc(s(0))`.
3. Shape and amount are captured into `VoiceSetup` when the voice starts. Moving the knob mid-fill affects the next fill only (deterministic, no zipper, preview/lock replay the same).
4. The envelope is a global performance setting, not part of `FillRecipe` (F0-3 has no field). Fill Lock replays the recipe under the current envelope setting.
5. Cutoff cap: `fc = min(fc, 0.49 * sampleRate)`, so 44.1 kHz stays valid; filter is bypassed only when `a == 0` or Flat.
6. RandomWalk: knots `k0..k4` drawn from `Rng` seeded `hashCombine (setup.rngSeed, kSalt_Envelope)`, draw order k0..k4, positions `i/4`, smoothstep interpolation; same seed -> same curve, in preview and natural fills.

## 2 Success / quality metric
- Neutrality: golden A-F bit-identical with defaults.
- Shape fidelity: on stationary pink noise through a 300 ms Freeze fill, the short-time RMS (10 ms windows) of Rise/Fall/RiseFall at a = 1 correlates >= 0.9 with the nominal shape; Flat correlation irrelevant.
- Loudness sanity: RMS of RiseFall a = 1 within -9 +/- 3 dB of Flat (documented, not compensated).
- No clicks: max abs first difference over the fill <= 1.5x the same metric of the Flat render at identical seed.

## 3 Design
- Files: `Source/features/envelope/{Envelope.h,Envelope.cpp,Gui.h,Gui.cpp,Help.cpp}`, `Tests/features/TestEnvelope.cpp`, `Source/params/fm05.{h,cpp}`, `tools/param_ids/fm05.lock`, cmake fragments.
- Pure API (no JUCE types in the header):
  ```cpp
  enum class FillShape : std::uint8_t { Flat, Rise, Fall, RiseFall, RandomWalk };
  float shapeAt (FillShape, float age01, const float knots[5]) noexcept;       // [0,1]
  struct EnvelopeStage {
      void start (FillShape, float amount01, std::uint64_t voiceSeed, double sr) noexcept;
      void process (float& l, float& r, float age01) noexcept;                  // no-op when inactive
      bool active() const noexcept;
  };
  ```
- Voice hook (single call, guarded): in `FillVoice::process`, after band isolation: `if (envStage.active()) envStage.process (l, r, (float) elapsed / (float) totalLength);` plus `VoiceSetup::{envShape, envAmount}` (neutral 0). `trySchedule`/`startPreview` copy `s.perf.envShape/envAmount` into the setup.
- `Effective` / `PerformanceSettings` already carries `envShape`, `envAmount01` (F0-5); `gather` reads the two `fm05_*` ParamCache atomics.
- State per voice: 2 smoothed floats, 2 one-pole states, 5 knots (about 60 bytes), preallocated inside `FillVoice`.

## 4 Parameters & state
New IDs (prefixed per ticket convention; they replace the unreleased `fillShape` / `fillShapeAmount` in the 02 registry and the ENVELOPE group slot, same group position). Group file `Source/params/fm05.h/.cpp`, lock `tools/param_ids/fm05.lock`, appended only:

| ID | Type / range | Default | Unit | Notes |
|---|---|---|---|---|
| `fm05_shape` | choice: Flat, Rise, Fall, RiseFall, RandomWalk | Flat | n/a | append-only choices |
| `fm05_amount` | float 0-100, step 1 | 0 | % | `valueFromText` accepts "%"; skew none |

- Both added with a `ParameterID` version hint of 1. Not added to `creativeParameterIDs()` / `randomisableParameterIDs()` (those are allowlists in `Parameters.cpp`; new IDs stay unrandomised, Surprise unchanged). Optional later: Random All may set shape with amount <= 60 (separate PR, [golden]-tagged only if presets change).
- Validator note: `tools/validate_repo.py` ID regex `^[a-z][A-Za-z0-9]*$` rejects the underscore; extend it to `^[a-z][A-Za-z0-9]*(_[A-Za-z0-9]+)?$` for the `fmNN_` namespace (owner decision D1, shared by all F-M tickets).
- State: only the two parameters. No subtree. Old sessions: IDs absent -> defaults (Flat, 0), identical sound. `.gapfill` files likewise. Preset factory table untouched (preset pass only).
- Neutral proof: stage never constructed active; test `flat_or_zero_amount_bit_identical` on scenarios A and E with `memcmp` of the output buffers.

## 5 Real-time safety
Per sample while active: 1 multiply (gain), 1 one-pole per channel, 2 smoother steps; `shapeAt` evaluated once per control interval (every 32 samples) with a 5 ms smoother between, so no `sin`/`pow` per sample (`fc` uses a precomputed `pow`-free recurrence: compute `0.1^x` with `std::exp` once per control tick). Bounded, no allocation, no branches on unbounded data. NaN guard: output still passes `sanitise`. Reset events: voice `start()` re-initialises; `reset()`/panic kill path unaffected (envelope multiplies before kill gain).

## 6 UI
- Placement: `CreativeExtra` slot, Advanced view only (F-M04 rule rows `envelope`). Components: shape combo (name "Fill shape"), amount knob (name "Fill shape amount"), 64x24 px preview curve drawing `shapeAt` plus the resulting level range; RandomWalk preview shows the shape for the current seed's next fill is NOT possible, so it draws a dashed generic wiggle labelled "random".
- Tooltips: shape "How loud and bright each fill is over its length. Flat = unchanged." Amount "How strongly the shape is applied. 0 = off."
- Help (order 1400): formulas in plain words: "At 100% a Rise fill starts quiet and dark and ends at full level and brightness."
- Accessibility: combo and knob have titles/descriptions; preview curve is decorative (`setInterceptsMouseClicks(false,false)`, accessibility ignored) with the textual value in the knob readout. Keyboard focus only under the keyboard-navigation preference. Reduced Motion: preview is static anyway.
- Narrow: at < 1100 px width the preview curve is dropped, combo and knob remain.

## 7 MIDI / automation / CLAP / host
Both parameters are normal host-automatable parameters and MIDI-learnable (existing right-click menu). Choice params expose discrete step counts to CLAP/VST3. Automation during a fill: values are sampled at the next fill start (documented). No latency/PDC change. Preview voice obeys it.

## 8 Tests (`Tests/features/TestEnvelope.cpp`)
C-4 list plus: `smoother_starts_at_target_no_initial_burst` | `knots_draw_order_fixed_and_repeatable` | `cutoff_capped_at_44k1` | `param_roundtrip_and_old_state_defaults` | `shape_captured_at_voice_start` | `envelope_applies_after_band_isolation` | `preview_uses_envelope` | `no_audio_thread_allocation` | `block_size_invariance_with_shape_enabled` (scenario E, sizes 32..4096) | `finite_output_all_shapes_all_algos`. Golden: `Tests/golden/*/E_fm05.f32` added only in a `[golden]` PR enabling Rise a=100 (new scenario, existing goldens untouched).

## 9 Dependencies & build order
Needs F0-1 (param group), F0-3 (recipe/setup), F0-5 (PerformanceSettings). Independent of F-M01..F-M04. Small prerequisite for F-L21 style tension curves (future). Build any time after Phase 0.

## 10 Effort & risks
Effort M. Risks: level-only reduction makes strong settings quieter (users may then raise Wet; consider optional makeup later); one-pole darkening may be too subtle on low-pass-heavy material; per-voice state growth tiny. Interaction with Tape Stop (already low-passes) at a=1 may over-darken - judged by ear in the preset pass.

## 11 Acceptance checklist
- [ ] Golden A-F unchanged; defaults bit-identical
- [ ] All C-4 and added tests green; shape correlation and click metrics met
- [ ] IDs `fm05_*` locked, validator regex extended, old fixtures load
- [ ] Combo/knob/preview usable at min size, keyboard and screen reader labelled
- [ ] Parameters automatable and MIDI-learnable; not randomised
- [ ] Help and tooltips registered
