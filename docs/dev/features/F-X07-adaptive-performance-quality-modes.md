# F-X07 Adaptive performance and quality modes

Status: specified (this pass), not implemented. Size L. Depends on F0-2 (stages), F0-5, F0-7 (`Tests/Perf.cpp`, AllocGuard), Gate B. Also delivers the "CPU/latency budget mode" candidate in `11-GUI-PLACEMENT-AND-FEATURE-PLAN.md`; the CPU readout itself is F-S01.

## 1. Summary and user-visible behaviour
A **Quality** setting trades CPU for richness in three fixed, documented tiers, plus an **Auto** choice that steps between them while playing:
* **High** (default): exactly today's behaviour.
* **Balanced**: fewer simultaneous voices and grains, at most two modifiers per fill, same algorithms.
* **Light**: one voice, 8 grains, one modifier, and the Spectral/freeze algorithms are not chosen (a fill that would have been Spectral becomes Granular). For weak stage laptops.
* **Auto**: starts from a tier suggested by a short one-time benchmark, steps down when GapFill's own processing load stays high, steps back up after a long calm period. Changes take effect only at the start of a new fill, are rate-limited, and are shown in the footer ("Quality: Balanced (auto)").
Dry audio, detection, protection, latency and bypass are never throttled. Offline bounce always renders at the manual tier (Auto renders as High), so renders are reproducible.

## 2. Success / quality metric
| # | Metric | Target |
|---|---|---|
| M1 | Worst-case CPU (scenario E, complexity 100, chaos 100, spectral) vs High: Balanced / Light | <= 85 % / <= 65 % |
| M2 | High is bit-identical to the pre-feature build | goldens A-F |
| M3 | Governor on synthetic load traces: sustained 80 % load -> step down | within 1.5-2.5 s |
| M4 | Governor stability: noisy trace around the threshold, 5 minutes | <= 1 tier change per 10 s; no ping-pong |
| M5 | Recovery: load drops to 25 % | step up after 20-25 s, one tier at a time |
| M6 | Transition safety: output of already-running voices across a tier change | bit-identical to a run without the change; new tier affects only later fills |
| M7 | Governor overhead | <= 0.05 % CPU; 2 clock reads per block |
| M8 | Manual tiers deterministic | per-tier goldens identical on repeat and across block sizes |

## 3. Design
### Modules (`Source/features/quality/`)
`QualityTier.h` (enum + `constexpr QualityCaps` table), `QualityGovernor.h/.cpp` (pure state machine with injectable clock), `QualityBenchmark.h/.cpp` (background micro-benchmark), `Gather.cpp`, `Gui.*`, `Help.cpp`, `Info.cpp`, tests `Tests/features/TestQuality.cpp`.

### Tier table (single source of truth; feature owners add a field when they add a CPU-heavy mode)
```cpp
enum class Tier : std::uint8_t { High, Balanced, Light };
struct QualityCaps { std::uint8_t maxConcurrentVoices;   // min() with the complexity rule (1 or 2)
                     std::uint8_t maxGrains;             // genGranular active grain limit
                     std::uint8_t maxModifiers;
                     std::uint8_t harmonicVoices;        // F-X01
                     bool allowSpectral;                 // Algo::Spectral and F-X02
                     std::uint8_t vizDecim; };           // visualiser decimation, no audio effect
constexpr QualityCaps kCaps[3] = {
  /* High     */ { 2, 16, 3, 4, true , 2 },    // equals today's constants
  /* Balanced */ { 2, 12, 2, 2, true , 4 },
  /* Light    */ { 1,  8, 1, 1, false, 8 } };
```
Numbers are starting points; X07a measures them and fixes the table so M1 holds. High must equal the legacy constants; a static test compares them with the constants in `FillVoice`/`FillEngine` so they cannot drift.
Where caps apply: `trySchedule` (`maxConcurrent`, and a local copy of the weights with `Spectral = 0` when `!allowSpectral`, forced Spectral becomes Granular, then Reverse if too short, exactly like the existing start-failure fallback), `toVoiceSetup` (copies `maxGrains`, `maxModifiers`, `harmonicVoices` into `VoiceSetup::caps` so a running voice keeps the caps it started with), `FillEngine` visualiser decimation. Draw order is unchanged: `Spectral`'s weight becomes 0, so `chooseAlgorithm` draws the same number of random values.

