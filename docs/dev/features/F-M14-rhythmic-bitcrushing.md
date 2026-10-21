# F-M14 Rhythmic bitcrushing
Status: implementation spec, Gate C candidate (08). Category FX/FUN, value M, size M. Canonical context: `10-SPEC-LIVE-CREATIVE-EFFECTS.md` section 5.2 (controls and bounds, restated and made concrete here). **Exists already:** `Algo::Crush` (a whole fill that is crushed, bits/hold derived from Intensity, `FillVoice::genCrush`) and `Modifier::Crush` (stacked by Complexity). Neither changes. This ticket adds a separate **post-stage** that can crush any fill, with a tempo-synced step pattern and protected edges.

## 1 Summary & user-visible behaviour
- **CRUSH** card (Effects section, off by default). When on, a fill (all fills, or a chosen share) passes through a bit-depth and sample-rate reducer after the fill is generated and before its envelope. Dry audio is never touched.
- **Pattern** makes it rhythmic: Constant, Gate (crushed/clean alternate each step), Step Down (clean to full crush over 4 steps), Step Up, Random Steps. Steps are tempo-grid aligned (1/4 ... 1/32, triplets/dotted) when the host gives a tempo, otherwise they run from the fill start using the last known tempo.
- **Protected edges:** crush fades in over 3 ms and fades back to clean over the last `min (10 ms, 15 % of fill)`, so nothing harsh reaches the next protected hit.
- Character is documented and intentional: sample-rate reduction is a plain hold, so it aliases; **Anti-alias** switches on a pre-filter for a smoother result.

## 2 Success/quality metric
- Bit-depth accuracy: for a full-scale 997 Hz sine at `bits=B`, measured SNR is within 1.5 dB of `20 log10 (L * sqrt (6))` (about `6.02 B + 7.8` dB, because the grid step is `1/L` over [-1, 1]) for B in 4..16 (dither off, AA off, rate 1).
- Hold accuracy: output changes value only every `rate` samples (+/- 1 sample over 1 s) for rate in {2, 3.5, 8, 64}.
- Pattern accuracy: with valid transport, every step boundary lies within 1 sample of the absolute grid, any block size 32-8192, any sample rate 44.1-192 kHz.
- `on=false` or `mix=0`: bit-identical to the pre-feature render (golden A-I). CPU: on, 5 voices, scenario E: <= +6 % mean block time.

## 3 Design
### 3.1 Shared FX seam (defined once here; F-M15, F-M16, F-M20 reuse it verbatim; whichever of the four lands first creates it)
`Source/features/voicefx/VoiceFx.h` (POD, header only) and `VoiceFxChain.h/.cpp`:
```cpp
struct VoiceFxContext {                       // built once in FillVoice::start, read-only per sample
    double sampleRate; int channels;          // 1..2
    int totalLength, fadeOut;                 // samples
    bool transportValid; double samplesPerBeat; double ppqAtFillStart;   // ppq at the fill's first sample
    float fxSend01;                           // F-M21 lane, 1.0 = neutral
    std::uint64_t fxSeed;                     // hashCombine (setup.rngSeed, kSalt_VoiceFx)
};
struct VoiceFxSetup { Fm20Setup tape; Fm16Setup ratchet; Fm14Setup crush; Fm15Setup reson; };  // zero-init = all off
class VoiceFxChain {
public:
    void prepare (double sr, int channels);                       // allocate everything, max 192 kHz
    void start (const VoiceFxSetup&, const VoiceFxContext&) noexcept;
    void reset() noexcept;
    void process (float& l, float& r, int elapsed) noexcept;      // fixed order, each stage early-outs when off
};
```
Fixed chain order (permanent, reviewed in one place): algorithm -> modifiers -> band filter -> **tape (F-M20) -> ratchet (F-M16) -> crush (F-M14) -> resonator (F-M15)** -> mistakes (C-3) -> envelope (C-4) -> gain/duck/sanitise. Hooks in hot files: `VoiceSetup::fx` (1 member), `FillVoice` (member `VoiceFxChain fx;` + calls in `prepare/start/reset/generateSample`, 4 lines), `EngineSettings::perf.fx` (neutral struct), and one call `VoiceFxDecider::decide (setup.fx, s.perf.fx, key, setup.algo, lengthSamples, ppqAtDecision)` in `adjustRecipe` (or just before `voice->start` until F0-2 lands). Deciders use sub-RNGs only. `FillRecipe` (16 bytes, full) is **not** extended: effect choices are re-derived per fill from `key`, so Lock (C-5) keeps the character but not a literal copy; accepted limitation.

