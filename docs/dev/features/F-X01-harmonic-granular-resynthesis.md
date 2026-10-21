# F-X01 Harmonic granular resynthesis

Status: specified (this pass), not implemented. Size XL, split into X01a tracker, X01b grain engine, X01c UI. Depends on F0-1/2/3/5, Gate B. Soft deps: F-L18 / F-X03 (scale snap; feature works without them).

## 1. Summary and user-visible behaviour
A new **Harmonic** mode for the existing Granular fill. When the source around the gap is pitched (bass line, lead, vocal), the fill is rebuilt from **pitch-synchronous grains** cut from that material and re-pitched to a chosen interval set (octaves, fifths, triads, or notes of a known scale), so the gap is filled with a tonal cloud that sits in the song's key. When the source is not pitched (drums, noise, dense mix) the fill falls back to ordinary Granular and the status line says so. Off by default; Fill Type, Auto weights and every other algorithm are untouched.

Not in MVP: polyphonic pitch tracking, chord recognition, a separate "Harmonic" entry in Fill Type (see section 10 stretch).

## 2. Success / quality metric
| # | Metric | Target |
|---|---|---|
| M1 | Tracker accuracy on synthetic voiced fixtures 55-1000 Hz, SNR >= 20 dB | >= 95 % of voiced frames within +-20 cents; octave errors < 2 % |
| M2 | Unvoiced rejection (white/pink noise, kick, snare, hat loops) | >= 90 % of frames flagged unvoiced |
| M3 | Output pitch: steady 220 Hz tone source, interval set Major | spectral peaks at 220, 275, 330, 440 Hz within +-10 cents |
| M4 | Output periodicity (normalised autocorrelation peak at the target period) vs standard Granular on the same source | >= 0.8 vs <= 0.4 |
| M5 | CPU, worst case (2 voices of 4, scatter 100) | <= +3 % of the F0 `processBlock` baseline; tracker alone <= 0.5 % |
| M6 | Memory | <= 0.5 MB total (tracker 40 KB, epoch table 2 KB, grain state < 4 KB per voice) |

## 3. Design
### Modules (`Source/features/harmonic/`)
| File | Content |
|---|---|
| `PitchTracker.h/.cpp` | Incremental YIN on a decimated mono signal; ring of `PitchFrame` |
| `PitchMarks.h/.cpp` | Pure functions: epoch (pitch mark) finder on a captured buffer |
| `HarmonicGrains.h/.cpp` | `class HarmonicGrainEngine`: pitch-synchronous grain player owned by `FillVoice` |
| `IntervalSets.h` | `constexpr` ratio tables and scale snapping |
| `Gui.*`, `Help.cpp`, `Info.cpp` | UI card, help section, ParamInfo |
| `Tests/features/TestHarmonic.cpp`, `cmake/sources/harmonic.cmake`, `cmake/tests/harmonic.cmake` | |

### PitchTracker (audio thread, hop-grid driven so it is block-size invariant)
```cpp
struct PitchFrame { float f0Hz; float clarity /*1-d'(tau*)*/; std::int64_t histPos; };  // 16 B
class PitchTracker {
public:
    void prepare (double sampleRate);                 // derives N, allocates rings
    void reset() noexcept;
    void setActive (bool) noexcept;                   // false => push() returns immediately
    void push (const float* mono, int n, std::int64_t histPosOfFirst) noexcept;
    struct Query { float f0Hz; float voicedFraction; float clarity; bool voiced; };
    Query query (std::int64_t histPosFrom, std::int64_t histPosTo) const noexcept;  // median f0
};
```
1. **Front end.** `N = round(sr / 12000)` rounded to a power of two (44.1/48 kHz -> 4, 88.2/96 -> 8, 176.4/192 -> 16), so the analysis rate is 10.4-12 kHz. Fourth-order Butterworth low-pass (two RBJ biquads, `fc = 1000 Hz`, enough to keep f0 and low harmonics and suppress hats/air), then keep every N-th sample into `decim[1024]` ring. Pitch range 50-1000 Hz => `tauMax = ceil(analysisRate / 50)` = 240 at 12 kHz; `tauMin = analysisRate / 1000`.
2. **YIN per frame** (de Cheveigne and Kawahara 2002): window `W = 512` decimated samples (~43 ms). Difference `d(tau) = sum_{j<W} (x_j - x_{j+tau})^2`, cumulative-mean-normalised `d'(tau) = d(tau) / ((1/tau) * sum_{k<=tau} d(k))`, `d'(0) = 1`. Pick the first `tau >= tauMin` with `d'(tau) < 0.15` and walk down to the local minimum; if none, take the global minimum and mark unvoiced when `d' > 0.2`. Parabolic interpolation on `d'` around `tau*`. `clarity = 1 - d'(tau*)`. `f0 = analysisRate / tau*`.
3. **Bounded work.** A frame is computed in slices: every 32 input samples (the existing analysis hop) the state machine processes up to **16 lags** (16 x 512 = 8192 MACs, ~3 us). A frame needs `tauMax/16` ~ 15 hops (~10 ms at 48 kHz), then a new frame starts with the newest 512 decimated samples. The loop count is a compile-time constant, so per-hop cost is bounded and independent of block size.
4. **Storage.** `PitchFrame frames[256]` (~3.3 s). `histPos` is the **source-history** coordinate (the same coordinate as `VoiceSetup::sourceAnchor`), so Capture (history paused) stays consistent: the processor calls `push` only when it calls `history.write`.
5. **query()** returns the median f0 of voiced frames (clarity >= 0.8) inside the window, `voicedFraction = voiced / total`. Voiced for the fill requires `voicedFraction >= 0.6` and the median inside 50-1000 Hz.

