# F-M15 Comb-filter resonator
Status: implementation spec, Gate C candidate (08). Category FX/MUS, value M, size M. Canonical context: `10-SPEC-LIVE-CREATIVE-EFFECTS.md` section 5.4. **Exists already:** `Algo::Comb` (`FillVoice::genComb`, a damped feedback comb excited by the opening hit, pitch picked at random from a fixed set rooted at 110 Hz, integer-sample delay, feedback from fill length). That algorithm is not removed; this ticket adds (a) user tuning for it and (b) a resonator **post-stage** usable on any fill, with fractional-delay tuning accuracy, a stability proof, and optional scale snapping.

## 1 Summary & user-visible behaviour
- **RESONATOR** card (Effects section, off by default). Turns the fill into a pitched, metallic ring tuned to a note or frequency you set.
- **Pitch mode:** Free (Hz), Note (MIDI note), or Key (each fill picks a note of the song key deterministically from its seed, in MIDI 36-84). The key is the manual Root + Scale of F-L18 (shared model, `Scale.h`); Key mode is available while F-L18 is enabled and otherwise shows "Turn on Key & Scale". This module never guesses a key and never reads one from the host (10 s5.4; F-X03 may later supply an auto-detected key through F-L18's resolver with no change here).
- Decay (ring time), Damping (how fast highs die), Spread (L/R detune for width), Mix.
- If the fill is `Algo::Comb` and the resonator is on, the Comb algorithm takes its pitch from the same settings (replacing the random choice) and the post-stage is skipped for that voice, so it never rings twice.
- The ring never outlives the fill: it is cut by the fill's existing fade-out, so it cannot leak into a protected hit.

## 2 Success/quality metric
- Tuning error <= 5 cents (measured by FFT-peak interpolation of the impulse response) for `f0` in {55, 110, 220, 440, 880, 1760, 3520} Hz at 44.1, 48, 96, 192 kHz, damping 0-100 %.
- Stability: for every parameter corner (decay 2000 ms, damping 0, spread 50) and every input in the test set, peak output <= 4.0 and finite; after 10 s of full-scale noise input the loop energy decays when the input stops.
- Neutral: `fm15_on=false` or `mix=0` bit-identical to golden A-I; `Algo::Comb` unchanged when `fm15_on=false`.
- CPU: on, 5 voices, scenario E: <= +5 % mean block time.

## 3 Design
Uses the shared FX seam (F-M14 s3.1: `VoiceFxContext`, `VoiceFxChain`, chain order ... crush -> **resonator** -> mistakes). If F-M14 has not landed, this PR creates that header verbatim. Module `Source/features/resonator/` (`Resonator.h/.cpp`, `Tuning.h/.cpp`, `Gui.*`, `Help.cpp`). Scale data is **not** redefined here: it reuses F-L18's `ScaleSetup` (`mask` = allowed absolute pitch classes) from `Source/features/scale/Scale.h`.

```cpp
// Tuning.h - pure, unit-tested
float midiToHz (float note) noexcept;                                   // 440 * 2^((n-69)/12), A4 = 440 Hz
int   pickKeyNote (const ScaleSetup&, std::uint64_t key) noexcept;      // MIDI 36..84 whose pitch class is in ScaleSetup::mask; one sub-RNG draw (kSalt_Fm15)
struct Fm15Setup { bool on; float f0Hz, spreadCents, decayMs, damp01, mix; };
Fm15Setup decideResonator (const Fm15Settings&, const ScaleSetup&, std::uint64_t key, int lengthSamples, Algo) noexcept;
// Resonator.h
class ResonatorStage {
public:
    void prepare (double sr);                    // 2 x 8192 float rings per channel pair (max 192 kHz / 30 Hz = 6400 + margin)
    void start (const Fm15Setup&, const VoiceFxContext&) noexcept;
    void process (float& l, float& r, int elapsed) noexcept;
};
```
**Algorithm (per channel, `fL = f0 * 2^(-spread/2400)`, `fR = f0 * 2^(+spread/2400)`):**
Loop `y[n] = x[n] + g * DC( LP( y[n-D] ) )`, output `o[n] = y[n] * (1 - g) * mixGain`.
- Damping: one-pole `LP: v[n] = (1-a) u[n] + a v[n-1]`, `a = lerp (0.02, 0.85, damp01)` (corner roughly 20 kHz down to 1.5 kHz at 48 kHz). Its group delay near DC is `dLP = a / (1 - a)` samples.
- Total loop delay `T = sr / f`; ring length `D = T - dLP - dAP`; split `D = Di + frac`; `frac` realised by a first-order all-pass `H(z) = (c + z^-1) / (1 + c z^-1)`, `c = (1 - frac) / (1 + frac)` (flat magnitude, fractional delay ~ `frac` at low frequency). Keep `frac` in [0.1, 1.1) by moving one sample into `Di` (standard all-pass range) to avoid the all-pass pole near -1. This is what gets tuning under 5 cents; integer-only delay (as in `genComb`) is up to ~40 cents out at 2 kHz / 44.1 kHz.
- Limits: `f` clamped to [30 Hz, min (4000 Hz, sr/8)] so `Di >= 6`; ring is 8192 (power of two, mask wrap).
- Decay to feedback: RT60 `decayMs` in [20, 2000]; `g = 10^(-3 * T / (sr * RT60))`, clamped to `[0, 0.985]` (`kMaxFeedback`, same ceiling as `genComb`).
- **Stability argument (documented in code):** the loop transfer function magnitude is `g * |LP| * |AP| * |DC| <= g < 1` at every frequency (LP and DC blocker have gain <= 1, all-pass exactly 1), so the feedback comb has all poles inside the unit circle (Smith, *PASP*, "Filtered Feedback Comb Filters"). Floating-point safety: `y` is clamped to +/-4 in the loop (as `genComb`), NaN/Inf input replaced by 0, state flushed by `ScopedNoDenormals` and an explicit `if (|y| < 1e-20) y = 0`.
- DC / low-frequency build-up: resonant peak gain of a comb is `1/(1-g)` (up to 66, 36 dB); the output is multiplied by `(1-g)` so the peak sits near unity, and a one-pole DC blocker (`fc = 20 Hz`) in the loop stops sub-audio accumulation. A final `softClip` at +/-1.5 guards residual peaks.
- Mix: `out = x + mixEff * (o - x)`, `mixEff = mix * fxSend01 * edge`, `edge` = 3 ms fade-in and the voice's `fadeOut` tail guard (as F-M14).
- Excitation: continuous (the fill itself drives the loop), so transients of the fill ring; no extra excite window.
**`Algo::Comb` integration:** `VoiceSetup::combTuneHz` (0 = derive as today, byte-identical). Precedence with F-L18 (which already snaps the Comb pitch to the key when enabled with scope Comb/All): explicit user tuning wins. In Free/Note mode with `fm15_on`, `decideResonator` supplies `f0` and F-L18 is bypassed for that voice; in Key mode `combTuneHz` stays 0 and F-L18's Comb site does the tuning (same notes, same mask), while the post-stage is skipped for Comb voices. When `fm15_on` and `algo == Comb` (Free/Note), `configureAlgorithm` uses `combDelay = clamp (round (sr / f0 - (1 - combDamp) / combDamp), 8, combSize - 4)` (the second term is the DC group delay of `genComb`'s damping one-pole) instead of the random semitone table; `genComb` is otherwise unchanged. `combSize` (60 ms) covers periods down to 30 Hz (33.3 ms). This path stays integer-delay, so its tuning is only good to the rounding error (test `comb_algo_tuning_within_25_cents_below_1k`); the post-stage is the accurate one.

