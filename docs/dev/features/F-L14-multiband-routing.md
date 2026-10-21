# F-L14 Multiband effects routing (and the shared wet-stage seam)

Status: specified, not approved (Gate C, `08`). Size M-L. Category VAL/FX, value H.
Overlap: `04` C-9 chooses the *algorithm* per band inside a voice; this ticket splits the finished *wet bus* and routes bands to wet effects. No duplication. `10` section 2 forbids a global serial chain; this design uses parallel sends only (see 3.2).
This ticket also owns the seam (`WetStage`, `WetFxHost`, `FeedbackBound`) that F-L15, F-L19 and F-L21 plug into. Ship the seam as its own small PR first (3.1).

## 1 Summary and user-visible behaviour
A "Bands" card in the Effects section. Off by default. When on, the generated wet signal (never the dry signal) is split into Low / Mid / High with a Linkwitz-Riley 4th-order crossover. Each band has a level (-48..+12 dB, bottom = mute) and a route: Direct, Dub (L15), Shift (L19), Reverb (L21). A band routed to an effect is also kept in the direct path; the effect's own Mix is the return level. Example: lows stay tight (Direct), mids get dub echoes, highs get frequency-shifted echoes. Routes to effects that are not built are hidden. Crossovers follow the Spectral-tab crossovers by default (Link on) or are set independently.

## 2 Success / quality metric
- Disabled: output bit-identical to the pre-feature build (golden hashes unchanged).
- Enabled, all levels 0 dB, all Direct: magnitude of (low+mid+high) flat within +-0.05 dB from 20 Hz to 0.45 fs at 44.1..192 kHz; no latency.
- Stop-band: each band at least 23 dB down one octave beyond its crossover (LR4: 24.1 dB).
- Route or level change: no discontinuity larger than -60 dBFS step in a 1 kHz sine test.
- CPU: below 1.5% of one core at 48 kHz stereo, 512 block, measured on the CI reference runner.

## 3 Design
### 3.1 Wet-stage seam (`Source/features/wetfx/`)
```cpp
namespace gapfill::wetfx {
enum class StageId { Dub = 0, FreqDelay, ConvReverb, Count };
struct StageContext {
    double sampleRate; int maxBlock;
    bool tempoValid; double bpm, ppqAtBlockStart;   // from EngineSettings, never read from the host here
    float duck;            // FillEngine::getCurrentDuck(); returns are multiplied by a 5 ms-smoothed copy
    bool nonRealtime;      // AudioProcessor::isNonRealtime()
    int  quality;          // 0 full, 1 reduced, 2 off (set by F-L25 only)
};
class WetStage {
public:
    virtual ~WetStage() = default;
    virtual void prepare (double sampleRate, int maxBlock) = 0;       // all allocation here
    virtual void reset() noexcept = 0;                                // hard clear, prepare/sample-rate change
    virtual void clearTail (int fadeSamples) noexcept = 0;            // soft clear (panic, bypass)
    virtual bool isActive() const noexcept = 0;                       // mix > 0 or tail still ringing
    virtual int  tailSamples() const noexcept = 0;                    // worst case at current settings
    /** outL/outR: wet-only return already scaled by the stage mix. n <= maxBlock. */
    virtual void process (const float* inL, const float* inR, float* outL, float* outR,
                          int n, const StageContext&) noexcept = 0;
};
class WetFxHost {                       // owned by the processor, plain member
public:
    void connect (juce::AudioProcessorValueTreeState&);               // caches fl14_* raw pointers
    void setStage (StageId, WetStage*) noexcept;                      // construction time only
    void prepare (double sr, int maxBlock);
    void process (float* wetL, float* wetR, int n, const StageContext&) noexcept;   // in place
    void panic (int fadeSamples) noexcept;                            // calls clearTail on every stage
    double maxTailSeconds() const noexcept;
};
/** Largest feedback g so that the loop decays by 60 dB within t60Max: g <= 10^(-3 d / t60Max), then min(absCeil). */
float maxFeedback (double delaySeconds, double t60MaxSeconds, float absCeil) noexcept;
}
```
Minimal hooks in existing files (one line each): `PluginProcessor.h` member `wetfx::WetFxHost wetFx;`; in `processChunk` one call after `engine.process (...)` and before the Low Protect block: `wetFx.process (wl, wr, numSamples, makeStageContext (engineSettings));`; each existing `engine.panic()` site also calls `wetFx.panic (fade)`; `getTailLengthSeconds()` returns `jmax (2.5, wetFx.maxTailSeconds() + 0.1)`. Placement before Low/Center/Width keeps the existing protections active on effect returns.