### 3.2 This module: `Source/features/bitcrush/` (`Crush.h/.cpp`, `Gui.h/.cpp`, `Help.cpp`)
```cpp
struct Fm14Setup { bool on; std::uint8_t pattern, bits; float rateDiv, mix, tone; bool aa, dither;
                   double stepSamples, stepPhaseSamples; bool gridLocked; };
Fm14Setup decideCrush (const Fm14Settings&, std::uint64_t key, double samplesPerBeat, bool transportValid,
                       double ppqAtFillStart, int lengthSamples) noexcept;     // pure; prob roll uses sub-RNG
class CrushStage { void prepare (double sr); void start (const Fm14Setup&, const VoiceFxContext&) noexcept;
                   void process (float& l, float& r, int elapsed) noexcept; };
```
**Per sample, per channel (state shared L/R hold counter, so channels stay phase-coherent):**
1. `step k`: grid locked => `k = floor ((ppqAtFillStart*spb + elapsed) / stepSamples)` with `stepSamples = divisionInBeats(div) * spb` (absolute, so loops/seeks reproduce); else `k = floor (elapsed / stepSamples)` with `spb` from `lastKnownBpm` (E-2) or 120 BPM fallback. `j = k - k0` (k0 = step at fill start).
2. Per-step state `(crushOn, bitsEff, rateEff)`: Constant `(1, bits, rateDiv)`; Gate `crushOn = (k & 1) == 0`; StepDown `t = min (1, (j+1)/4)`, `bitsEff = 16 - round ((16-bits) * t)`, `rateEff = 1 + (rateDiv-1) * t`; StepUp uses `t = max (0, 1 - j/4)`; Random: `Rng r; r.seed (hashCombine (fxSeed, (uint64_t) k))` (random access, seek safe), `bitsEff = bits + r.nextInt (5)` clamped 2..16, `rateEff = rateDiv * (0.5f + r.nextFloat())` clamped 1..64.
3. Optional AA: two cascaded `SVF` low-passes at `fc = 0.45 * sr / rateEff` (24 dB/oct), only if `aa`; else raw (aliasing is the point).
4. Tone: `tone < 0` post one-pole LP `fc = 20000 * 10^(tone/100 * 1.125)` (to ~1.5 kHz); `tone > 0` pre one-pole HP `fc = 20 * 10^(tone/100 * 1.6)` (to ~800 Hz); 0 = neither.
5. Hold: `acc += 1/rateEff; if (acc >= 1) { acc -= 1; hold = Q (x_pre, bitsEff); }` with `acc = 1` at start. `Q(x,b) = floor (clamp (x,-1,1) * L + 0.5) / L`, `L = 2^b - 1` (same grid as `quantiseBits`; `floor` not `round`/`lrint`, so no platform or FP-mode difference). Dither (optional): add `(u1 - u2) / L` (TPDF, 1 LSB) from a per-voice `Rng` seeded `hashCombine (fxSeed, kSalt_Dither)` before `Q`; no draw when off.
6. Mix: `wetAmt = mix * edge(elapsed) * fxSend01 * crushOn`, `edge = smoothstep (0, 3ms, t) * (1 - smoothstep (len-tail, len, t))`, `y = x + wetAmt * (hold - x)`. Output |y| <= max (|x|, 1); a final `sanitise` is the voice's existing one.
Bits 16 + rate 1 + no dither is the identity by construction (early-out, not by arithmetic). Latency 0.

## 4 Parameters & state
All appended in group `GAPFILL_PARAMS_FM14(X)` (`Source/params/fm14.h`, lock `tools/param_ids/fm14.lock`), after the existing groups, permanent order. `_` in IDs needs the validator regex change noted in F-M13 s4. All excluded from Random/Surprise/Undo (`performanceParameterIDs()`).

| ID | Type | Range | Default | Unit |
|---|---|---|---|---|
| `fm14_on` | bool | - | off | - |
| `fm14_bits` | int | 2-16 | 8 | bits |
| `fm14_rate` | float, skew centre 8 | 1.0-64.0 | 4.0 | x (hold factor) |
| `fm14_pattern` | choice | Constant, Gate, Step Down, Step Up, Random | Constant | - |
| `fm14_div` | choice (`divisionNames()`) | 1/1 ... 1/16. | 1/16 | - |
| `fm14_aa` | bool | - | off | - |
| `fm14_dither` | bool | - | off | - |
| `fm14_tone` | float | -100..100 | 0 | % |
| `fm14_mix` | float | 0-100 | 100 | % |
| `fm14_prob` | float | 0-100 | 100 | % of fills |

State: none beyond parameters (no subtree). Neutral proof: `fm14_on` default off; `EngineSettings::perf.fx.crush.on == false` makes `decideCrush` return a zeroed setup with zero RNG draws, `CrushStage::process` early-outs. Old sessions lack the IDs, load defaults, sound unchanged (`old_state_unchanged`).

