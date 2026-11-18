# Harmonic pitch step

`Yin` is a pure synchronous pitch estimator. The scheduler adapter reads a
bounded history window, averages it to approximately12kHz, then analyzes1024
samples with at most400 lags. It owns no pointers beyond the call, allocates no
memory, performs no IO and does not change latency or share an FFT plan.
`HarmonicGrains` consumes the result only after voiced-confidence and stationarity
gates; unpitched material retains the existing ordinary Granular fallback.

The optimized difference-function loop computes four independent lag sums in
parallel. Each lag retains the released sample-addition order; cumulative-mean
normalization still visits lags in increasing order. SIMD can operate across
lags without a horizontal/reassociated reduction. There is no model change.

The scalar-release compatibility test uses16-bit binary fractions and four
sample rates, including the367-lag tail at44.1kHz. Every difference sum fits exact
double precision; the expected public-output trace is captured from e89aecd.
An independent native oracle also compared8000 tone/noise/decay/DC/non-finite
workloads against separately compiled release source with identical f0,
confidence and voiced values.

The pitch decision, including history reads, must meet0.3ms under the reference
workload. Strict assertions retain that limit; a shared/cloud diagnostic is not
reference-machine certification. See `docs/progress/CONTEXT-BUDGETS-20261031.md`.
