# F-L19 Frequency-shifted delays

Status: specified, not approved. Size M. Category FX/FUN, value M.
Overlap: `10` section 5.5 ("Frequency-shifted delay") lists purpose, controls and test headings. This file is the implementation-complete spec; it keeps those names and adds the algorithm, limits and tests. Plugs into the F-L14 wet-stage seam. Distinct from the existing PitchRise/PitchFall algorithms (those resample; this is single-sideband shifting, which does not preserve harmonic ratios).

## 1 Summary and user-visible behaviour
A stereo delay whose repeats are moved by a fixed number of Hz each pass. Small shifts (+-0.1..5 Hz) give phasing and slow barber-pole motion; large shifts (tens to 1000 Hz) give inharmonic, metallic or sci-fi repeats. Sign is direction (positive up, negative down). A Spread control shifts the left and right channels differently for width. Feedback makes the shift cumulative (n repeats = n x shift) until the loop damping removes it. Mix 0 = off. Applies to wet fill audio only.

## 2 Success / quality metric
- Mix 0: bit-identical to baseline.
- Accuracy: a 1 kHz sine shifted by +f produces a spectral peak at 1000 + f Hz within 0.05 Hz, with unwanted sideband at least 40 dB down for input 100 Hz..0.4 fs (scaled with fs), at 44.1/48/96/192 kHz.
- Aliasing: tone at 0.48 fs shifted +1 kHz leaves its folded image at least 25 dB below the input (guard filter).
- Feedback: bounded, `|y| <= input peak + 1.2` for 60 s of noise at maximum settings; -60 dB tail within 6 s.
- CPU below 2% of one core (48 kHz stereo, 512 block, reference runner).

## 3 Design
Files: `Source/features/freqdelay/FreqShifter.{h,cpp}` (Hilbert pair + oscillator, reusable), `FreqDelayStage.{h,cpp}` (implements `wetfx::WetStage`), `Gui.{h,cpp}`, `Help.cpp`.

### 3.1 Frequency shifter (single sideband)
Analytic signal from a polyphase IIR allpass pair (Niemitalo, "Hilbert transform IIR", yehar.com; same family as Csound `hilbert`). Each section in z^-2: `y[n] = a^2 (x[n] + y[n-2]) - x[n-2]`. Branch A coefficients a = 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737; branch B a = 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278; one branch additionally gets a one-sample delay. Which branch is delayed, and the sign convention, are fixed by test `hilbert_quadrature_and_direction`; if the measured quadrature error exceeds 1 degree between 0.01 fs and 0.45 fs or sideband rejection is below 40 dB, replace the coefficients by a re-optimised set (Remez/least-squares) rather than loosening the test. Usable band begins near 0.002 fs (about 96 Hz at 48 kHz); below it rejection degrades (documented).
```
(I, Q) = hilbertPair (x)             // I in-phase (delay-matched), Q quadrature
theta += 2 pi f / fs                 // double accumulator, wrapped to [0, 2 pi)
up:   y = I cos(theta) - Q sin(theta)     down (f < 0): same formula with signed f
```
Oscillator: complex phasor recurrence `z <- z * e^{j w}` in `double`, renormalised to |z| = 1 every 256 samples; `w` is smoothed (30 ms) so shift automation does not click. Shift range +-1000 Hz, resolution 0.01 Hz around zero (skewed range, centre 0).

### 3.2 Loop (per channel, delay lines L and R)
```
t   = readCubic (line, d)                     // delay d as in F-L15 (sync or ms, 20..1500 ms, 120 ms glide)
s   = guardLP8 ( shift (t, fShift_ch) )       // guard: 2 cascaded LR4 low-pass at fc = min (0.40 fs, 0.46 fs - |f|)
fb  = g * lowpass1 ( s, damping )             // one-pole damping, 1 kHz..20 kHz
line.write (inCh + dcBlock (fb))              // dcBlock: one-pole HP 10 Hz
out = mix * s
```
`fShift_L = f`, `fShift_R = f * (1 - 2 * spread/100)` (spread 0 same on both, 50 R unshifted, 100 mirrored). Ping-pong cross-feeding is not offered here (F-L15 covers it); an optional `crossFeed` is out of scope.
Stability: shifter magnitude ripple <= +-0.3 dB, so loop small-signal gain is `g * 1.04`. Limits: `g_eff = min (feedback, maxFeedback (d, 6.0, 0.90))` using `wetfx::maxFeedback` (same T60 rule as F-L15), which keeps `g_eff * 1.04 < 0.94`. A `tanh` soft clip (unity small-signal slope, ceiling 1.5) sits before the line write as the hard bound. All writes `sanitise (.., 4.0f)`.
`tailSamples() = 6 s * fs + d`.