### Governor (Auto only)
* Load per block: `load = (t1 - t0) / (numSamples / sr)` with `juce::Time::getHighResolutionTicks()` around the whole `processBlock` body (non-blocking on all platforms: QueryPerformanceCounter, `clock_gettime(CLOCK_MONOTONIC)`, `mach_absolute_time`). **Amendment required to `06` section 1**: add this call to the "allowed" list. It measures GapFill's own time, not the host or system load (state this in Help).
* State: `ema` with 1 s time constant, `hot` (decaying count of blocks with load > 0.9), `sinceChange`, `calmTime`.
* Step down one tier when (`ema > target` for >= 1.5 s, or `hot >= 3` within 1 s) and `sinceChange >= 5 s`. `target = fx07_target / 100` (default 0.70).
* Step up one tier when `ema < 0.5 * target` continuously for >= 20 s and `sinceChange >= 20 s`.
* At Light and still hot: set `overloaded` (shown in the footer, and consumed by F-L25's overload fallback); never throttles dry audio.
* Applies at the next fill start only (the tier is read in `trySchedule`); `TierEvent { tier, samplePos, reason }` goes to the message thread through `fillEvents` (reason: `Overload`, `Calm`, `Benchmark`).
* Offline: if `isNonRealtime()`, the effective tier is the manual tier and Auto maps to High; governor state is frozen.
* The clock is a function pointer (`std::uint64_t (*)()`), replaced in tests by a synthetic clock and a synthetic load trace.

### Benchmark (suggests the starting tier for Auto)
`QualityBenchmark` runs once per process on a low-priority `juce::Thread` started from the editor/first `prepareToPlay` (never inline, never in offline): 8 x 2048-point real FFT + a 16-grain loop over 4096 samples, 20 repeats, takes the median time and divides by a reference constant (fixed in X07a on the reference machine) to get `score` (1.0 = reference). Start tier: `score >= 0.8` High, `>= 0.4` Balanced, else Light. The result is cached in `std::atomic<float>` and in the global settings file (U-4) keyed by a coarse CPU description (`SystemStats::getCpuModel()`), expiring after 30 days. Only Auto reads it; manual tiers ignore it.

### Hooks (hot-file budget)
Processor: 2 lines around `processChunk`/`processBlock` timing + `governor.update (...)`; `gatherEngineSettings`: `s.perf.tier = governor.effectiveTier (...)` (one line in the F0 hook list); `FillEngine::trySchedule`: caps lookup; `FillVoice`: reads `setupData.caps` in `genGranular` and `configureAlgorithm` (modifier count).

## 4. Parameters and state
Group `fx07`, file `Source/params/fx07.h`, lock `tools/param_ids/fx07.lock`, appended to `GAPFILL_PARAMS_EXT`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fx07_quality` | choice: High, Balanced, Light, Auto | High | - |
| `fx07_target` | float 40-90 (Auto only) | 70 | % load |
* The current Auto tier and benchmark are runtime/global-settings data only, never in the project. `fx07_quality` is in `performanceParameterIDs()`; Random/Surprise/Undo exclude it. No subtree; `stateVersion` unchanged; old sessions get High.
* Neutral proof: High + code path `caps == legacy constants`; the governor is not constructed or fed when the setting is not Auto (timing calls are skipped, so even the clock reads are absent); goldens A-F bit-identical. A static_assert/test ties `kCaps[High]` to the legacy constants.

## 5. Real-time safety
Governor state is ~64 bytes of floats/ints; `update` is O(1). Two clock reads per block. No allocation, locks, or logging; the benchmark runs on its own thread and publishes through one `std::atomic<float>`. Tier is published through `std::atomic<std::uint8_t>`. Reset table: `reset()` zeroes `ema/hot/timers` and keeps the tier; `releaseResources` the same; sample-rate change: `target` is a ratio so nothing to rescale; bypass: governor paused; transport jump: no effect; offline: frozen.

## 6. UI (docs/dev/11 placement)
Options page (slot `OptionsExtra`): **Quality** combo (High, Balanced, Light, Auto) and **Auto target** knob (visible only for Auto). Footer status (`FooterStatus`) shows a message only when the effective tier is not High or when it changed in the last 30 s: "Quality: Balanced (auto, load 74 %)" and "Overloaded: reduce Complexity or choose Light" (highest priority below learn/export/preview). F-S01's CPU readout sits beside it. Tooltip text lists exactly what each tier removes. Accessible names; keyboard reachable; Reduced Motion: no flashing, message text only. Help section "Quality and CPU" order 1760, including the statement that Auto is a live safety net and bounces always use the manual tier.

## 7. MIDI / automation / CLAP / host
`fx07_quality` is automatable but stepped; changes apply at the next fill. No latency/PDC change in any tier (latency is Lookahead, unchanged). Hosts that render offline set `isNonRealtime()` which forces determinism. Some hosts report `isNonRealtime()` incorrectly during freeze/render-in-place: documented; E-7 checks it in REAPER and one more host.

## 8. Tests (`Tests/features/TestQuality.cpp`, group `Quality`)
`caps_high_equals_legacy_constants` | `caps_table_monotonic` | `light_never_selects_spectral_and_forced_spectral_falls_back` | `weights_copy_not_mutating_settings` | `draw_count_unchanged_when_spectral_excluded` | `governor_steps_down_on_sustained_load` (M3, synthetic clock) | `governor_ignores_single_spike` | `governor_hot_count_triggers_down` | `governor_no_pingpong_noisy_trace` (M4) | `governor_steps_up_after_calm` (M5) | `governor_rate_limit_5s_20s` | `governor_one_tier_per_step` | `tier_applies_to_next_fill_only` (M6) | `running_voice_keeps_started_caps` | `offline_forces_manual_tier` | `auto_maps_to_high_offline` | `benchmark_score_to_tier_table` | `benchmark_thread_stops_on_shutdown` | `manual_tier_goldens_T1_T2` (M8, Balanced/Light, added in a `[golden]` PR) | `perf_tiers_budget` (M1, in `Perf` group, reports numbers) | `no_clock_reads_when_not_auto` | `no_audio_thread_allocation` | `state_roundtrip_old_state_high` | `footer_message_priority`.

## 9. Dependencies and build order
Requires the F0 stage seams and `Tests/Perf.cpp`. Order: X07a measure and fix the caps table (needs F0 baselines) -> X07b governor + manual tiers (no benchmark) -> X07c Auto + benchmark + UI. F-X01/F-X02/F-X04 add their fields to `QualityCaps` in their own PRs. F-S01 (CPU display) is independent and should land first. F-L25 consumes `overloaded`.

## 10. Effort and risks
~7 ideal days (measure and table 1, governor 1.5, wiring 1, benchmark 1, UI 1, tests 1.5). Risks: (a) own-load measurement can mislead on busy hosts: wording is "GapFill load"; (b) users may not want any automatic change: Auto is opt-in and default is High; (c) tier goldens add maintenance: only two extra scenarios; (d) benchmark noise: median of 20, only used for the starting tier. Decision for the owner: approve the `06` amendment for `getHighResolutionTicks`. Stretch: per-feature caps for F-L20/F-L21 (convolution), adaptive FFT size for spectral FX (Light uses N = 1024 for the freezer), a user-visible "Safe for gigs" preset that pairs Light with F-L25's panic mapping.

## 11. Acceptance checklist
- [ ] High identical to baseline (goldens A-F, caps test)
- [ ] M1-M8 measured and recorded in `docs/progress/F-X07.md` (reference machine named)
- [ ] Governor tests use only the injected clock (no sleeps)
- [ ] Offline bounce deterministic with Auto selected
- [ ] `06` amendment merged or the clock call replaced by an approved alternative
- [ ] UI checklist (tooltips, names, keyboard, Reduced Motion, min size, Help)
- [ ] Unverified: low-end real machine (list model), host `isNonRealtime` behaviour
