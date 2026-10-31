# Context performance seams —2026-10-31

Canonical limits are unchanged: F-X01 pitch decision<=0.3ms, F-X03 key front end
<=0.3% of a core and worst hop<=60us, F-X04 features/tracker<=0.2% of a core,
worst frame<=30us and tracker<=80KiB. Existing tests printed these targets but
allowed5ms,3000us,2000us and100KiB. The enforced assertions now use the actual
limits, with additional whole-history pitch-decision and dense64-tone key tests,
key total CPU and arrangement feature+tracker total CPU workloads. Input synthesis,
allocation, preparation and test assertions are outside the timed regions.

`GAPFILL_PERF_ENFORCE=1` selects the strict reference gate; ordinary shared CI
runs report diagnostics. This does not change the acceptance limits or establish
reference-machine compliance by itself.

## Behavior-preserving key work

The ChromaTracker optimization skips unused high-bin magnitude/prefix work and
avoids logarithms for mathematically impossible harmonic candidates. The exact
original logarithmic predicate still decides every possible match; accumulator
order and public outputs are preserved. No new model version is required.

A native oracle compiled the released e89aecd implementation independently under
a separate namespace and compared all public chroma/tuning/tonal-mass float bits,
gates, frame counts and completion counts after every chunk. Four sample rates
(44.1/48/96/192kHz), chunks1/64/512/4096, noise, harmonic and dense64-tone signals:
**2,321,256 comparisons passed exactly**. The bounded modules and changed tests
are syntax-checked against pinned JUCE8.0.15; full native integration remains with
the coordinator. No harmonic YIN algorithm or old enabled key behavior changes.

## Timing evidence and limits

A short dedicated component run used Release/O3 production sources and reused
pinned JUCE objects on this managed Xeon Platinum8573C environment after the
coordinator's suite completed. Across1000 interleaved iterations the pre-key-
optimization diagnostic maxima were YIN2123.5us, key517.1us and arrangement232.7us,
with similar process-CPU maxima. Total runner wall time was0.18s, indicating
outliers require distribution/cold-start investigation. These maxima do not
meet the strict limits; no budget pass is claimed or assertion relaxed.

This machine is not a defined physical reference platform. Quiet reference
measurement, per-format whole-plugin latency/CPU and host/device validation
remain required. The source optimization has exact-output evidence; its timing
improvement and the new strict native workloads still require the coordinator's
fresh candidate and a controlled performance run. Do not report strict budgets
as complete on the basis of ordinary diagnostic CI passes.

## Exact YIN follow-up

The pitch difference function now computes four independent lag sums in
parallel, preserving each lag's sample-addition order and the later sequential
running-normalization order. This permits SIMD across lags without a new model,
worker lag, changed history anchor, reassociated reduction or relaxed threshold.
A separately compiled scalar e89aecd oracle compares8000 workloads at all four
rates (tones, noisy tones, decays, random noise, DC, Inf and NaN): all public
f0/confidence float bits and voiced flags match exactly. A durable128-case
binary-input scalar trace test passes strict warnings and ASan+UBSan (leak
checking disabled for the ptrace environment).

Under concurrent compiler load, diagnostic YIN median dropped from171.4us to
102.4us. Those samples retain outliers above0.3ms (neither is an acceptance pass).
The controlled strict reference run and whole-plugin callback timing are still
required. The improvement and compatibility proof must not be presented as a
worst-case budget certification.