### 3.3 Panic / clear
`clearTail (fade)` ramps `g` and `mix` to 0 over 20 ms then zeroes lines, Hilbert states and filters. Phasor is not reset (keeps phase continuity).

## 4 Parameters and state
Group `FL19`, `tools/param_ids/fl19.lock`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fl19_mix` | float 0..100 | 0 | % (0 = off) |
| `fl19_shiftHz` | float -1000..+1000, centre-skewed | 0 | Hz |
| `fl19_spread` | float 0..100 | 0 | % |
| `fl19_timeSync` | bool | on | |
| `fl19_division` | choice, 12 `Division` values | 1/8 | |
| `fl19_timeMs` | float 20..1500 | 250 | ms |
| `fl19_feedback` | float 0..90 | 40 | % |
| `fl19_damping` | float 1000..20000 log | 8000 | Hz |
No subtree. Neutral proof: `fl19_mix == 0` => `isActive()` false => stage not called (wet-bus hash test). Old sessions load unchanged. Not randomised (`performanceParameterIDs()`).

## 5 Real-time safety
Delay lines, Hilbert and guard states allocated in `prepare`. Per sample: 8 allpass sections (Hilbert pair) plus 4 SVF sections (guard) per channel, one phasor multiply, no trigonometric calls (recurrence), no division. Parameter reads per block; coefficient updates (guard fc, damping) every 32 samples when changed. Denormal flush on allpass states. NaN/Inf input zeroed; a non-finite internal state triggers `reset()` and increments a fault counter (F-L25). Worst-case CPU measured at maximum settings.

## 6 UI
Slot `CreativeExtra`, card "Shift Echo" (delay group, collapsed until Mix > 0). Controls: Shift (large knob, bipolar, shows signed Hz with unit), Spread, Time (sync/ms), Feedback, Damping, Mix. Tooltips: Shift "Moves every echo up (+) or down (-) by this many Hz each time it repeats. Small values phase; large values sound metallic." Feedback "Capped automatically so echoes die away within about 6 seconds." Help section order 1190 "Shift Echo" explains Hz vs semitones and the cumulative effect. Accessible names include sign and unit ("Shift, minus 12 point 5 hertz"). Keyboard: fine adjust with Shift modifier (0.01 Hz steps near 0). Reduced motion: static. Narrow: two-row wrap.

## 7 MIDI / automation / CLAP / host
Automatable; shift automation is smoothed. Tail length reported 6.5 s while active (same as F-L15; maximum of active stages). No latency. The 12-pole Hilbert pair adds no latency. Sample-rate change re-prepares. References for design: Niemitalo Hilbert IIR (yehar.com), JUCE forum thread "Hilbert Transforms" (forum.juce.com/t/hilbert-transforms/40531), Csound `hilbert` manual.

## 8 Tests (`Tests/features/TestFreqDelay.cpp`)
`hilbert_quadrature_and_direction` | `sideband_rejection_40db` | `shift_peak_frequency_accuracy` (positive/negative, 0.1 Hz..1 kHz) | `zero_shift_is_flat_magnitude_delay` (impulse repeats at d, magnitude within 0.5 dB to 0.4 fs) | `cumulative_shift_per_repeat` | `spread_values_map_channels` | `alias_image_25db_down` | `feedback_cap_table_and_t60_grid` | `noise_60s_bounded_max_settings` | `shift_automation_no_click` | `tempo_change_glide` | `mix0_bit_identical` | `panic_clears_within_25ms` | `sample_rates_44k1_to_192k` | `nan_recovery_counts_fault` | `state_roundtrip_old_session` | `cpu_worst_case`. Golden: 1 kHz sine and a drum hit at +37.5 Hz, -250 Hz (48 kHz, tolerance 1e-4).

## 9 Dependencies and order
F-L14 seam, F0-1/F0-6/F0-9. Shares `maxFeedback` with F-L15. `FreqShifter` is reusable by F-X02 (spectral freeze) and F-M15. Independent of F-L18 (Hz, not scale based).

## 10 Effort and risks
M. Risks: published coefficient values must be verified by the quadrature test before trust; low-frequency error below about 96 Hz; metallic output can be harsh (default shift is 0 and Mix is 0); negative large shifts fold content around DC by nature.

## 11 Acceptance checklist
- [ ] Quadrature and sideband tests pass at all four sample rates (coefficient set confirmed or replaced)
- [ ] Aliasing and feedback bounds proven by tests
- [ ] Mix 0 hash identical; panic/bypass clear
- [ ] CPU within budget
- [ ] UI reviewed at min/default/large, keyboard, screen-reader units
- [ ] IDs locked; legacy sessions load unchanged
- [ ] Cross-platform runs recorded; DAW checks labelled
