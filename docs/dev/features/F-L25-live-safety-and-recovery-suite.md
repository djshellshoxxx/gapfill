# F-L25 Live safety and recovery suite

Status: specified (this pass). Size L. Depends on F-S12 (Panic), F-L07 (last-good scene), F-X07 (`overloaded` flag, optional), Gate B. Consolidates the safety items in `10` section 4.3 and `11`.

## 1. Summary and user-visible behaviour
Safety nets that act without the user having to notice a problem, all visible, all reversible, all neutral when nothing is wrong:
* **Wet guard**: if the wet bus ever goes non-finite or sustains a level above +12 dBFS, GapFill fades the wet path out and reports "Wet guard: level fault" (self-healing resume after 2 s of normal level).
* **Wet ceiling** (optional): a transparent soft ceiling on the wet bus only, 0 / +3 dBFS.
* **Overload fallback**: when F-X07 reports `overloaded` at the lowest tier for more than 3 s, wet level is reduced 6 dB and the user is told how to fix it.
* **Last good scene**: one action restores the last scene that was fully applied (never an unvalidated pending one).
* **Safe start**: option to open every new instance in Safe (wet muted) so a project loaded on stage cannot make noise before the performer is ready.
Dry audio is never limited or altered by any of these.

## 2. Success / quality metric
| # | Metric | Target |
|---|---|---|
| M1 | Injected NaN/Inf into the wet bus (test hook) | wet faded in <= 10 ms, output finite, dry unchanged |
| M2 | Sustained +15 dBFS wet signal (test hook) | guard fires within 100-150 ms, wet <= -80 dBFS after 30 ms ramp |
| M3 | No false triggers: scenario E (complexity/chaos 100) and all factory presets | 0 guard events |
| M4 | Ceiling at 0 dBFS: wet bus peak | <= 0 dBFS + 0.1 dB; ceiling Off bit-identical |
| M5 | Safe start | wet silent until Resume, dry normal, in 100 % of load paths (state, preset, DAW restore) |
| M6 | Last-good recall never applies invalid data | corrupted pending scene -> recall previous good, message shown |

## 3. Design
`Source/features/safety/{WetGuard,WetCeiling,SafeStart}.{h,cpp}` plus glue in `Panic.cpp`.
* **WetGuard** (audio thread, per chunk on the final wet bus before the mix): track `nonFinite` (any sample failing `std::isfinite` before `sanitise`; the existing sanitiser hides these today, so count its hits through a thin counter wrapper in the safety module, not by editing generators) and `peak`. Rules: any non-finite -> immediate `panicGain` ramp (10 ms) + event `WetFault(NonFinite)`; `peak > 4.0` (+12 dBFS) continuously for 100 ms -> same with `WetFault(Level)`. Auto-resume after 2 s with `peak < 1.4` (+3 dBFS), at most 3 resumes per minute, then stays Safe with "Wet guard latched - Resume".
* **WetCeiling**: when `fl25_ceiling != Off`, wet bus passes `softCeiling (x, c) = c * tanh (x / c)` only for `|x| > 0.5 c` (linear below, C1-continuous knee: `y = x` for `|x| <= 0.5c`, else `sign(x) * (0.5c + 0.5c * tanh ((|x| - 0.5c) / (0.5c)))`). Stateless, zero latency, deterministic.
* **Overload fallback**: `if (quality.overloaded && lowestTier) wetTrim -> -6 dB over 100 ms` until `overloaded` clears for 10 s; event `Overload`.
* **Last good scene**: F-L07 maintains `lastGoodScene` (index + payload hash) only after the message thread confirms APVTS caught up; action `RecoverScene` (never quantised) re-applies it through the immediate path.
* **Safe start**: if `fl25_safeStart`, `prepare` sets `state = Safe` and `panicGain = 0` before the first block; resume as F-S12.
* Events go to the footer status (priority just below Panic) and the debug overlay counters.

## 4. Parameters and state
Group `fl25`, file `Source/params/fl25.h`, lock `tools/param_ids/fl25.lock`, appended to `GAPFILL_PARAMS_EXT`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fl25_guard` | bool | on | - |
| `fl25_ceiling` | choice: Off, 0 dBFS, +3 dBFS | Off | - |
| `fl25_safeStart` | bool | off | - |
* `fl25_guard` on by default is neutral because its thresholds are outside every legal output (M3 proves it on scenario E and all presets). Counters are runtime only. `fl25_safeStart` is stored in the project (the performer wants it to travel with the project).
* All three in `performanceParameterIDs()`. `stateVersion` unchanged; old sessions get defaults.
* Neutral proof: goldens A-F bit-identical with defaults (guard never fires; branch is a compare).

## 5. Real-time safety
One compare/accumulate per sample on the wet bus (two flops); no allocation; counters atomic. Reset table: `reset()` clears timers and keeps Safe if latched; `releaseResources` -> Live unless safeStart; sample-rate change recomputes the 100 ms/2 s windows; bypass: guard paused; transport jump: none.

## 6. UI
Options page group "Safety": Wet guard toggle, Ceiling combo, Start in Safe toggle. Footer status lines: "Wet guard: level fault - wet muted. Resume", "Overloaded - wet reduced 6 dB. Choose Light quality". Recover-scene button beside PANIC in the Performance tab (text label "LAST GOOD SCENE"). Keyboard reachable; accessible names; no colour-only cues; Reduced Motion safe. Help section "Safety" order 1510.

## 7. MIDI / automation / CLAP / host
Recover-scene and Resume are MIDI-learnable actions. The three parameters are automatable but marked non-automatable for `fl25_safeStart`. No bus/latency changes.

## 8. Tests (`Tests/features/TestSafety.cpp`, group `Safety`)
`guard_nonfinite_fades_wet` (M1) | `guard_sustained_level_triggers` (M2) | `guard_not_triggered_by_scenario_E_or_any_preset` (M3) | `guard_resume_rate_limited` | `ceiling_bounds_peak_and_off_is_identity` (M4) | `ceiling_knee_continuous` | `overload_fallback_trims_6dB_and_recovers` | `safe_start_silent_until_resume_all_load_paths` (M5) | `recover_scene_ignores_corrupt_pending` (M6) | `dry_never_touched` | `events_reach_status_in_priority_order` | `no_audio_thread_allocation` | `old_state_defaults`.

## 9. Dependencies and build order
F-S12 first. F-L07 for last-good scene; F-X07 optional (overload input stubbed false until it lands). Module registry for tails from F-S12.

## 10. Effort and risks
~4 ideal days. Risks: guard thresholds too tight for exotic setups (they are above every legal output; user can disable); soft ceiling colouring the sound (Off by default). Owner decision: default for `fl25_safeStart` (spec: off) and whether `fl25_guard` should default on (spec: on).

## 11. Acceptance checklist
- [ ] M1-M6 recorded in `docs/progress/F-L25.md`
- [ ] Zero guard events across all presets in CI
- [ ] Messages are actionable (say what to do)
- [ ] Safe start verified with a DAW project restore (unverified items listed if not run)
