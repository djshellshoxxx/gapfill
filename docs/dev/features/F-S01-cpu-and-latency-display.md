# F-S01 CPU and latency display

Status: specified (this pass). Size S. Depends on F0-9 (`FooterStatus` slot), optionally U-9 (status line priority). Shares `LoadMeter` with F-X07.

## 1. Summary and user-visible behaviour
A small readout answers two questions honestly: "how much delay does GapFill add?" (exact, reported to the host) and "how hard is GapFill working?" (measured, GapFill only). Shown in Options > Diagnostics and, while hovered/focused, in the footer: `Latency 40 ms (1920 samples)  |  GapFill CPU 3 % avg, 9 % peak of one core @ 512`. It never claims system-wide CPU. When bypassed or not prepared it shows "Idle".

## 2. Success / quality metric
Latency value equals `getLatencySamples()` and the Lookahead setting within 1 sample at 44.1-192 kHz; CPU average within +-1.5 percentage points of an external reference (a test that burns a known synthetic load) and peak within +-3 points; readout cost < 0.05 % CPU; zero audio-thread allocation.

## 3. Design
`Source/features/diagnostics/LoadMeter.h/.cpp` (shared with F-X07), `Gui.*`, `Help.cpp`, `Info.cpp`.
* **Measurement:** `t0 = juce::Time::getHighResolutionTicks()` at the top of `processBlock`, `t1` at the end; `load = ticksToSeconds (t1 - t0) / (numSamples / sampleRate)`. Needs the `06` section 1 amendment (allow `getHighResolutionTicks`, same as F-X07; non-blocking on all three OS). Accumulate per block: `avgEma` (time constant 2 s), `peak` (max over a 2 s window using two 1 s buckets), `blocks`, `lastBlockSize`. Published as three relaxed `std::atomic<float>` plus block size; UI reads at 4 Hz.
* **Labels:** latency is *exact* (reported); CPU is *measured* (wall time of GapFill's own processing relative to the block duration). Estimated figures are not shown. Tooltip: "Measured time GapFill spent in the audio callback. Other plugins and the host are not included."
* **Offline/stopped:** when `isNonRealtime()` the readout says "Offline render" and hides percentages (timing is meaningless).
* The measurement is disabled (no ticks read) unless the editor's diagnostics view or F-X07 Auto is active (`LoadMeter::setEnabled`), so the default has zero overhead.

## 4. Parameters and state
None (view only). `Perf` option `showDiagnostics` (bool, default off) in `<Perf>` state; absent = off. Neutral proof: disabled meter performs no clock reads; goldens unaffected (no audio change).

## 5. Real-time safety
Two clock reads and a few flops per block when enabled; atomics only; no allocation. Reset: `reset()` clears averages; sample-rate change recomputes block duration; bypass: skips.

## 6. UI
Options page group "Diagnostics" with a toggle and a two-line readout; footer text on hover/focus of the latency label. Text only (no meters). Accessible text "Latency 40 milliseconds, 1920 samples. GapFill CPU 3 percent average, 9 percent peak." Reduced Motion safe (updates 4 Hz, no animation). Help "CPU and latency" order 1600.

## 7. MIDI / automation / CLAP / host
None. Latency display agrees with the host's PDC (cross-check in E-7: Reaper PDC display).

## 8. Tests (`Tests/features/TestDiagnostics.cpp`)
`latency_matches_reported_all_rates` | `load_math_with_injected_clock` | `ema_and_peak_windows` | `disabled_reads_no_clock` | `offline_hides_cpu` | `bypass_idle` | `synthetic_load_within_tolerance` | `no_audio_thread_allocation` | `readout_text_format`.

## 9. Dependencies and build order
Independent; land first so F-X07 and F-L25 reuse `LoadMeter`. Amendment to `06` required.

## 10. Effort and risks
~1.5 days. Risk: users read it as total CPU; wording and tooltip prevent it. Block-averaged figures hide single-block spikes: the peak is per-block maximum.

## 11. Acceptance checklist
- [ ] Wording reviewed ("GapFill CPU", measured)
- [ ] 06 amendment merged
- [ ] UI checklist; Help text
