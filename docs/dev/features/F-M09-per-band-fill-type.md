# F-M09 Per-band fill type (completion spec over C-9)

Status: canonical rules are `04-SPEC-CREATIVE.md` C-9 (forced algorithm per spectral lane, `NoAlgoFits`, tests). This file adds the phase-coherence analysis the backlog asks for, the UI/limits C-9 omits, and replaces the IDs.

## 1 Summary & user-visible behaviour
In Spectral detector mode each lane (Low / Mid / High) already picks gaps by its own confidence; this ticket lets the user choose which algorithm a lane uses (Auto or any of the 15), e.g. Stutter on Low, Reverse on Mid, Granular on High. Rules (C-9, restated only where it matters): applies after `chooseBand`; Fill Type set to a specific algorithm overrides band types; if the forced algorithm does not fit the gap (`lengthMs < kProfiles[algo].minMs * 0.6`) the gap is skipped with `NoAlgoFits`, no silent substitution to another algorithm for *length* reasons; non-Spectral detector modes ignore band types.
Findings and fixes beyond C-9:
1. **Phase coherence is not guaranteed by the current splitter.** `BandSplitter::processStereo` cascades `lowSplit` then `highSplit(rest)`, i.e. `low = LP1`, `mid = HP1*LP2`, `high = HP1*HP2`. The low band is not delayed by the mid/high all-pass `AP2 = LP2 + HP2`, so `low+mid+high = LP1 + HP1*AP2`, which is not flat. Analytic LR4 magnitude (computed offline for this spec, 4th-order Linkwitz-Riley = two cascaded Butterworth-2 per [musicdsp LR4](https://musicdsp.readthedocs.io/en/latest/Filters/266-4th-order-linkwitz-riley-filters.html); background on all-pass compensation: [JUCE forum thread](https://forum.juce.com/t/perfect-crossover-filters/36125)): crossovers 180/3500 Hz -> 0.0 dB deviation; 500/1500 -> -1.4 dB; 1000/1500, 2000/3000, 300/450 (ratio 1.5, the allowed minimum) -> -7.2 dB at the dip. Adding `AP2` to the low band restores 0.0 dB in all cases.
2. Why it matters: with Complexity > 0.65 two voices can run at once; if two band voices run algorithms that keep the source coherent (Reverse/Delay/Filter), their sum should be flat. Correct fix for the shared splitter changes the Spectral-mode golden, so it is NOT done silently: `CompensatedBandSplitter` (new, in this feature) is used only for voices started with a non-Auto band type; the legacy splitter and detection analysis are untouched. Retrofitting compensation to the legacy path is a separate `[golden]` PR (owner decision D1).
3. Concurrent voices and a single `SpectralSmear` instance: a forced `Spectral` algorithm on two lanes cannot run at once (existing `spectral.isClaimed()`); the second falls back to Granular via the existing start-failure path (same as a global forced type). The recipe records the algorithm actually used.
4. Short-gap reality: High-lane gaps are short, so forced `Spectral` (min 180 ms), `Freeze` (130) or `Granular` (110) will often yield `NoAlgoFits`. The UI shows the minimum gap each choice needs.

## 2 Success / quality metric
- Neutrality: all three Auto -> golden B (spectral) bit-identical; draw count identical to baseline.
- Targeting: over 500 seeded gaps (scenario B, all lanes on), when Low = Stutter the share of Stutter among Low-lane fills = 100%, other lanes unchanged vs baseline distribution (chi-square p > 0.05).
- Coherence: `CompensatedBandSplitter` low+mid+high magnitude within +/-0.1 dB of flat from 20 Hz to 0.45 fs for 20 crossover pairs including ratio 1.5; phase of sum equals single all-pass (group delay finite, no cancellation).
- Usability: with a deliberately short gap ( < algorithm minimum ) the status explains the cause in one line.

## 3 Design
- Files: `Source/features/bandtype/{BandType.h,BandType.cpp,CompensatedBandSplitter.h,CompensatedBandSplitter.cpp,Gui.cpp,Help.cpp}`, `Tests/features/TestBandType.cpp`, `Source/params/fm09.{h,cpp}`, `tools/param_ids/fm09.lock`, cmake fragments.
- Scheduler: `PerformanceSettings::bandType[3]` (F0-5, 0 = Auto else `Algo+1`) filled by `gather()`. In `buildRecipe` (F0-2): after `band >= 0`: `if (s.fillTypeIndex == 0 && s.mode == DetMode::Spectral && perf.bandType[band] > 0) algoIndex = perf.bandType[band]-1` and apply the existing forced-length check; else `chooseAlgorithm` as today. The forced path makes no `Rng` draw; no later draw exists in `trySchedule` (setup seed derives from the key), so later decisions are unchanged.
- `int minFitMs (Algo) noexcept` = `ceil (kProfiles[a].minMs * 0.6f)`; export from `FillEngine.h` as a free `constexpr` helper so the GUI never copies the table.
- Voice: `VoiceSetup::compensatePhase` (default false). `FillVoice::start` selects `CompensatedBandSplitter` when true; both share the `processStereo` signature. Compensation = additionally run the low band through the mid/high all-pass pair (`LP2+HP2`) so `sum = AP-only`; costs two extra `LinkwitzRileyFilter` per channel, allocated in `prepare()`.
- `FillRecipe::band` and `algo` already store the outcome (Lock, History, Timeline).
- Event data (F-M02): `NoAlgoFits` carries `neededMs = minFitMs(forced algo)`.

## 4 Parameters & state
Replaces unreleased `bandLowType/MidType/HighType` of the 02 registry (BANDTYPE group slot). Group file `Source/params/fm09.{h,cpp}`, lock `tools/param_ids/fm09.lock`, append-only, version hint 1:

| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fm09_lowType` | choice: Auto + 15 algorithms in `Algo` enum order (names from `algoName`) | Auto | n/a |
| `fm09_midType` | same | Auto | n/a |
| `fm09_highType` | same | Auto | n/a |

- Choice list is append-only: a future 16th algorithm is appended to both `Algo` and the choices. Not randomised (not in allowlists), not touched by Surprise. State: parameters only. Old sessions: absent -> Auto -> identical (golden B). Factory presets unchanged until the preset pass.
- Neutral proof: `auto_identical_to_baseline` + golden B; `compensatePhase` false for Auto.

## 5 Real-time safety
Per gap: three int reads and one compare. Per voice: unchanged cost for Auto; +2 LR filters/channel when a forced band type is used (about 8 biquad-equivalents per sample, bounded). Filters allocated in `FillVoice::prepare`, reset in `start()` (as `bandFilter`). Reset events: voice `start()`/`reset()`; crossover change updates both splitters through the same `setCrossovers` clamp (lm 30 Hz..0.45 nyq, mh >= 1.5 lm). No allocation/locks; output through `sanitise`.

## 6 UI
- Placement: inside each lane row of `SpectralPanel` (`Panels.cpp` `Lane` struct gains `std::unique_ptr<GapCombo> type`; this is the single allowed edit of that file, per C-9). Hidden in Simple view (F-M04), visible in Advanced.
- Combo name "Low lane fill type" / "Mid ..." / "High ...". Items: "Auto", then algorithm names with the minimum gap suffix in the open list only ("Granular (needs 66 ms)"). Tooltip: "Which fill this lane uses. Auto lets GapFill choose. Needs Spectral detector mode; a specific Fill Type in the Creative tab overrides this."
- Disabled (not hidden) with greyed text when detector mode is not Spectral or global Fill Type is not Auto; the disabled tooltip states the reason.
- Help (order 1800): explains skip behaviour ("If the lane's gap is too short for the chosen fill, GapFill skips it instead of switching") and the coherence note in plain words.
- Accessibility: names above; list items readable; keyboard under the preference; Reduced Motion n/a. Narrow: combos drop the lane label text, width >= 96 px; at < 1100 px the three combos stack below the lane knobs.

## 7 MIDI / automation / CLAP / host
Automatable stepped parameters (16 steps), MIDI-learnable. Changes apply from the next gap. CLAP/VST3 expose names. No latency impact. Offline render deterministic.

## 8 Tests (`Tests/features/TestBandType.cpp`)
C-9 list plus: `splitter_sum_flat_legacy_documents_dips` (records -1.4/-7.2 dB values as expectations so a later fix shows up) | `compensated_sum_flat_20_pairs` | `compensated_energy_preserved` | `forced_band_type_only_that_band` | `fill_type_forced_overrides` | `forced_too_short_reports_NoAlgoFits_with_minfit` | `spectral_claimed_falls_back_to_granular_and_recipe_records_it` | `rng_stream_unchanged_after_forced_choice` | `non_spectral_mode_ignores_bandtype` | `lane_off_ignores_its_type` | `state_roundtrip_old_state_auto` | `minfit_table_matches_profiles` | `no_audio_thread_allocation` | `block_size_invariance_with_forced_types`. Fixtures: scenario B, synthetic 3-tone (100 Hz, 1 kHz, 8 kHz) for coherence, sweep for splitter magnitude.

## 9 Dependencies & build order
F0-2 (stages), F0-5 (`bandType[]`), F0-1, F-M02 (reason detail). Independent of other F-M tickets; F-L14 (multiband routing) will reuse `CompensatedBandSplitter`. Build after F-M02.

## 10 Effort & risks
Effort M. Risks: owner may want the legacy splitter fixed (golden change); forced short-gap skips look like "it stopped working" (mitigated by min-fit text + F-M02); 16-item combos in narrow lanes; two voices forcing `Spectral`.
Owner decision D1: retrofit compensation to the legacy `BandSplitter` in a separate [golden] PR or keep legacy as is.

## 11 Acceptance checklist
- [ ] All Auto = golden B unchanged; RNG stream unchanged
- [ ] Forced types honoured per lane only; global Fill Type overrides
- [ ] Compensated splitter flat within +/-0.1 dB; legacy values recorded
- [ ] `NoAlgoFits` message includes needed ms; min-fit shown in the list
- [ ] IDs `fm09_*` locked; old states load as Auto
- [ ] Combos usable at min size, disabled states explained, keyboard/screen-reader checked