### Fill-start analysis (`PitchMarks`)
At `FillVoice::start` (the capture is already copied, <= 0.32 s): given `T0 = sr / f0` find **epoch marks**: take the global maximum of `|x|` low-passed at 4*f0 (one biquad, in place on a scratch of the capture) within the last 2*T0 as `e0`; walk backwards `e_{k+1} = e_k - T0`, re-centre each on the local maximum within +-0.25*T0; stop at the buffer start. `epochs[512]` (ints, cap by length). Cost O(captureLength). If fewer than 4 epochs fit, treat as unvoiced.

### Grain engine (`HarmonicGrainEngine`, inside `FillVoice`)
```cpp
struct HarmonicSetup { bool enabled=false; std::uint8_t intervalSet=0, voices=2; float motion01=.3f, scatter01=.2f, tone=0, snap01=0;
                       float f0Hz=0; std::uint16_t scaleMask=0xFFF; std::int8_t rootPc=-1; };   // in VoiceSetup, neutral default
void configure (const HarmonicSetup&, const float* capL, const float* capR, int capLen, const int* epochs, int nEpochs, double sr, std::uint64_t seed);
void render (float& l, float& r) noexcept;   // one sample
```
* **Targets.** Interval table (ratios to f0): Octaves {1, 2, 0.5}; OpenFifths {1, 3/2, 2, 3}; Major {1, 5/4, 3/2, 2}; Minor {1, 6/5, 3/2, 2}; Suspended {1, 4/3, 3/2, 2}; Scale {semitone offsets of in-scale notes within one octave above the detected pitch class, requires `rootPc >= 0`, else behaves as OpenFifths}. `voices` picks the first V entries, ordering by seeded shuffle from `sub-RNG` so repeated fills vary.
* **Snap.** If `snap01 > 0` and `rootPc >= 0`: for each target `m = 12*log2(ratio) + midi(f0)`, nearest in-scale semitone `m'`; use `m + snap01 * (m' - m)` (cents-continuous, so 0 = just intonation, 1 = equal-tempered in key).
* **Per voice** (`PSOLA`): output epoch period `Tout = sr / (f0 * ratio)` (samples). A voice keeps `nextEpochOut`, `srcEpochIdx`, and `grains[6]` (each: start read pos float, age, length `2*T0`, gain, pan). At each output epoch, spawn a grain centred on source epoch `srcEpochIdx`, Hann window of length `2*T0` (two source periods), read at rate 1 (pitch change comes from the spacing, not resampling, so formants are preserved). Gain `1/ratio` compensates window overlap (`sum Hann = 2*T0/Tout / 2 = ratio`) and `1/sqrt(V)` across voices.
* **Motion.** `srcEpochIdx` follows a bounded path through the epoch table: `idx = round(c + motion * A * sin(2 pi phase))` with `A = nEpochs/2 - 1`, slow LFO period = fill length, `c` = newest epoch; plus a +-`round(3*scatter)` random epoch jitter and `Tout` jitter of +-`0.25*scatter` (sub-RNG). `motion = 0` repeats the newest period (a held tone).
* **Tone.** One-pole tilt shelf on the output: `tone > 0` brightens (`+6 dB` shelf at 4 kHz at +100), `< 0` darkens. Skipped (no float ops) at 0.
* **Stereo.** Source L/R grains are read separately (width preserved); voice pans fixed by index `{0, -0.3, +0.3, 0}` equal-power. Mono stays mono.
* Output passes the existing `sanitise` and the usual fill envelope / fades / duck, so the fill obeys protection exactly like Granular.

