# W-03 endpoint-tempo prediction behavior fix

This is a separate behavior change after the preserved extraction in
`W-03-20261031.md` / commits `0467c2a`, `43fa70a`, `4bd29ce` and `3488494`.
The released model was characterized before editing: 9 reference cases and
10 extracted-helper cases passed on candidate `780bd17`. The extractor itself
is not relabeled as this feature.

The detailed W-03 specification requires prediction over the **previous whole
host callback**, using the mean of previous/current BPM for a linear tempo ramp.
The tracker now keeps the previous PPQ, resolved BPM and callback duration. That
duration is measured using the sample rate which produced the interval; a later
`prepare` must not reinterpret it at a new rate. No processor adapter, parameter,
state format, musical RNG or DSP path is edited here.

An instantaneous BPM step at the current callback boundary has a different
integral: the previous interval used the previous BPM for its entire duration.
Therefore a changed BPM permits exactly two hypotheses:

1. `previousPPQ + previousSeconds * (previousBPM + currentBPM) / 120`.
2. The released previous-BPM endpoint, retaining its original operation order.

Each prediction has the same inclusive **0.25 beat** tolerance. Positions in the
gap between their tolerance neighborhoods are rejected; this is not an expanded
continuous acceptance interval. At constant BPM the original arithmetic and
transition behavior are retained. Start, stop, PPQ loss/recovery and no-PPQ
one-shot behavior remain unchanged.

There is unavoidable ambiguity: PPQ/BPM endpoints alone cannot distinguish a
seek that lands exactly on one tempo hypothesis from actual tempo integration.
A sample-position trace and operator annotations are needed to investigate such
cases. This patch does not claim every simultaneous seek/tempo change is
identifiable, and does not tighten the threshold without two real-host traces.

## Local evidence

The new tests were run before changing production code: **13 passed / 3 failed**.
Failures demonstrated the missing linear-ramp integral, inclusive tolerance at
the averaged endpoint, and retaining the old interval's sample rate across a
tempo-changing reprepare. After the implementation, **16 passed / 0 failed**
with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`; the unchanged
released reference still passes its 9 cases.
The same 16 pure cases also pass UndefinedBehaviorSanitizer. The combined
AddressSanitizer/UndefinedBehaviorSanitizer executable could not complete leak
checking here: LeakSanitizer reports that this managed process runs under
ptrace. That environmental failure is retained, not reported as a passing
sanitizer run; the complete native sanitizer workflow remains required.

The ramp uses an independently specified integral, BPM(t)=60+7.5t and
PPQ(t)=t+t²/16, across eight bars, five sample rates (8/44.1/48/96/192 kHz) and
65536/32/8192/64/512/4096 callback lengths. Endpoint-step up/down, exact/adjacent
tolerance boundaries, the rejected gap between hypotheses, one-shot real seeks
and a rate-changing reprepare have explicit cases. A recorded released predictor
demonstrates the false ramp reset. Constant-tempo equality against that reference
covers 300,000 seeded callbacks across the five rates and three BPM values; the
prior all-tempo equality assertion is intentionally scoped to the behavior that
must remain neutral. A native AllocGuard case was added for the processing
update path and awaits the fresh integrated native candidate.

Standalone commands (no JUCE build required):

```bash
g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -DGAPFILL_TRANSPORT_STANDALONE Tests/features/TestTransportTracker.cpp \
  -o /absolute/evidence/transport-prediction
/absolute/evidence/transport-prediction
```

The additional private scalars change `TransportTracker`'s internal layout.
Rebuild all header consumers for the next native candidate; do not splice these
objects into the immutable `780bd17` build. Its ongoing native run establishes
evidence for that older candidate, not this later behavior change.

## External work remains open

Fresh integrated `TransportTracker`/`Transport`/`Plugin`, allocation, neutral and
golden checks must run on the resulting candidate and supported platforms.
Actual REAPER and a variable-block host must record PPQ jitter, loops, seeks,
steps/ramps, bypass/PDC and rate changes with provenance. No real DAW or hardware
is available in this workspace; no host measurement or tolerance retune is
claimed. The optional timing-only probe and offline analyzer are a separate
tooling slice.
