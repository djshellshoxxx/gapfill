# F-L20 Formant transformations

Status: specified, not approved. Size M-L. Category FX/MUS, value M.
Overlap: `10` section 5.7 "Formant shifting" (purpose, controls, bounds, test headings). This file is the implementation-complete spec and chooses the method and the latency policy. Related but separate: F-X02 spectral freeze/morph (XL), existing `SpectralSmear` (magnitude smearing, shares FFT style only).

## 1 Summary and user-visible behaviour
Changes the spectral envelope (the broad resonant shape) of a fill's material without changing its pitch or timing: Shift moves the resonances up or down up to +-12 semitones, Tilt brightens or darkens, Vowel imposes A/E/I/O/U-like resonances (character only, not speech). Preserve blends between the imposed shape and the original envelope. Applies per fill voice, to generated material only. Amount 0 = off.

## 2 Success / quality metric
- Amount 0: bit-identical to baseline (no processor claimed, no latency).
- Pitch preservation: a 220 Hz harmonic test tone (sawtooth) with Shift = +7 semitones keeps its fundamental within 3 cents, while the spectral centroid rises by 30..60% of the expected `2^(7/12)` factor.
- Transient timing: peak of a click through the stage is at the expected offset (`kLatency`) within 1 sample, with pre-echo below -30 dB at Eco and -35 dB at Normal.
- Level: processed RMS within +-1.5 dB of unprocessed for pink noise at any setting.
- CPU: at most 2 concurrent instances; below 3% of one core in total at 48 kHz stereo, 512 block.

## 3 Design
Files: `Source/features/formant/FormantProcessor.{h,cpp}` (streaming STFT, mirrors the `SpectralSmear` pattern), `Vowels.h` (tables), `FormantSetup.h` (POD in `VoiceSetup`), `Gui.{h,cpp}`, `Help.cpp`.

### 3.1 Method: cepstral envelope warping in a streaming STFT
Frame `N = 512`, hop `H = 128` (75% overlap), Hann analysis and synthesis with 2/3 normalisation (same overlap-add scheme and `juce::dsp::FFT` use as `SpectralSmear`). Per frame, per channel:
1. `X(k) = FFT(frame)`, `L(k) = ln (max (|X(k)|, 1e-9))`.
2. Real cepstrum `c = IFFT(L)`; keep quefrencies `n < nc` with a half-cosine taper over the last 25%; `nc = round (detailMs * 0.001 * fs)`, detail Low/Med/High = 0.5/1.0/2.0 ms (about 24/48/96 at 48 kHz). Envelope `E(k) = exp (Re FFT(c_liftered))`.
3. Warp: `alpha = 2^(shift/12)`, `E'(k) = E (k/alpha)` by linear interpolation on the log-envelope, clamped to the valid bin range (edge values extended).
4. Tilt: `logE'(k) += ln(10)/20 * tilt * log2 (max (f_k, 50) / 1000)` with `tilt` in dB per octave, total added gain limited to +-24 dB.
5. Vowel: synthetic envelope `V(k) = 0.05 + sum_i 1 / (1 + ((f_k - F_i) / (B_i/2))^2)` with `B_i = 60 + 0.1 F_i` and formants (Hz) A 730/1090/2440, E 530/1840/2480, I 270/2290/3010, O 570/840/2410, U 300/870/2240 (standard average adult values, Peterson-Barney). `logV` is shifted to the same mean as `logE`. Final `ln E'' = p * ln E'_warped + (1 - p) * ln V` where `p = preserve/100` (p = 1 with Vowel Off).
6. `Y(k) = X(k) * E''(k) / E(k)` (phase of X kept, so pitch and timing are untouched). Gain `E''/E` is limited to +-30 dB per bin.
7. Frame energy match: scale `Y` so frame RMS equals `|X|` frame RMS, limited to +12 dB. `IFFT`, window, overlap-add.
Quality note: plain cepstral liftering is the simple end of envelope estimators; the "true envelope" iteration (DAFx paper "Efficient spectral envelope estimation and its application to pitch shifting and envelope preservation", dafx.de paper archive) is the documented upgrade path if listening tests show the envelope hugs harmonics; not required for v1.
Cost per frame per channel: 4 FFTs of 512 (about 4 x 2.3k butterflies). At 48 kHz that is 375 frames/s x 2 ch x 4 FFT, roughly 30 MFLOP/s: about 1% core. Frame hop is fixed in samples, so cost grows with sample rate; at 192 kHz use `N = 2048, H = 512` (same milliseconds) via a table keyed by `fs`.

### 3.2 Latency policy (acceptance-critical)
The streaming STFT delays its output by `kLatency = N` samples (about 10.7 ms at 48 kHz, scaled table above). A fill voice cannot be rendered in advance of its start, and the plug-in must not change reported latency per fill. Policy: the formant stage lives inside the voice; the voice output is delayed by `kLatency` and the voice's `lengthSamples` is reduced by `kLatency` so the fill still ends where the scheduler planned (never overlaps the next protected region). The scheduler subtracts `kLatency` from the available length when evaluating `minMs` for any voice with `formant.amount > 0` (fills shorter than `minMs + kLatency` skip formant processing and play unprocessed, counted in telemetry `formantSkipped`). The voice primes the processor with its first `N` input samples (as `SpectralSmear` callers do). No change to reported plug-in latency or PDC. Owner decision recorded in section 10.

