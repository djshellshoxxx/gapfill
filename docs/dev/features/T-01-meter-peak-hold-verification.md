# T-01 Level meter peak-hold and fall verification (PROGRESS "Next up" item 3)

Status: specified (this pass). Size S. The meter's gradient, 1.5 s peak hold and 20 dB/s fall are "verified only by construction"; `theme.md` requires them.

## 1. Summary and user-visible behaviour
No visible change. Extract the peak-hold arithmetic out of `LevelMeter` (`Source/gui/WaveformGapView.h`, which reads `juce::Time::getMillisecondCounter()` directly) into a pure, clock-injected helper so hold time, fall rate, the dB mapping and the gradient colour at known levels are unit-tested.

## 2. Success / quality metric
Hold lasts 1500 ms +- 1 timer tick; fall is 20 dB/s +- 5 % after the hold; `dB -> normalised` map is monotonic and hits the documented end points; colour at -60/-18/-6/0 dBFS equals the theme gradient stops; zero change in rendered pixels at a fixed level (golden screenshot of one meter with tolerance).

## 3. Design
New `Source/gui/MeterMath.h` (header-only, pure): `struct PeakHold { float peakDb; std::uint32_t holdUntilMs; void push (float db, std::uint32_t nowMs, std::uint32_t holdMs) noexcept; bool tick (std::uint32_t nowMs, float dtSeconds, float fallDbPerSec) noexcept; }`, `float normalised (float db) noexcept`, `float toDb (float lin) noexcept`, `juce::Colour gradientAt (float norm)`. `LevelMeter` delegates; clock via a `std::function<std::uint32_t()>`-free static setter `LevelMeter::setClockForTests (std::uint32_t (*)())` (message-thread code, std::function acceptable but a plain function pointer keeps it trivial). Behaviour must be identical (characterisation test first, then extraction, per `AGENTS.md` rule 5).

## 4. Parameters and state
None.

## 5. Real-time safety
UI only. The meter value arrives from `MeterState` atomics; no audio-thread change.

## 6. UI
No visual change; Reduced Motion keeps live meters active (communicates signal, per PROGRESS).

## 7. MIDI / automation / CLAP / host
None.

## 8. Tests (`Tests/features/TestMeter.cpp`, group `Gui`)
`peak_holds_1500ms` | `peak_does_not_fall_during_hold` | `peak_falls_20_dB_per_second` | `new_higher_peak_resets_hold` | `lower_level_does_not_reset_hold` | `db_map_monotonic_and_endpoints` | `gradient_stops_exact` | `readout_hidden_below_26px` | `meter_pixels_unchanged_golden` | `silence_shows_minus_inf_floor`.

## 9. Dependencies and build order
None. Do before the Phase 5 UI window so later UI work has a guard.

## 10. Effort and risks
~1 day. Risk: pixel golden flakiness across platforms; use a tolerance and per-platform image.

## 11. Acceptance checklist
- [ ] Characterisation tests pass on old code, then on extracted code
- [ ] Items in 8 all green on Linux and Windows
- [ ] PROGRESS "Next up" item 3 closed with a link to the test names
