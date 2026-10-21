# F-L15 Dynamic dub feedback network

Status: specified, not approved. Size M. Category FX/MUS, value H.
Overlap: `10` section 5.6 gives purpose, controls list and test headings only. This file is the implementation-complete version and supersedes its numbers; do not create a second module. Plugs into the wet-stage seam owned by F-L14.

## 1 Summary and user-visible behaviour
A stereo ping-pong echo that only ever sees generated fill audio. Each repeat passes through a high-pass, a low-pass and a saturator inside the loop, so repeats get darker and dirtier. A slow bounded filter motion (LFO, or tempo-locked when transport is valid) makes the echoes evolve. Time is host-synced (note division) or free (ms). Mix 0 = off. Panic, bypass and scene changes clear the tail with a short fade. Tail is bounded (see 3.4) and reported to the host.

## 2 Success / quality metric
- Mix 0: bit-identical to baseline.
- Impulse response: repeat k peak equals `g^k` times the loop-filter gain within 0.5 dB (filters open, drive 0).
- Worst-case settings: output bounded `|y| <= input peak + 1.2` for 60 s of full-scale noise; never NaN.
- Tail: -60 dB reached within `t60Max = 6 s` for every (time, feedback) pair the UI can produce.
- CPU: below 1.5% of one core, 48 kHz stereo, 512 block (reference runner).

## 3 Design
Files: `Source/features/dub/DubStage.{h,cpp}` (implements `wetfx::WetStage`), `DubParams.h` (POD snapshot), `Help.cpp`, `Gui.{h,cpp}`.

### 3.1 Signal flow (per sample, two delay lines L and R)
```
in  = routed wet input (L, R)
tL  = readCubic (lineL, d(t));  tR = readCubic (lineR, d(t))        // d = smoothed delay in samples
loop(y) = sat ( LP ( HP ( y ) ) )                                    // HP/LP: 2nd-order Butterworth SVF, Q = 0.7071
PingPong: lineL <- inMono + g * loop(tR);  lineR <- g * loop(tL)    // inMono = 0.5 (inL + inR) enters L only
Stereo  : lineL <- inL + g * loop(tL);     lineR <- inR + g * loop(tR)
Mono    : both lines get inMono + g * loop(0.5 (tL + tR))
out     = mix * (tL, tR)                                             // wet-only echoes, no dry
```
Saturator: `sat(x) = tanh (k x) / k`, `k = 1 + 3 * drive01` (slope 1 at 0, magnitude below `1/k`). A one-pole DC blocker (5 Hz) follows it. Filters are passive (|H| <= 1) but their time-domain overshoot can exceed 1, so bounding relies on the saturator: recirculating magnitude `<= l1(filters) * g / k <= 1.2 * 0.92 / 1 = 1.1`, independent of input.

### 3.2 Delay time
Sync: `d = divisionInBeats (idx) * 60 / bpm` (reuse `Division`; default 1/8 dotted); free: `fl15_timeMs`. Clamp `20 ms..1500 ms`. Buffer: 2 x (1500 ms + 20 ms) at the prepared sample rate, 4-point Lagrange interpolation. Time changes glide with a one-pole (tau 120 ms) and a slew limit so the read-rate ratio stays in [0.5, 2]; this gives tape-style pitch bend on tempo change and avoids clicks. If tempo is invalid, sync mode falls back to `fl15_timeMs` and the status line says "free time".

### 3.3 Filter motion
`cutoff_eff = cutoff * 2^(motion * 1.5 * lfo)`, `lfo` a sine in [-1, 1], applied to the low-pass (and inverted, half depth, to the high-pass). Rate `fl15_motionRate`. With valid transport the phase is `2 pi * frac (ppq / (rateBeats))` where rate is rounded to the nearest of 1/2/4/8 bars (deterministic, seek-safe); otherwise a free-running phase reset on `reset()`. Cutoffs clamped to 20 Hz..0.45 fs; coefficient update every 32 samples.

### 3.4 Stability and tail bound
Effective feedback `g_eff = min (fl15_feedback, maxFeedback (d, t60Max = 6.0, 0.92))` using `wetfx::maxFeedback`, i.e. `g <= 10^(-3 d / 6)`: d = 125 ms gives 0.87, 250 ms 0.75, 500 ms 0.56, 1 s 0.32. The UI shows the effective cap on the feedback knob (C-0 style ring). `g_eff` is smoothed (20 ms). `tailSamples() = ceil (6 s * fs) + d`; the processor reports `jmax (2.5, 6.5)` s while the stage is active. Every value written to a line passes `sanitise (.., 4.0f)`.