## 5 Real-time safety
Only arithmetic on preallocated state: 2 SVF pairs, 2 one-poles, one `Rng`. No tables, no allocation, no logging. Step lookup is O(1) (floor/divide on doubles, `stepSamples >= 1` enforced: the shortest musical step, 1/32 at 300 BPM, is 25 ms (1 102 samples at 44.1 kHz); free fallback clamps to >= 64 samples). Rate/bits clamped on read; NaN/Inf input => `sanitise` to 0 before quantiser. Denormals: `ScopedNoDenormals` plus DC-free (mid-tread quantiser maps 0 to exactly 0). Resets: all five events (reset, releaseResources, sample-rate change, bypass, transport jump) end the fill; `start()` clears hold, accumulators, SVF.

## 6 UI
Slot: Effects section (11) inside Creative, foldable card "CRUSH", header toggle; parameters visible only when enabled. Controls: BITS, RATE knobs; PATTERN and DIVISION combos; AA, DITHER toggles; TONE, MIX knobs; PROB under "more". Tooltips: BITS "Resolution of the fill. Lower = grittier."; RATE "Holds each sample for N samples. Higher = more aliasing."; PATTERN "How the crush changes step by step with the song tempo."; AA "Filters before reducing the rate. Off keeps the harsh digital aliasing."; MIX "How much crushed signal replaces the clean fill." Help section "Rhythmic bitcrush" (order 1140): what aliasing is, why edges are protected, tempo behaviour table. Accessible titles: "Crush on", "Crush bit depth", etc.; combo items readable. Keyboard: card header reachable by Tab, Space toggles, arrows adjust. Reduced motion: card fold has no animation; no step-flash indicator (a text badge "step 3" is not shown in reduced mode). Narrow: card collapsed at minimum size; scroll confined to the content panel.

## 7 MIDI/automation/CLAP/host impacts
Ten new automatable params, MIDI-learnable (`allParameterIDs`). Changing them mid-fill affects the next fill only (setup captured at start; deterministic). No latency/PDC change. No tempo from host: uses E-2 memory; status footer shows "Crush: free tempo". Offline render deterministic at fixed seed.

## 8 Tests (`Tests/features/TestBitcrush.cpp`)
`off_is_bit_identical_golden_A_to_I` | `bits_snr_matches_formula` | `hold_interval_exact` (2, 3.5, 8, 64) | `fractional_rate_average_exact` | `quantiser_midtread_zero_and_symmetry` | `impulse_sine_silence_dc_nyquist` (DC gives no drift; Nyquist-adjacent content aliases as documented when AA off, attenuated >= 30 dB when on) | `nan_inf_input_recovers` | `channels_independent_values_shared_timing` | `pattern_gate_alternates_on_grid` | `step_down_up_values` | `random_steps_seek_equals_playthrough` | `grid_alignment_all_block_sizes_rates` | `loop_wrap_same_pattern` | `no_transport_fallback_uses_last_tempo` | `edges_clean_start_and_end` (first 1 ms and last 5 ms equal the clean fill within 1e-6 at mix 100 and prob 100) | `mix_zero_identity` | `dither_deterministic_and_off_draws_nothing` | `sample_rate_change_resets` | `no_audio_thread_allocation` | `cpu_ceiling` (Perf group) | `state_roundtrip_and_old_state`. Golden `FM14-A` (scenario A, pattern Gate, 1/8) per platform dir, `[golden]` PR.

## 9 Dependencies & build order
Needs F0-1/2/3/5/6/7/9 (Gate B: B2, B3, B5, B6, B7); E-2 tempo memory (soft: falls back to 120 BPM without it). Seam 3.1 shared with F-M15, F-M16, F-M20; 10 s6 order: ratchet, bitcrush, tape, resonator. F-M21 `fxSend01` and F-M23 targets (`fm14_mix`, `fm14_bits`, `fm14_rate`) consume it later.

## 10 Effort & risks
M. Risks: seam is the cross-ticket coupling (mitigation: build it once, with chain-order test); aliasing character sounds "broken" to some users (AA and Help text); `FillRecipe` not carrying FX choices, so U-1 "Audition this" of a historic recipe may sound different (note in Help); host without tempo makes the pattern free-running.

## 11 Acceptance checklist
- [ ] Golden A-I unchanged with `fm14_on` off; old states load unchanged
- [ ] SNR, hold, grid accuracy tests green at 44.1/48/96/192 kHz
- [ ] Edges clean, no click at engage/disengage; output finite for all inputs
- [ ] Seam header reviewed once, chain-order test present
- [ ] No audio-thread allocation; CPU within ceiling
- [ ] UI reviewed at minimum size, keyboard and screen-reader names verified
- [ ] Help, tooltips, lock file, cmake fragments, `docs/progress/F-M14.md` done
