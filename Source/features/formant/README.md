# Per-voice formant transformations

F-L20 is independent of the shipped `fmt*` bus card. Its six `fl20_*`
parameters are appended as a new tail group; values are latched when a fill
starts. Amount zero never requests a processor or enters the new DSP path.
Normal Undo/A-B includes these parameters; Random's explicit allow-list does
not include them. No new persistent subtree or recipe flag is used.

`FormantProcessor` applies a bounded cepstral-envelope warp while preserving
analysis phase, with guarded log magnitudes, tapered quefrency liftering,
vowel/preserve blending and frame-energy matching. A spectrally flat impulse
frame remains unchanged to protect its onset from pre-ringing. Stereo is
processed with identical settings and unchanged bin phases.

`prepare()` allocates FFT plans, all scratch and delay buffers. FFT sizes are
512/1024/2048 at up-to-48/up-to-96/up-to-192 kHz, with quarter-frame hops.
The private stereo STFT's N-1 delay receives one extra output sample so this
stage has exactly N samples of latency. Both independent real channels are
packed into one complex FFT; real-even log spectra likewise share paired
cepstral transforms. This halves transform count without sharing envelopes,
retuning either channel or introducing another FFT dependency. The original
SP-0, bus card and plugin PDC are unchanged.
Non-finite input/frame data clears the stage to silence and increments its
fault count without releasing the voice's ownership.

The engine owns two processors. The audio thread alone requests/releases
them; there are no locks, allocations or cross-thread ownership changes.
Short gaps and a third simultaneous request play unprocessed and increment
separate atomic counters. Natural fills require enough planned duration
for their algorithm minimum plus N samples. Source input stops N samples
before the existing output deadline and zeros drain the processor inside
that deadline; a delayed onset fade and the original final fade/duck/panic
remain outside the stage. This resolves the detailed spec's contradictory
instruction to prime the first N source samples, which would otherwise
remove the specified N-sample onset delay.

Reset/rate changes release all claims; a voice ending or being killed also
releases its processor. Processor pointers are never serialized. Their
owning engine must outlive its voices, and the host must stop audio before
destroying a processor, as for the original voice pool.

The compact CreativeExtra FORMANT disclosure follows the existing SHAPE
popover pattern and exposes all six controls in two rows; the current
header slot cannot hold a full expanded card. The controls use standard
keyboard/accessibility/tooltip components and no animated envelope display.
Owner/platform visual review of this placement remains an acceptance item.

Component contracts live in `TestFormant.cpp`; production state, off-path
audio and voice deadlines live in `TestFormantIntegration.cpp`. Performance
is measured by the Perf case and enforced only when the existing
`GAPFILL_PERF_ENFORCE` policy requests it. Numeric, native-format, sanitizer,
listening and visual evidence must be recorded separately; source presence
and syntax compilation are not a feature acceptance result.