### Hooks in existing files (<= 8 lines total)
* `VoiceSetup`: `HarmonicSetup harmonic;` (defaults neutral).
* `FillVoice::configureAlgorithm()` case Granular: `if (setupData.harmonic.enabled) harmonic.configure (...)` after the legacy configuration (legacy RNG draws unchanged).
* `FillVoice::genGranular()`: first line `if (harmonic.active()) { harmonic.render (l, r); return; }`.
* Scheduler `toVoiceSetup` (F0-2): if `settings.perf.fx01.enabled && algo == Granular`, call `tracker.query(...)`, run pitch-marks decision, fill `HarmonicSetup`; unvoiced => leave `enabled=false` and set `FillRecipe::flags` bit3 `harmonicFellBack`; used => bit4 `harmonicUsed`. (Both bits are appended to the FillRecipe flag byte; undefined bits stay 0.)
* Processor: `tracker.push (monoMain, n, histPos)` immediately after `history.write` (one line, guarded by `setActive`).
* `EngineSettings::perf.fx01` (neutral struct) filled by `gather` in `Source/features/harmonic/Gather.cpp` (F0-5 pattern).

### Data flow
`mainBuffer -> mono -> tracker.push -> PitchFrame ring` ... later `trySchedule -> toVoiceSetup -> tracker.query + PitchMarks -> HarmonicSetup -> FillVoice.start -> HarmonicGrainEngine.render`. UI reads `std::atomic<float> lastF0, lastClarity` written by the tracker once per frame.

## 4. Parameters and state
Group `fx01`, file `Source/params/fx01.h`, lock `tools/param_ids/fx01.lock`, appended to `GAPFILL_PARAMS_EXT` (see 00-SPEC-GAP-REPORT conventions). All defaults are neutral because `fx01_mode = Off`.
| ID | Type / range | Default | Unit | Notes |
|---|---|---|---|---|
| `fx01_mode` | choice: Off, Harmonic | Off | - | |
| `fx01_intervals` | choice: Octaves, Open fifths, Major, Minor, Suspended, Scale | Open fifths | - | `Scale` needs a key (manual F-L18 or auto F-X03) |
| `fx01_voices` | int 1-4 | 2 | voices | |
| `fx01_motion` | float 0-100 | 30 | % | read-pointer travel |
| `fx01_scatter` | float 0-100 | 20 | % | time/epoch jitter |
| `fx01_tone` | float -100..100 | 0 | % | tilt |
| `fx01_snap` | float 0-100 | 0 | % | in-key snapping |
* None are in `creativeParameterIDs()`/Random sets except `fx01_motion`, `fx01_scatter`, `fx01_tone`, which are added to the Random-All list only when `fx01_mode != Off` at press time (Random never switches the mode on). `fx01_mode` is added to `performanceParameterIDs()`.
* State: parameters only, `stateVersion` unchanged. No subtree. Old sessions: IDs absent -> defaults -> Off.
* Neutral proof: with `fx01_mode = Off` the tracker is inactive (`push` returns first), `HarmonicSetup.enabled = false`, no extra RNG draw (sub-RNG is created only inside `configure`), golden scenarios A-F bit-identical, `memcmp` of `EngineSettings` identity test.

## 5. Real-time safety
All buffers allocated in `prepare` (max 192 kHz: decimated ring and YIN arrays are sized from the analysis rate, which is ~12 kHz at every sample rate); no allocation in `push`, `query`, `configure`, `render`. Bounded loops: 16 lags/hop; epoch walk <= 512 steps; <= 4 voices x 6 grains. Division guards: `T0 >= sr/1000`, `Tout >= 8` samples, ratio clamp [0.25, 4]. Denormals: `ScopedNoDenormals` already; feedback-free design. Reset table:
| Event | Tracker | Grain engine |
|---|---|---|
| `reset()` / `releaseResources` | clear rings | `active=false` |
| sample-rate change | `prepare` re-derives N and filters | n/a (voices re-`prepare`d) |
| bypass | `reset()` (history is not written while bypassed) | voices killed by existing `panic` |
| transport jump | none (signal-based) | existing `noteTransportJump` |
| Capture on/off | frames keep history coordinates | n/a |