### 3.5 Panic / clear
`clearTail (fade)`: ramp `g` and `mix` to 0 over `fade` (default 20 ms), then zero both lines and filter states. `reset()` zeros immediately.

## 4 Parameters and state
Group `FL15`, IDs through `makeID`, appended, locked in `tools/param_ids/fl15.lock`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fl15_mix` | float 0..100 | 0 | % (0 = off) |
| `fl15_timeSync` | bool | on | |
| `fl15_division` | choice, 12 `Division` values | 1/8 dotted | |
| `fl15_timeMs` | float 20..1500 | 375 | ms |
| `fl15_feedback` | float 0..92 | 45 | % |
| `fl15_lowCut` | float 20..800 log | 120 | Hz |
| `fl15_highCut` | float 800..16000 log | 5000 | Hz |
| `fl15_motion` | float 0..100 | 0 | % |
| `fl15_motionRate` | choice 1/2/4/8 bars, plus 0.1/0.25/0.5 Hz free | 4 bars | |
| `fl15_drive` | float 0..100 | 20 | % |
| `fl15_stereo` | choice Mono, Stereo, PingPong | PingPong | |
No subtree. Neutral proof: `fl15_mix = 0` makes `isActive()` false and the host never calls `process`; test compares wet-bus hash. Old sessions lack the IDs and load unchanged. Not randomised; excluded from Surprise and Undo-by-Surprise lists via `performanceParameterIDs()`.

## 5 Real-time safety
Lines allocated in `prepare` for the actual sample rate (max about 290 k floats x 2 at 192 kHz). Per-sample cost fixed; no division in the inner loop (reciprocals per block). Denormals: `ScopedNoDenormals` already set; add 1e-20 offset-free flush in filter state. Parameter reads once per block. No locks, no logging, no allocation after prepare. Sample-rate change: re-prepare.

## 6 UI
Slot `CreativeExtra`, foldable card "Dub Echo" (rhythmic/tape/delay group per `11`), body shown when Mix > 0 or the card is expanded. Controls in two rows: Time (sync toggle + division combo or ms knob), Feedback, Mix | Low Cut, High Cut, Motion, Rate, Drive, Stereo. Tooltips: Feedback "How much of each echo returns. Capped automatically so the tail dies within about 6 seconds." Mix "Level of the echoes. 0 turns the effect off." Help section order 1150 "Dub Echo" (what it processes, tail, free-time fallback). Accessible names: "Dub echo mix", "Dub echo feedback, effective cap 56 percent". Keyboard: standard knob keys. Reduced motion: Motion indicator is static text, no animated LFO dot. Narrow: two rows wrap to four.

## 7 MIDI / automation / CLAP / host
All params automatable and learnable. Tail length reporting changes while active (hosts may render extra tail on offline bounce, up to 6.5 s: tell users). Tempo changes during playback glide delay time. Offline render deterministic. CLAP/VST3/AU need no extra declarations.

## 8 Tests (`Tests/features/TestDub.cpp`)
`mix0_bit_identical` | `impulse_repeat_levels_match_g` | `pingpong_alternates_channels` | `stereo_and_mono_modes_route_correctly` | `feedback_cap_formula_table` | `t60_within_6s_all_grid` (time x feedback grid, measured by simulation) | `full_scale_noise_60s_bounded` | `max_feedback_automation_sweep_no_click` | `tempo_change_glide_no_click` (max sample delta) | `no_tempo_falls_back_to_free` | `motion_deterministic_given_ppq` | `seek_reproducible` | `panic_silent_within_25ms` | `sample_rate_change_clears` | `nan_input_recovers` | `denormal_tail_cpu_stable` | `state_roundtrip_old_session` | `cpu_worst_case`. Golden: impulse render at 48 kHz (committed WAV, tolerance 1e-5) and a drum-loop fill render.

## 9 Dependencies and order
Requires the F-L14 seam PR and F0-1/F0-6/F0-9. Independent of F-L19 and F-L21. Used by F-L25 (clear, shedding at quality 2).

## 10 Effort and risks
M. Risks: perceived tail longer than users expect (cap shown in UI); pitch glide on tempo change (documented, tape character); AU/host tail-length caching.

## 11 Acceptance checklist
- [ ] Mix 0 hash identical
- [ ] Impulse and bound tests pass at 44.1, 48, 96, 192 kHz
- [ ] T60 cap verified over full grid
- [ ] Panic/bypass/scene clear without discontinuity
- [ ] CPU within budget on the reference runner
- [ ] UI reviewed at min/default/large, keyboard, reduced motion
- [ ] IDs locked, state compatibility tests pass
- [ ] Windows, Linux, macOS runs recorded; DAW checks labelled unverified where not done