## 4 Parameters & state
Group `GAPFILL_PARAMS_FM15(X)` (`Source/params/fm15.{h,cpp}`, `tools/param_ids/fm15.lock`), appended; `_` regex note as F-M13 s4. All in `performanceParameterIDs()`. Root and scale are F-L18's `fl18_root`/`fl18_scale`, not duplicated.

| ID | Type | Range | Default | Unit |
|---|---|---|---|---|
| `fm15_on` | bool | - | off | - |
| `fm15_mode` | choice | Free, Note, Key | Note | - |
| `fm15_freq` | float, skew centre 330 | 30-4000 | 220 | Hz |
| `fm15_note` | int | 24-96 | 57 | MIDI (A3), used in Note mode |
| `fm15_decay` | float, skew centre 300 | 20-2000 | 400 | ms (RT60) |
| `fm15_damp` | float | 0-100 | 40 | % |
| `fm15_spread` | float | 0-50 | 0 | cents |
| `fm15_mix` | float | 0-100 | 35 | % |
| `fm15_prob` | float | 0-100 | 100 | % of fills |

No subtree. Neutral proof: `fm15_on` off => `decideResonator` returns `on=false`, `combTuneHz=0`; stage early-outs. Old sessions: IDs absent, defaults, identical sound.

## 5 Real-time safety
Rings (`2 x 8192 x 4 B` per voice, 5 voices ~ 330 KB) allocated in `prepare()` (max rate), zeroed in `start()`. Per sample: 2 one-poles, 1 all-pass, 1 ring read/write per channel; no loops, no `pow` (coefficients computed once in `start()`). Key pick is one sub-RNG draw per fill. Sample-rate change re-prepares and clears; bypass/transport jump end the fill. `kMaxFeedback`, `kMaxAbs` are `constexpr`.

