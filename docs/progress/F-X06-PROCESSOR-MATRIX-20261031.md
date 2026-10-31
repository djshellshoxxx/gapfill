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
370caaa worktree. Native execution remains pending the coordinated immutable
candidate. This is a strict acceptance test, not a claim that M1/M3 already pass.
The coordinator has a separate reproduction of legacy callback-profile audio
dependence; if it affects this matrix, retain the exact PCM assertion and fix the
cause through the approved compatibility/versioning seam.
