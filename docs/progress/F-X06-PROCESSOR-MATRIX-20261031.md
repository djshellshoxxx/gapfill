# Actual processor cooperative render matrix —2026-10-31

`TestCoopRenderMatrix.cpp` drives four real prepared processors for64 bars of the
same repeating120BPM drum loop. No synthetic views, manual registry calls, forced
fills or open editor are used. Each instance has a unique fixed slot, equal
priority, full Yield and one-bar grid. Absolute sample-derived PPQ removes host
accumulation differences. The fixed Stutter algorithm and Full quality avoid
unrelated automatic quality/load choices.

The reference processes64-sample callbacks in fixed order on one thread. Four
additional runs shuffle callback order on one/four threads at64/512 samples.
Every actual stereo PCM bit is compared directly against the reference, with
first mismatch position retained. The196.6MB reference is allocated outside the
audio callbacks. Fill-start samples/planned lengths and auxiliary digests must
also match. All64 bars must contain natural fill starts, and each bar's start
mask may contain only one member. Every observed start must belong to its elected
owner at its exact decision PPQ; all four tracks must have finite nonzero fill
audio and no dropped transition evidence.

Four worker threads run distinct processors. Harness barriers/input copying and
observation/vector updates are outside processBlock. All processors join before
any callback; each run releases every member before the next independent cohort.
This exercises fresh-project registry reuse as well as render-order independence.

C++20 syntax passes with -Wall/-Wextra/-Werror against the exact root-derived
370caaa worktree. Native M1/M3 now PASS on immutable652592ca0ead7981738833cea0453fa9fa61e0c8:
all five actual render profiles satisfy every unchanged assertion in25003.1ms.
Only this new test TU was compiled, with the immutable candidate's exact
UNIT_TEST/header flags, then linked to its complete existing object set. The
test's first include was redirected to the immutable Tests/support path; no DSP
source, JUCE module or baseline object was rebuilt. Functional execution ran
under the coordinator's CPU lease; this does not certify the M6 CPU budget.

Evidence: `/workspace/work/evidence/coop-matrix-652/matrix.log` and
`fingerprint.json` record command lines, exact source/header/binary identities
and output. Original test source902737c SHA256:
`e2a25bf3026d95d9fda5e9a08f0ae1a2323b0c0657adb757c61028e55c2dd155`.
Approved baseline binary SHA256:
`6f9c2cab7378c5047f64030f1372f4fcfac054124f2e0421dada3cb6e97efa4b`.

The coordinator's separate reproduction of legacy callback-phase dependence at
a parameter-change boundary is not disproved by this constant-parameter matrix.
Its core compatibility/versioning diagnosis remains independent. Windows/real
DAW thread scheduling and sandboxed-host fallback are still unverified.
