# Key detection front end

`ChromaTracker` owns the prepared4096-point real FFT, decimator/filter histories,
peak scratch and leaky chroma state. One audio-thread owner calls `process`;
`prepare` allocates, `reset` clears bounded storage, neither persistence nor GUI
objects enter this algorithm. `AutoKey`/`KeyEstimator` retain their existing
confidence, hold and resolver policies. Off-path scheduling stays in `ContextState`.

The2026-10-31 optimization is behavior preserving. Bins above the highest local
peak-mean access are never observed, so magnitude and prefix loops stop there.
Harmonic candidates outside0.97..1.03 of an integer ratio cannot satisfy the
original+-0.45-semitone predicate; the original logarithm remains the deciding
predicate inside that loose bound. No sums, peak ordering, tuning update or public
values change. A separately compiled release oracle compared2,321,256 chunks at
four rates and four chunk sizes with bit-identical public outputs.

Hard acceptance remains0.3% total core use and60us worst hop at48kHz. Tests enforce
those exact values when `GAPFILL_PERF_ENFORCE=1`; diagnostic runs on busy machines
never substitute looser assertions for that reference gate. See the context-budget
progress record for evidence and outstanding platform timing.