## 6. UI (docs/dev/11 placement)
Foldable **Harmonic** card in the Creative tab's effects group (slot `CreativeExtra`, Advanced view), body shown only when `fx01_mode = Harmonic`. Header toggle "HARMONIC GRAINS" + readout "Source: A2 (110 Hz) - clear 92 %" or "Source: not pitched - using normal Granular". Controls: interval combo, voices stepper, motion/scatter/tone/snap knobs (snap disabled with tooltip "Set a key in Key settings" when no key). Tooltips from ParamInfo, e.g. `fx01_snap`: "How strongly harmonics are pulled onto your key's notes. 0 = pure ratios, 100 = in key." Accessible names "Harmonic grains on/off", "Harmonic interval set", etc. Tab order follows layout; 2 px focus ring; readout updates are text-only, no animation (Reduced Motion safe). Help section `Harmonic grains` registered order 1700.

## 7. MIDI / automation / CLAP / host
Mode and interval are automatable but stepped; host changes apply at the next fill start (never mid-voice), so no clicks. MIDI-learnable via the existing right-click menu. CLAP: parameters are exposed through the existing JUCE/CLAP wrapper; value-refresh after state load is the existing program-change report. No MIDI output. No latency change (analysis uses past material only). AU/VST3 identical.

## 8. Tests (`Tests/features/TestHarmonic.cpp`, group `Harmonic`)
`yin_sine_accuracy_55_to_1000Hz` (M1, 4 sample rates) | `yin_harmonic_complex_octave_errors` | `yin_vibrato_tracks_within_50cents` | `yin_rejects_noise_and_drums` (M2, fixtures from TestSignals: pink noise, kick, hat loop) | `yin_silence_unvoiced_no_nan` | `tracker_block_size_invariance` (frames memcmp-equal for blocks 32/64/480/4096) | `tracker_work_per_hop_bounded` (counter <= 8192 MACs) | `epochs_spacing_equals_period` | `epochs_too_short_falls_back` | `ratio_tables_valid_and_sorted` | `snap_pulls_to_scale` | `output_peaks_at_target_ratios` (M3) | `output_periodicity_beats_granular` (M4) | `mono_stays_mono` | `output_finite_bounded_extreme_params` | `no_click_start_end` | `fallback_sets_flag_and_plays_granular` | `off_is_bit_identical_golden_A_to_F` | `off_zero_rng_draws` | `no_audio_thread_allocation` (AllocGuard, tracker + fill) | `state_old_fixture_loads_off` | `sample_rate_change_rederives_decimation` | `capture_hold_keeps_history_coordinates` | `192k_32sample_block_ok` | `deterministic_same_seed_and_ppq`. Golden: new scenario `X1` (synthetic bass-line + drum gaps, Harmonic on, fixed seed) added in a `[golden]` PR for the feature; tolerances per F0-7. Fixture generation: bass line from additive synth with 6 partials at 1/n, notes A1-E2-G2 plus a vibrato variant.

## 9. Dependencies and build order
F0-1/2/3/5 and B3 stages first. X01a (tracker, pure, no hooks) -> X01b (grain engine behind `enabled=false`) -> X01c (params, scheduler hook, UI). F-L18 (manual key) and F-X03 optional; `Scale` set degrades to Open fifths until one exists. Preset pass (U-10) may add presets afterwards.

## 10. Effort and risks
MVP ~ 8 ideal days (tracker 2.5, grain engine 3, hooks/params/UI 1.5, tests 1). Risks: (a) real material is mostly polyphonic or percussive, so fallback will be frequent: mitigate with the honest readout and by documenting "best on bass, lead, vocal buses"; (b) PSOLA artefacts on noisy voices: bounded by clarity gate and cross-checking epoch regularity (reject if epoch spacing variance > 15 %); (c) YIN octave errors on bass with strong 2nd harmonic: threshold 0.15 plus lowest-tau-first rule; test M1. Stretch (separate tickets): first-class "Harmonic" Algo with its own Auto weight once the algorithm registry exists; low-band bass-only analysis mode; chord-follow from F-X03 key; polyphonic f0 via spectral peak picking.

## 11. Acceptance checklist
- [ ] Off: goldens A-F bit-identical, zero RNG draws, `EngineSettings` identity
- [ ] M1-M6 measured and recorded in `docs/progress/F-X01.md`
- [ ] Block-size invariance and determinism tests green on Linux and Windows
- [ ] AllocGuard test green; ASan/UBSan nightly clean
- [ ] Old-state fixtures load with Off; round trip of all `fx01_*`
- [ ] UI: tooltip, accessible names, keyboard path, Reduced Motion, minimum size, Help section
- [ ] Fallback is visible in the readout and in the History/Why status
- [ ] Unverified real-host items listed (REAPER + one more host on bass material)