### 3.3 Resource pool
Two `FormantProcessor` instances in `FillEngine` (like the single `SpectralSmear`, claimed per voice with `claim()/release()`); a third concurrent request plays unprocessed. All buffers preallocated in `prepare` for the actual sample rate. `FillVoice::start` copies `FormantSetup` and the stage runs after `applyModifiers` and band isolation, before the voice fade-out envelope.

## 4 Parameters and state
Group `FL20`, `tools/param_ids/fl20.lock`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fl20_amount` | float 0..100 | 0 | % (0 = off; dry/processed blend inside the voice) |
| `fl20_shift` | float -12..+12, step 0.01 | 0 | semitones |
| `fl20_tilt` | float -6..+6, step 0.1 | 0 | dB/octave |
| `fl20_vowel` | choice Off, A, E, I, O, U | Off | |
| `fl20_preserve` | float 0..100 | 100 | % (original envelope kept; irrelevant when Vowel Off) |
| `fl20_detail` | choice Low, Medium, High | Medium | |
No subtree. Neutral proof: `amount == 0` => `FormantSetup.enabled = false`; the engine never claims a processor and the voice path is unchanged (golden hash + `no_claim_when_off`). Applies to fills chosen by any algorithm, including per-band voices. Not randomised.

## 5 Real-time safety
FFT plan objects, window tables, cepstral and spectral scratch (about 6 x N floats per channel) allocated in `prepare`; no plan creation at runtime. Fixed per-frame work, no allocation, no locks. `log` and `exp` per bin guarded with floors (no NaN). Whole-frame NaN/Inf check: on failure output zeros for that frame, `reset()` the processor, increment fault counter. Sample-rate change re-prepares and releases claims.

## 6 UI
Slot `CreativeExtra` (spectral group, collapsed until Amount > 0): card "Formant" with Amount, Shift, Tilt, Vowel selector (segmented 5 letters plus Off), Preserve, Detail. Tooltips: Shift "Moves the resonant character of the sound without changing its pitch." Vowel "Adds vowel-like resonances. This colours the sound; it does not make words." Help section order 1200 "Formant" (including the 10 ms onset delay of processed fills). Accessible names include units ("Formant shift, plus 3 semitones"). Keyboard standard. Reduced motion: a static envelope diagram, no animation. Narrow: two rows.

## 7 MIDI / automation / CLAP / host
Automatable, learnable; values are sampled at fill start (documented: changes affect the next fill, like other creative settings). No reported-latency change. Offline rendering deterministic. All formats unchanged.

## 8 Tests (`Tests/features/TestFormant.cpp`)
`amount0_no_claim_and_hash_identical` | `pitch_preserved_within_3_cents` | `centroid_follows_shift_direction` | `identity_settings_near_transparent` (shift 0, tilt 0, vowel off, preserve 100: spectral difference below -40 dB) | `click_offset_equals_klatency` | `pre_echo_below_threshold` | `level_within_1p5db_pink_noise` | `vowel_peaks_near_tabulated_formants` (+-10% on F1/F2) | `channel_coherence` (stereo image preserved: inter-channel phase unchanged) | `extreme_settings_bounded_no_nan` | `silence_and_dc_stable` | `short_gap_skips_formant_and_counts` | `pool_exhaustion_plays_unprocessed` | `sample_rate_table_44k1_to_192k` | `panic_fades_within_budget` | `state_roundtrip_old_session` | `cpu_two_instances_budget` | `no_audio_thread_allocation`.

## 9 Dependencies and order
F0-1, F0-3 (`VoiceSetup` plumbing), F0-6; `10` order says spectral FX only after rhythmic FX are profiled. Independent of F-L14..F-L19. F-X02 may reuse `FormantProcessor`.

## 10 Effort and risks
M-L. Decision for the owner: accept a 10.7 ms onset delay (scaled by fs table) for formant-processed fills, versus restricting formant to an offline-prepared capture (needs worker thread, more complex). Risks: cepstral envelope on inharmonic or noisy material can sound unstable (mitigated by Detail and Preserve); CPU at high sample rates; transient smearing (30 ms window); `fl20` interplay with Spectral algorithm (both use FFT, different instances).

## 11 Acceptance checklist
- [ ] Off path hash identical, no claim
- [ ] Pitch, level, latency, pre-echo tests pass at four sample rates
- [ ] Onset-delay policy implemented and visible in Help
- [ ] No runtime allocation; pool exhaustion handled
- [ ] CPU within budget
- [ ] UI reviewed (min size, keyboard, units)
- [ ] IDs locked; legacy sessions load unchanged