### 3.2 Routing math
Per block, per channel, with `bandIn[b]` from the crossover and `g[b]` the smoothed level:
`stageIn[s] = sum_b send[b][s] * bandIn[b]` where `send[b][s]` is 1 if route[b]==s else 0 (each send gain is a 10 ms linear ramp, so a route change cross-fades and the old stage's tail rings out).
`out = sum_b g[b] * bandIn[b] + sum_s ret[s]` with `ret[s] = duckSm * stage[s].process (stageIn[s])`.
Multiband off: `stageIn[s] = wet` for every enabled stage (parallel sends, no ordering, no recursion); `out = wet + sum ret[s]`. Stages with mix 0 are skipped entirely (not called).

### 3.3 Crossover (`Source/features/multiband/Crossover3.{h,cpp}`)
The existing `BandSplitter` is not flat when its three outputs are summed: low = LP1(x) is not allpass-compensated for the second split, so sum = LP1(x) + AP2(HP1(x)). Do not reuse it for audio output (leave it for detection). New topology per channel:
`lo1 = LP4_f1(x); hi1 = HP4_f1(x); low = AP4_f2(lo1) = LP4_f2(lo1) + HP4_f2(lo1); mid = LP4_f2(hi1); high = HP4_f2(hi1)`.
Each LP4/HP4 is two cascaded TPT SVF sections (use `SVF` from `DspUtils.h`, Q = 0.70711). Sum is `AP_f2(AP_f1(x))`: flat magnitude. Constraints: f1 in 30..0.45 fs, f2 >= 1.5 f1, f2 <= 0.9 Nyquist. Frequencies are smoothed in the log domain (tau 30 ms) and coefficients recomputed every 32 samples only when the change exceeds 0.1%. `sanitise (.., 32.0f)` on band outputs.

## 4 Parameters and state
Group `FL14` (`Source/params/fl14.{h,cpp}`, `tools/param_ids/fl14.lock`), appended to `GAPFILL_PARAMS`. Phase-0 follow-up: reserve empty groups FL14..FL25 in ticket order so parameter index order does not depend on merge order. The validator regex `^[a-z][A-Za-z0-9]*$` and `PRESET_PARAM_RE` must be relaxed to allow `_` (owner decision, one-line change in `tools/validate_repo.py`). IDs are created through the existing `makeID` helper.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fl14_enable` | bool | off | |
| `fl14_xoverLink` | bool | on | uses `xoverLM`/`xoverMH` |
| `fl14_xoverLM` | float 60..1000, log skew | 180 | Hz |
| `fl14_xoverMH` | float 900..12000, log skew | 3500 | Hz |
| `fl14_lowLevel`, `fl14_midLevel`, `fl14_highLevel` | float -48..+12, step 0.1 | 0 | dB (<= -48 = mute) |
| `fl14_lowRoute`, `fl14_midRoute`, `fl14_highRoute` | choice Direct, Dub, Shift, Reverb | Direct | |
State: no subtree. Added to `performanceParameterIDs()` (not randomised). Old sessions lack these IDs, so defaults apply; `fl14_enable` off skips all code. Neutral proof: with enable off `WetFxHost::process` returns before touching the buffers when no stage is active; test hashes the wet bus.

## 5 Real-time safety
All buffers (band scratch, stage input scratch, 2 x maxBlock floats per stage) allocated in `prepare`. Per-sample work is fixed: 8 SVF sections per channel plus 3 ramps. No locks, no logging. Parameters are read as `std::atomic<float>*` once per block. `process` handles n up to `preparedBlockSize` (the processor already sub-chunks). Sample-rate change calls `prepare` then `reset`. NaN/Inf: band outputs sanitised; a non-finite stage return is zeroed and the stage `reset()` (counter exposed for F-L25).

## 6 UI
Slot `CreativeExtra` ("Effects" section per `11`): foldable card "Bands", collapsed by default, content only after Enable. Layout: three columns (Low, Mid, High) each with level knob and route combo; a Link toggle and two crossover knobs shown when Link is off. Tooltips: "Bands: split the generated fill into low, mid and high so each can be turned up, down or sent to an effect. Does not touch your dry signal." Route tooltip: "Send this band to an effect as well as the direct output. The effect's own Mix sets how much returns." Help section order 1140 "Bands". Accessible names: "Low band level", "Mid band route", etc.; the card header is a toggle button announcing expanded/collapsed. Keyboard: Tab order low, mid, high, link; arrow keys adjust. Reduced motion: no fold animation. Narrow size: columns stack to a 3-row table below the minimum width; scrolling stays inside the content panel.

## 7 MIDI / automation / CLAP / host
All params automatable; MIDI-learnable through the existing manager (index-based, appended IDs are safe). CLAP/VST3 expose them automatically; AU parameter count grows (check host parameter limits in QA). Tail length reported rises only while an effect is active; hosts that cache tail length need a note in Help. No latency change. Not saved in the host project as anything but parameters.

## 8 Tests (`Tests/features/TestMultiband.cpp`, group `Multiband`)
`crossover_sum_flat_20hz_to_0p45fs` (44.1/48/96/192 kHz) | `existing_bandsplitter_sum_not_flat_characterised` (documents why it is not reused) | `stopband_23db_one_octave` | `disabled_is_bit_identical_golden` | `route_change_no_click` | `level_ramp_no_zipper` | `mute_floor_is_silence` | `link_follows_spectral_crossovers` | `crossover_sweep_extreme_no_nan` | `stage_mix0_not_called` | `duck_applied_to_returns` | `panic_clears_all_stage_tails` | `state_roundtrip_and_old_session_loads` | `param_ids_locked_and_unique` | `cpu_budget_report`. Edge cases: mono wet, 8 kHz sample rate floor, f2 clamp, block size 1 and 8192.

## 9 Dependencies and order
Needs F0-1, F0-6, F0-9 (slot host). Seam PR first, then this ticket; F-L15, F-L19, F-L21 each depend on the seam only (their route entry stays hidden until built). F-L25 uses `WetFxHost::panic` and `quality`.

## 10 Effort and risks
M for seam plus M for multiband (L overall). Risks: phase/level surprises if users expect `BandSplitter` behaviour (mitigated by test and Help); stage returns spilling over a protected hit (mitigated by duck on returns); parameter count growth in AU hosts.

## 11 Acceptance checklist
- [ ] Seam PR merged with no audible or hash change
- [ ] Crossover flat-sum and stop-band tests pass at all sample rates
- [ ] Disabled build hashes equal baseline
- [ ] Route/level changes click-free
- [ ] Panic and bypass clear stage tails
- [ ] IDs locked, state round-trip and legacy load pass
- [ ] UI checked at minimum, default and large size, keyboard-only, reduced motion
- [ ] Native Windows/Linux/macOS test runs recorded; unverified hosts labelled
