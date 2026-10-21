# F-X02 Spectral freeze and morph

Status: specified (this pass), not implemented. Size L (MVP) + stretch. Depends on F0-1/2/3/5, Gate B. Refines `10-SPEC-LIVE-CREATIVE-EFFECTS.md` section 5.8 (that section remains the product intent; this file is the implementation spec). Related but separate: F-M19 (time-domain Freeze and Scrub), F-L20 (formant shifting).

## 1. Summary and user-visible behaviour
The existing **Spectral** fill smears the captured material. With F-X02 on, the Spectral fill instead **freezes** the spectrum of the moment just before the gap and sustains it as a stable, pad-like texture (partials stay coherent instead of smearing), optionally **morphing** toward a second spectrum (an earlier moment of the same material, a brighter or a darker version) over the length of the fill, with a decay control and a shimmer control that adds air around the held partials. The wet result stays inside the gap and obeys protection like every fill. Off by default; legacy Spectral fills are unchanged.

Nothing here extends a fill beyond the gap or keeps sound after the fill ends. MVP excludes pitch shift, formant shift, stereo width and user-grabbed morph targets (stretch, section 10).

## 2. Success / quality metric
| # | Metric | Target |
|---|---|---|
| M1 | Freeze of a steady 440 Hz sine: output frequency error | < 1 cent after the attack |
| M2 | Level stability of a frozen sine / two-sine chord after attack (`Hold` decay) | within +-1 dB over 500 ms |
| M3 | Partial coherence: spectral peak width of a frozen sine (-6 dB) vs a Hann-windowed 8192-point reference | <= 1.5x reference width (no phasiness smear) |
| M4 | Click-free: max absolute second difference of the output vs a pure sine reference of equal level | <= 1.2x |
| M5 | Decay: measured RMS time constant vs `fx02_decay` setting | within +-10 % |
| M6 | Morph endpoints: `morph = 0` equals Freeze output (max error 1e-6); `morph = 1` peak magnitudes within 1.5 dB of target frame | pass |
| M7 | CPU steady state, 48 kHz stereo | <= 1 % of one core; fill-start spike <= 35 % of a 64-sample/48 kHz block (p99.9) |
| M8 | Memory | <= 400 KB at 192 kHz |

## 3. Design
### Modules (`Source/features/spectralfreeze/`)
`SpectralFreezer.h/.cpp` (engine, pure DSP, JUCE `dsp::FFT` only), `SpectralTargets.h` (pure functions: tilt, earlier-frame selection, peak picking), `Gather.cpp`, `Gui.*`, `Help.cpp`, `Info.cpp`. Tests `Tests/features/TestSpectralFreeze.cpp`.

### Engine
```cpp
class SpectralFreezer {
public:
    enum class Mode : std::uint8_t { Off, Freeze, Morph };
    enum class Target : std::uint8_t { Earlier, Brighter, Darker };
    struct Params { Mode mode; Target target; float decaySeconds /*0 = hold*/; float shimmer01; float morph01;
                    float levelGain; int lengthSamples; std::uint64_t seed; };
    void prepare (double sampleRate, int numChannels);   // fftOrder = sr > 100000 ? 12 : 11 ; hop = N/4 ; allocates everything
    void reset() noexcept;
    bool isClaimed() const noexcept;  void claim() noexcept;  void release() noexcept;   // one instance, shared like SpectralSmear
    bool begin (const float* capL, const float* capR, int capLen, const Params&) noexcept;  // analysis, returns false if capLen < N + hop
    void process (float& outL, float& outR) noexcept;   // one sample pair, overlap-add synthesis
};
```
**Analysis (`begin`, 4-6 FFTs, bounded).** Hann window `N`. Per channel: frame `A` = last `N` samples of the capture; `Aprev` = the frame ending `hop` earlier; optional `E` (Earlier) = frame ending `min(capLen - N, round(0.2 s * sr))` samples before the end (clamped to the capture start; if impossible falls back to `Brighter`). Store `magA[k]`, `phaseA[k]`, and the instantaneous frequency per bin from the phase difference of `A` and `Aprev`:
`omega_k = 2 pi k / N`, `dphi = wrap(phaseA[k] - phasePrev[k] - omega_k * hop)`, `omegaInst[k] = omega_k + dphi / hop`.
**Peaks (identity phase locking, Laroche and Dolson 1999).** Bin `k` is a peak if `mag[k] > mag[k+-1]` and `> mag[k+-2]` and `> 1e-2 * max(mag)` (-40 dB). Region of influence of peak `p` = bins between the midpoints to the neighbouring peaks (max 256 peaks; extra weakest ones are dropped). Stored `peakOf[k]` as `uint16`.
**Targets (magnitude only, deterministic, no cross-fill memory so seeks and offline renders reproduce):**
* `Earlier`: `magB = |E|` (same channel).
* `Brighter`: `magB[k] = magA[k] * clamp((k / k0)^0.5, 0.25, 4)` with `k0 = round(1000 Hz / binHz)`; renormalised to the same total energy as `magA`.
* `Darker`: exponent `-0.5`, same normalisation.
**Synthesis per hop** `h = 0,1,2...` (one hop = `N/4` samples):
1. Morph position `m(h) = morph01 * smoothstep(0, 1, h*hop / lengthSamples)` (0 in Freeze mode).
2. Magnitude `mag[k] = exp((1-m) log(magA[k]+eps) + m log(magB[k]+eps)) * g^h` with decay `g = exp(-hop / (decaySeconds * sr))` (1 when hold), times `levelGain`.
3. Phase: peak phase `theta_p(h) = phaseA[p] + h * hop * omegaInst[p]` (IF blended with target IF `(1-m)*IF_A + m*IF_B` where B's IF = A's for Brighter/Darker and the bin centre for Earlier); bin phase `theta_k = theta_p(h) + (phaseA[k] - phaseA[p])`. Skirt bins (more than 2 bins from their peak) get `+ shimmer01 * pi * u` with `u` from a sub-RNG `hashCombine(seed, kSalt_Fx02)`, one draw per skirt bin per hop (draw count fixed per hop, so determinism is independent of block size). Locked peak bins get no random phase.
4. Wrap phases to +-pi every hop (precision), build conjugate-symmetric spectrum, inverse FFT, multiply by the synthesis Hann window, scale `2/3` (Hann x Hann at 75 % overlap sums to 1.5; same constant as `SpectralSmear`), overlap-add into a circular `accum`.
The first output hop ramps in naturally over ~2 hops (~21 ms at 48 kHz) on top of the voice's own fade-in, so the freeze fades up as a swell; no pre-roll FFTs are spent.
Output passes `sanitise`. Channels are processed independently so the stereo image of the captured material is preserved.