## 6 UI
Slot: Effects section card "RESONATOR" (11). Controls: MODE combo, PITCH (Hz knob in Free, note stepper with name "A3" in Note, read-only text "Key: D minor" in Key mode with a link to the Key card), DECAY, DAMP, SPREAD, MIX knobs. Tooltips: DECAY "How long the ring lasts (RT60). Never longer than the fill."; DAMP "Higher = highs die faster, softer ring."; Key mode "Each fill picks a note from your song key (set in Key & Scale). GapFill does not guess your key." Help "Comb resonator" (order 1150) with a note-name table and a statement that the host key is not read. Accessible: note stepper announces name ("A 3, 220 hertz"); combos titled. Keyboard: arrows step notes by semitone, Shift+arrows by octave. Reduced motion: no ring-down animation. Narrow: card collapsed; scale combos wrap onto a second row only when expanded.

## 7 MIDI/automation/CLAP/host impacts
9 automatable params; `fm15_note` stepped. MIDI note input does **not** tune the resonator in v1 (GapFill accepts MIDI for CC only); a "follow last MIDI note" option is out of scope and would need `acceptsMidi` handling decisions in F-M10. No PDC change. Hosts with different tuning reference: A4 fixed 440 Hz (documented).

## 8 Tests (`Tests/features/TestResonator.cpp`)
`tuning_error_under_5_cents_matrix` | `allpass_fraction_range` | `fractional_delay_vs_integer_error` (documents the gain over `genComb`) | `stability_corners_finite_bounded` | `energy_decays_after_input_stops` | `feedback_clamped_at_max` | `dc_input_no_buildup` | `peak_normalised_near_unity` | `stereo_spread_cents_exact` | `mono_channel_ok` | `silence_denormal_flush` | `nan_inf_recovers` | `key_pick_in_mask_and_range` (every F-L18 scale x 12 roots x 1000 keys) | `key_pick_deterministic_seek_safe` | `key_mode_inactive_when_fl18_off` | `explicit_tuning_beats_fl18_on_comb` | `comb_algo_uses_user_pitch_when_on` | `comb_algo_unchanged_when_off` (golden D) | `no_double_resonance_on_comb_voice` | `ring_cut_by_fadeout_no_leak` | `sample_rate_change_resets` | `no_audio_thread_allocation` | `state_roundtrip_and_old_state`. Golden `FM15-A` per platform, `[golden]`.

## 9 Dependencies & build order
F0-1/2/3/5/6/7/9; seam from F-M14/F-M16 (or created here); F-L18 for Key mode only (Free and Note work without it; if F-L18 has not landed, Key is hidden). Soft: E-2, F-X03 (auto key through F-L18). Order per 10 s6: last of the four FX (ratchet, bitcrush, tape, resonator). F-M23 targets: `fm15_mix`, `fm15_decay`, `fm15_damp`. F-M21 `fxSend01` scales `mix`.

## 10 Effort & risks
M. Risks: the all-pass tuning must be verified at all four rates (matrix test, not spot checks); long RT60 at low `f0` can make the fill sound boomy (output normalisation and `protLow` still apply downstream); Key mode may be mistaken for key detection (Help wording); memory x5 voices.

## 11 Acceptance checklist
- [ ] Tuning error <= 5 cents across the matrix at 44.1-192 kHz
- [ ] Stability corners pass; no unbounded tail; ring ends with the fill
- [ ] Golden A-I and D unchanged when off; old states unchanged
- [ ] `Algo::Comb` honours user pitch only when `fm15_on`
- [ ] No audio-thread allocation; CPU within ceiling
- [ ] UI reviewed at minimum size; note stepper accessible; Help merged
- [ ] Lock file, cmake fragment, progress note done
