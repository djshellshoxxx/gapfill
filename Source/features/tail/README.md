# Host tail prediction

Header-only, JUCE-free host metadata. `PreparedBounds` contains complete legacy
serial stage extents, including cap fade/chunk overshoot/spectral support.
`Activation` is built from current raw parameter atomics at getTailLengthSeconds,
not cached during the last callback. Its parallelMaximumSeconds is the maximum
of active new parallel returns, never their sum. The result conservatively adds
base voice duration, old serial extents and that maximum; it therefore also
covers sends branching before the serial chain. AllFXoff remains exactly2.5s.

`PublishedBounds` stores all six extents in one lock-free64-bit word, initialized
in its constructor before audio. Prepare/control owners publish once complete.
Bounds round upward to1/16s so torn rate/cap snapshots are impossible and no
partial update under-reports the host bound. Per-stage supported range is0 to
31.9375s; an invalid active snapshot fails conservatively to a finite1hour bound.
The adapter should treat publish=false as an internal preparation/config error.

Root owns the getter and actual card preparation hooks. Publish the actual
prepared full-quality upper bounds (quality may later shorten them). Legacy Dub
can use16s, not only the default8. A card with a silent-input cap additionally
needs its250ms cap fade and up to256/sampleRate seconds of chunk overshoot.
Spectral cards need their finite analysis/synthesis support; a safe adapter
bound is2*(N-1)/sampleRate. New pervoice formant finishes within its planned voice
deadline, so it does not add a separate bus tail. New mode selection can suppress
a legacy card in the activation snapshot. Disabled/zero-amount current parameters
can return the base bound because their contribution fades well inside2.5s.

No audio sample, PDC, processor pointer, source buffer, GUI or audio-owned isIdle
state is accessed here. The host query can run concurrently with preparation.