### Integration hooks (<= 10 lines)
* `FillEngine` gets member `SpectralFreezer freezer;` prepared/reset next to `spectral`; `FillVoice::start (setup, history, spectral, SpectralFreezer* freezer = nullptr)` (defaulted so existing call sites compile).
* `VoiceSetup` gets `SpectralFreezeSetup freeze;` (mode Off default: `mode`, `target`, `decaySeconds`, `shimmer01`, `morph01`, `levelGain`).
* `FillVoice::genSpectral`: first lines `if (freezer != nullptr) { freezer->process (l, r); playPos += 1.0; return; }`; `start()` for `Algo::Spectral` with `freeze.mode != Off` claims the freezer instead of `spectral` and calls `begin` on the voice capture. If claim or `begin` fails the voice falls back to the legacy smear path (same fallback style as today) and sets a flag bit in `FillRecipe::flags` (bit5 `freezeFellBack`).
* `trySchedule`/`chooseAlgorithm` already skip Spectral while `spectral.isClaimed()`; the same check must also look at `freezer.isClaimed()` (one added condition).
* `EngineSettings::perf.fx02` filled by `Gather.cpp` from the `ParamCache`.

## 4. Parameters and state
Group `fx02`, file `Source/params/fx02.h`, lock file `tools/param_ids/fx02.lock`, appended to `GAPFILL_PARAMS_EXT`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fx02_mode` | choice: Off, Freeze, Morph | Off | - |
| `fx02_target` | choice: Earlier moment, Brighter, Darker | Earlier moment | - |
| `fx02_decay` | float 0-100 (0 = Hold; maps to 8 s -> 0.15 s exponentially) | 0 | % |
| `fx02_shimmer` | float 0-100 | 10 | % |
| `fx02_morph` | float 0-100 (final morph position; Morph mode only) | 60 | % |
| `fx02_level` | float -12..+6 | 0 | dB |
* Random sets: `fx02_decay`, `fx02_shimmer`, `fx02_morph` may be randomised only if `fx02_mode != Off`; `fx02_mode` joins `performanceParameterIDs()`.
* State: parameters only, no subtree, `stateVersion` unchanged; old sessions get defaults (Off).
* Neutral proof: with `fx02_mode = Off` `VoiceSetup.freeze.mode = Off`, the freezer is never claimed, `genSpectral` takes the legacy path, goldens A-F bit-identical (note scenario B uses spectral mode detection but not necessarily the Spectral algorithm; add scenario `X2` that forces the Spectral algorithm with mode Off and requires bit-identity with the pre-feature render).

## 5. Real-time safety
Allocation only in `prepare`: per channel `fifo/accum[N]`, `magA/magB/magE/phaseA/phasePrev/omegaInst[N/2+1]`, `peakOf[N/2+1]` (uint16), plus two `fftData[2N]` => about 0.35 MB at N = 4096 stereo. `begin` is bounded: <= 6 FFTs + O(N) loops. `process` costs 1 inverse FFT per channel per hop plus O(N) trig: use a precomputed `sinCos` via `std::polar`-free recurrence per bin or a 4096-entry table with linear interpolation (decide in the PR; test accuracy 1e-4). No locks, no logging. Reset table: `reset()` clears accum and counters; `releaseResources` releases the claim; sample-rate change re-runs `prepare` (N may change 2048 <-> 4096); bypass/panic: the voice's existing kill ramp fades the output, then `release()`; transport jump: existing `noteTransportJump` plus voice kill; Capture hold: capture buffer comes from the latched history, nothing special.

## 6. UI
Spectral tab (Advanced view), a foldable **Freeze** card in the effects group, body visible only when `fx02_mode != Off`. Controls: mode segmented control (Off / Freeze / Morph), target combo (Morph only), decay, shimmer, morph, level. A static spectrum thumbnail (message thread, from a 64-band downsample of the last frozen frame published via a seqlock-free atomic array of 64 floats) is optional and skipped under Reduced Motion (replaced by text "Frozen: 18 partials, 220-2400 Hz"). Tooltips from ParamInfo, e.g. `fx02_shimmer`: "Adds soft air between the held notes. 0 keeps only the frozen partials." Accessible names for every control; keyboard Tab order mode -> target -> knobs; Help section "Spectral freeze" order 1710.

## 7. MIDI / automation / CLAP / host
Parameters automatable; mode/target changes apply at the next Spectral fill. The existing `Surprise`/`Random` rules above apply. No latency or PDC change (synthesis only starts at the gap). The CPU of the freezer is only spent while a freeze fill is active. CLAP/AU/VST3 identical.

## 8. Tests (`Tests/features/TestSpectralFreeze.cpp`, group `SpectralFreeze`)
`freeze_sine_frequency_within_1_cent` | `freeze_level_stable_hold` | `freeze_two_partials_preserved` | `freeze_peak_width_not_smeared` (M3) | `freeze_no_click_second_difference` | `decay_time_constant_matches` (M5) | `shimmer_zero_is_pure_locked_partials` | `shimmer_100_raises_spectral_flatness` | `shimmer_draw_count_independent_of_block_size` | `morph_zero_equals_freeze` | `morph_one_matches_target_peaks` | `target_earlier_falls_back_when_capture_short` | `target_tilt_energy_conserved` | `begin_refuses_short_capture_and_falls_back` | `claim_release_exclusive_with_smear` | `fftOrder_12_at_192k` | `mono_source_stereo_output_identical_channels` | `silence_in_silence_out_no_nan` | `output_bounded_extreme_params` | `block_size_invariance_32_to_4096` | `deterministic_same_seed_and_ppq` | `off_is_bit_identical_golden_A_to_F_and_X2` | `no_audio_thread_allocation` | `fill_start_spike_measured` (reported, asserted <= 35 %) | `panic_during_freeze_fades_and_releases` | `sample_rate_change_during_idle`. Fixtures: sine, two-sine chord, 3-note guitar-like decaying pluck set, white noise, drum loop. Cross-platform golden for the freeze render uses the F0-7 tolerance (`1e-5` max error); if FFT implementations differ per platform keep per-platform goldens.

## 9. Dependencies and build order
F0-1/2/3/5, B3 (voice start signature), `SpectralSmear` unchanged. Order: engine + tests (pure) -> hooks -> params/UI. F-L20 formant shift and F-M19 reuse `SpectralTargets.h`. F-X07 may lower to `N = 1024` in the Light tier.

## 10. Effort and risks
MVP ~ 6 ideal days (engine 3, hooks 1, UI 1, tests 1). Risks: (a) IF-based phase propagation drifts for inharmonic or noisy frames, audible as slow beating: shimmer and decay hide it, M1/M2 tests bound it for tonal input; (b) peak locking on dense mixes yields a metallic pad: documented as a character, not a defect; (c) fill-start spike: measured, fall back to computing `E` lazily on the second hop if over budget; (d) FFT numeric differences across platforms: per-platform goldens. Stretch (separate tickets): pitch shift by bin remapping (+-12 semitones), formant (cepstral envelope warp, shared with F-L20), stereo width (mid/side magnitude scaling), user "Grab target" command into a 2-slot target memory (state-free, runtime only), freeze-to-scene capture, tempo-synced morph curve.

## 11. Acceptance checklist
- [ ] Off: goldens A-F and X2 bit-identical, zero extra RNG draws
- [ ] M1-M8 measured and recorded in `docs/progress/F-X02.md`
- [ ] Block-size invariance and determinism on Linux and Windows
- [ ] AllocGuard and TSan (claim/release) tests green
- [ ] Old-state fixtures load with Off; all `fx02_*` round trip
- [ ] UI checklist: tooltip, accessible names, keyboard, Reduced Motion, min size, Help
- [ ] Fallback to legacy smear is visible in History/status
- [ ] Unverified items (real host, 192 kHz machine) listed honestly
