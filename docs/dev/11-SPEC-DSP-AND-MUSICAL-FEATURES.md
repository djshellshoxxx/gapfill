# 11 - Supplemental specifications: DSP and musical behavior

Status: developer-ready candidate specifications, added 2026-10-20. These tickets
remain deferred until Gates A and B in `08` pass and one ticket is selected. They
supplement, and do not replace, the established E/C/U specifications. In particular,
existing Stutter, Freeze, Comb, Tape Stop, sidechain detection and Capture behavior
are foundations; these tickets specify bounded extensions only.

## Shared DSP contract

Each effect is an independent module with a small settings POD and a preallocated
processor object. It receives an immutable fill recipe and source-window descriptor;
it cannot schedule fills, edit shared parameters, or own UI state. `prepare()` may
allocate; `process()` may not. Maximum work per sample and memory are documented and
tested. Disabled/default paths remain golden-equivalent to the current plugin.
Algorithms with feedback must be stable for every valid control value and recover to
finite output after invalid input. All timing uses the E-2 transport snapshot; missing
or discontinuous transport must not create unbounded or callback-size-dependent work.

## F-M14 - Rhythmic bitcrushing

**Goal:** add beat-shaped sample-rate and bit-depth reduction to generated fill audio.

**Contract:** effect is active only on generated voices. Controls: bit depth 4-16 bits,
hold interval 1-32 samples or tempo divisions 1/4 through 1/64, wet 0-100%, and an
optional per-fill envelope. Default off. Quantization is symmetric around zero with
no DC offset; dither is optional and deterministic from recipe seed. Dry input and
detector audio are untouched.

**Implementation:** implement a `RhythmicCrusher` module after the source reader and
before the existing output guard. Use phase accumulation for fractional sample holds;
tempo subdivisions derive from PPQ at the current render sample. Reset phase at recipe
start and handle discontinuities by restarting at the next safe boundary.

**Acceptance:** verify silence, low-level signals, Nyquist tones, stereo linking,
automation extremes, 44.1-192 kHz, deterministic dither and tempo fallback. Measure
aliasing and document expected spectral change. Peak remains bounded and default-off
goldens remain unchanged.

## F-M15 - Tunable comb resonator

**Goal:** extend the existing Comb algorithm with direct pitch and damping control.

**Contract:** expose root frequency 30-2000 Hz, resonance/decay 0-90%, damping
frequency 500-16000 Hz and optional scale-constrained pitch as a later dependency on
F-L18. The existing Comb preset sound and current parameters remain unchanged when
new controls are at defaults. Feedback is internally capped below unity with a
sample-rate-aware margin.

**Implementation:** isolate delay/filter state in `CombResonator`. Smooth time and
feedback coefficients to prevent zippering; clamp delay to available prepared memory.
Per-band instances are owned by F-L14 if that ticket is selected. Do not change
existing Comb output until this ticket's explicit migration/default decision is
reviewed.

**Acceptance:** frequency sweep tests measure resonance within 2% of target at 44.1,
48, 96 and 192 kHz; impulse-response tests confirm monotonic decay and finite feedback.
Test parameter automation, short buffers, stereo and old presets. No state value can
request a delay larger than the allocated bound.

## F-M16 - Rhythmic ratcheting

**Goal:** repeat a selected short region in a deliberate accelerating or decelerating
pattern.

**Contract:** subdivisions are 2, 3, 4, 6 or 8 repeats per eligible fill region.
Direction is even, accelerate or decelerate; rate ratio remains within 0.5-2.0. Every
slice fits the source window and receives a click-safe edge envelope. No extra fill is
scheduled outside the current recipe duration.

**Implementation:** add a `Ratcheter` voice module. Build slice boundaries from integer
sample positions to avoid cumulative drift. For tempo mode, calculate target duration
from E-2 PPQ; in free mode, divide the recipe window evenly. Disable or fall back to
even repeats when the minimum safe slice is not met.

**Acceptance:** assert exact repeat count and end sample, no OOB reads, no clicks over
the shared threshold, and deterministic renders across callback sizes. Test all
divisions at 32-sample blocks and 192 kHz.

## F-M20 - Tape destruction expansion

**Goal:** add controlled tape-like instability around the existing Tape Stop voice.

**Contract:** optional wow/flutter depth 0-5%, dropout probability 0-10% per second,
dropout depth 0-24 dB, saturation 0-12 dB drive and splice count 0-4 per fill. Every
control defaults neutral. Dropout and splice events are deterministic from the recipe
seed; no random events are generated outside the active fill.

**Implementation:** isolate stages in order: resample-speed modulation, dropout gain
envelope, soft saturation, splice crossfade. Use a per-voice PRNG substream; do not
consume the scheduler RNG sequence. Smooth control changes and reserve all buffers in
`prepare()`.

**Acceptance:** fixed-seed render is byte-stable; zero settings match the old Tape Stop
golden; high settings remain finite and bounded; splice boundaries meet click tests.
Run spectral/THD characterization and worst-case CPU measurement for all active voices.

## F-L09 - Groove inheritance

**Goal:** align generated subdivision events to the input's observed microtiming rather
than impose an unrelated straight grid.

**Contract:** measure swing from a rolling window of at least 8 reliable events, with
confidence >= 0.7. Clamp inferred offset to +/- 20% of the subdivision and smooth
updates over one bar. If confidence is low, reset to straight timing. This affects
generated placement only; it never moves host transport, protection events or source
transients.

**Implementation:** add a bounded event-statistics module using detector timestamps.
Exclude sidechain duplicates, protected transients and tempo-discontinuity windows.
Store only aggregate timing statistics; no raw audio or song-long event history.

**Acceptance:** synthetic straight/swing fixtures (0-67% swing) recover the expected
offset within 5% of a subdivision; noisy, changing-tempo and sparse fixtures return
low confidence and neutral timing. Demonstrate block-size invariance and no placement
past protection boundaries.

## F-L10 - Call and response

**Goal:** produce a complementary later fill using a recent fill's bounded rhythmic
and algorithmic profile.

**Contract:** remember at most the last 4 compact recipes for 1, 2 or 4 bars. Response
preserves event count class and duration class while changing at least one allowed
dimension (algorithm family, direction, density). It must respect current user weights;
if no complementary option exists, report a reason and skip. User can clear memory.

**Implementation:** depend on U-1 `FillRecipe` history and C-5 lock conflict rules.
No captured audio is stored. Deterministic response variation uses a recipe-local
sub-seed; history is a versioned <= 8 KiB optional subtree.

**Acceptance:** paired fixtures show a complementary property change without altering
the protected timing class; fixed seed is repeatable; user-disabled algorithms never
appear. Test empty history, reset, preset/state load and mode off identity.

## F-X01 - Harmonic granular resynthesis

**Goal:** create pitch-coherent granular material from captured source while retaining
the original transient rhythm and a stable low-frequency foundation.

**Contract:** opt-in mode operates on a selected capture window and exposes grain size
20-200 ms, density 1-8 grains, pitch shift +/-12 semitones, harmonicity 0-100% and
stereo spread 0-100%. Pitch tracking is optional and confidence-gated; when confidence
< 0.75, preserve source pitch. No claim of polyphonic source separation.

**Implementation:** split analysis from playback. Analysis may run on a worker from a
bounded copied buffer; publish immutable analysis frames. Playback uses fixed grain
slots and preallocated windows. Define max capture bytes, max analysis time and worker
cancellation. Never wait for analysis on the audio thread; unavailable analysis falls
back to unpitched granular mode.

**Acceptance:** sine, harmonic stack, drum and noise fixtures measure pitch error,
fallback confidence and transient preservation. Test worker cancellation, buffer
replacement, CPU/memory ceiling and finite output at all sample rates. Provide audible
blind-review renders against the current Granular mode before release approval.

## F-X02 - Spectral Freeze and Morph

**Goal:** sustain a captured spectral frame and morph between two captured frames for
textural fills.

**Contract:** capture A/B explicitly or at a fill boundary; expose freeze length,
morph amount, pitch shift +/-12 semitones, formant shift +/-6 semitones and decay.
Default bypasses this processor. Freeze and morph must have deterministic phase policy
and a documented latency. The mode is separate from the existing time-domain Freeze
algorithm.

**Implementation:** fixed-size STFT module with preallocated FFT buffers, bounded
overlap-add and explicit window/hop sizes. Report and compensate added latency through
the plugin wrapper; if the host cannot accommodate mode-switch latency safely, reject
activation while processing and apply at stop. No dynamic FFT plans in `process()`.

**Acceptance:** test impulse alignment, sine pitch, formant shift, stereo phase,
transient smearing, freeze release, state restore and mode-switch latency in hosts.
CPU and memory are budgeted for maximum active voices before enabling by default.

## F-X03 - Automatic key / pitch-class detection

**Goal:** estimate tonal center for optional pitch-bearing algorithms, with honest
confidence and manual override.

**Contract:** return root pitch class, optional major/minor tendency and confidence
0-1. Publish `unknown` below 0.75 confidence. Manual selection always overrides the
estimate. Drum/noise-only input must normally return unknown. Analysis is local and
non-persistent except the user-selected override.

**Implementation:** worker-thread analysis consumes bounded spectral summaries, not
unbounded raw history. Define minimum observation duration (>= 2 s), maximum history
(<= 16 s), update cadence (<= 1 Hz), and reset on seek/transport discontinuity. No
automated parameter changes; consumers read a snapshot.

**Acceptance:** labeled synthetic and permissively licensed fixtures report precision,
recall and calibration by source type. Silence/noise confidence remains below
threshold; manual override is immediate and stable. CPU, cancellation and stale-result
tests cover sample-rate changes and editor closure.

## F-X04 - Multi-section arrangement awareness

**Goal:** classify repeated rhythmic sections and boundaries to vary fills over a song
without accessing project files or a network.

**Contract:** operate on live audio and host transport only. Expose enable, sensitivity
and reset. Show section/boundary confidence and allow the user to lock/reset a section.
Never claim formal verse/chorus semantics. When uncertain, remain neutral and use
existing gap logic.

**Implementation:** bounded feature vectors (onset density, spectral bands and
periodicity), <= 256 recent frames; worker-thread segmentation; explicit stop/seek
reset rules. Only pass a small section descriptor and confidence to the decision
stage. No song-long raw audio retention or automatic scene/preset recall.

**Acceptance:** synthetic repeated-pattern fixtures detect repetitions/boundaries
within 1 bar at supported tempos; unrelated/noisy material remains low confidence.
Test memory ceiling, cancellation, transport discontinuities and no audible change
below confidence threshold. A reviewed blind test must show useful section variation.

## F-X05 - Local preference learning

**Goal:** rank locally used recipes from explicit user likes/dislikes without sending
telemetry or making choices opaque.

**Contract:** learning is off until user enables it. Only explicit Keep/Like/Dislike
actions count. Store aggregate counts and feature vectors, not audio or session names.
Provide clear, reset and export controls. Recommendations include a short reason and
confidence; they never auto-apply.

**Implementation:** bounded local model with <= 512 recipe summaries, versioned state
<= 64 KiB and deterministic scoring. Define decay/eviction and prevent a single long
session from dominating. No filesystem scan or network access; persistence uses the
existing message-thread state mechanism.

**Acceptance:** fixture actions produce predictable rankings and explanations; reset
erases all model data; disabled mode makes no changes; corrupted/oversized state is
dropped safely. Privacy review confirms only documented aggregates are persisted.

## F-X06 - Cooperative multi-instance coordination

**Goal:** avoid redundant or conflicting fills across multiple plugin instances when
the host can provide a safe shared context.

**Contract:** disabled by default. No sockets, network, global named objects or
cross-process IPC. Coordination is best-effort, keyed to host-provided instance/group
identity; if identity is unavailable, operate independently. A failed/missing peer
never delays audio or silences local processing.

**Implementation:** prototype only after a host-specific capability survey. Use an
explicit opt-in group ID and bounded lock-free message exchange with sequence numbers,
expiry and single-writer ownership. Define ownership and unload semantics before
implementation; never expose raw pointers between instances.

**Acceptance:** host matrix demonstrates identity availability and clean teardown.
Stress 1-16 instances with peers starting/stopping and automation; no blocking, races,
stale messages or cross-project leakage. Until those guarantees pass, this remains a
research-only ticket with no code.

## F-X07 - Adaptive performance / quality modes

**Goal:** let users select predictable DSP quality/resource tradeoffs and optionally
warn when measured processing time is near the host deadline.

**Contract:** offer fixed `Eco`, `Balanced`, `High` modes first. Modes have documented
algorithm-specific quality settings and must be user-selectable; do not silently change
quality in response to CPU load. Any future automatic fallback is opt-in, hysteretic,
logged in UI telemetry and reversible at a block boundary. No change to latency unless
the host is stopped and has accepted a new PDC value.

**Implementation:** define a per-module quality table and benchmark each combination.
The audio thread reads one immutable mode snapshot. CPU telemetry may warn but cannot
block or select a mode. If no safe cheaper path exists for an algorithm, that algorithm
remains unchanged in Eco mode.

**Acceptance:** benchmark representative sessions on minimum supported hardware;
publish CPU, memory, alias/noise and latency results per mode. Repeated rendering is
deterministic within each selected mode; switching is click-safe and defaults preserve
current quality. No automatic mode changes in the initial release.

## F-L13 - Enhanced sidechain-driven fills

**Goal:** improve how sidechain events shape generated fills while preserving the
existing detector-source contract.

**Contract:** sidechain is timing/shape input; main input remains the audio source
unless a separately reviewed routing feature is selected. Add configurable threshold,
hold/release and event confidence only if absent from current parameters. Sidechain
silence or disconnect falls back according to existing Detector Source behavior and
shows status; it never produces unbounded fill density.

**Implementation:** isolate sidechain envelope/event analysis from recipe generation.
Use existing latency alignment and protection policy. Any external-source latency
must be measured and included in PDC; no additional audio bus by this ticket.

**Acceptance:** main-only, sidechain-only and hybrid tests across mono/stereo, missing
bus, silence, polarity, variable blocks and transport reset. Verify protection against
sidechain transients, exact reported latency and no change to existing modes when new
controls are neutral.

## F-L14 - Multiband effects routing

**Goal:** process generated fills through low/mid/high paths with independent effect
selection and levels.

**Contract:** initial split uses two crossover frequencies (default 180 Hz and 2.5 kHz)
with a phase-coherent Linkwitz-Riley 4th-order crossover. Per-band type selection reuses
C-9's algorithm enum; per-band gain is -24 to +6 dB, default 0 dB. No band may alter
detector, dry path or latency. Disable means the current broadband path exactly.

**Implementation:** implement a fixed three-band routing module and one processor
instance per active band. Preallocate filters/state for all bands, avoiding runtime
construction. Handle sample-rate clamping and crossover order; do not process an
algorithm multiple times when bands are inactive.

**Acceptance:** null-sum/reconstruction response is within +/-1 dB from 30 Hz to
18 kHz at supported rates (excluding defined Nyquist guard band); impulse alignment
is within one sample. Test crossover automation, stereo image, max-voice CPU/memory,
state migration and default-off golden identity.

## F-L15 - Dynamic dub feedback network

**Goal:** create tempo-aware stereo ping-pong repeats with evolving filter and
saturation on generated fills.

**Contract:** time divisions 1/32-2 bars or free 1-2000 ms; feedback 0-90%, crossfeed
0-100%, damping 200-12000 Hz, modulation depth 0-5 ms and drive 0-12 dB. Hard internal
feedback ceiling is 0.90, even if state data is corrupt. A wet envelope confines
feedback tails to the configured voice tail limit.

**Implementation:** separate `DubFeedback` delay module with preallocated buffers,
smoothed coefficients and soft clipping in the loop. Define max tail and voice-steal
behavior; reset state on panic, deactivation and sample-rate change. No shared feedback
between instances.

**Acceptance:** impulse decay never grows, output remains finite under max feedback,
DC input and NaN injection; stereo ping-pong and tempo divisions are verified. Test
tail truncation, automation and memory/CPU at maximum concurrent voices.

## F-L18 - Manual scale and key awareness

**Goal:** constrain pitch-bearing fills to a user-chosen root/scale without automatic
key detection.

**Contract:** root C-B plus scale None, Major, Natural Minor, Dorian, Mixolydian,
Pentatonic Major or Pentatonic Minor. None is identity. Quantize generated pitch
intervals to the nearest in-scale semitone within +/-12 semitones; ties choose the
smaller absolute move, then lower pitch. Does not pitch-shift the dry or source signal.

**Implementation:** pure pitch-class mapping table shared by Comb/pitch/granular
consumers; no analysis and no transport dependency. Store root/scale as stable choice
parameters, and expose the selected scale in help/status. Add an extension hook for
F-X03, but do not enable automatic detection here.

**Acceptance:** exhaustive tests over 12 roots, 7 scales and every semitone interval
-12..+12; prove identity for None, output remains within range, and mappings are
deterministic. Old states load with None and render unchanged.

## F-L19 - Frequency-shifted delay

**Goal:** add a frequency-shifted feedback delay distinct from pitch-shifted delay.

**Contract:** shift range -500 to +500 Hz, delay 1-2000 ms or tempo division,
feedback 0-85%, wet 0-100%, default off. Shift is a true frequency translation, not a
musical pitch ratio. Include anti-alias filtering and mono/stereo mode. Silence and
sub-audio handling are defined and finite.

**Implementation:** use a bounded quadrature/Hilbert shifter with preallocated FIR or
allpass network; document group delay and compensate only if stable across mode
changes. Isolate feedback limiter and parameter smoothing in the module.

**Acceptance:** sine sweep measures frequency shift within 2 Hz or 2% (whichever is
larger), tests sidebands/aliasing across sample rates, max feedback decay and CPU.
Verify state and latency/PDC; defaults leave existing output unchanged.

## F-L20 - Formant transformation

**Goal:** reshape spectral envelope independently from fundamental pitch in captured
fills.

**Contract:** formant shift -6 to +6 semitones, wet 0-100%, optional vowel targets
A/E/I/O/U as named spectral-envelope presets. This does not promise intelligible
speech or source separation. Default bypasses processing; latency is fixed and
reported before activation.

**Implementation:** begin with a bounded LPC or cepstral envelope method selected by
an offline benchmark; document numerical stability and fallback when estimation fails.
Compute coefficients on a fixed hop, interpolate safely between frames and clamp
gain. If no method meets CPU/quality budgets, do not implement under this ticket.

**Acceptance:** synthetic formant fixtures and pitched/noisy sources quantify envelope
shift, pitch preservation, artifacts and latency. Test silence, transients, stereo and
malformed coefficients. Require listening review before release; no claim of speech
intelligibility.

## F-L21 - Transient-derived convolution texture

**Goal:** turn a selected captured hit into a short convolution response for generated
fills.

**Contract:** user selects capture window, maximum impulse duration 250 ms, wet 0-100%,
and decay trim. The source impulse is normalized only according to an explicit
`Normalize IR` option; default preserves its level. A captured transient is local,
temporary data and is not embedded in presets or project state unless the user exports
it through the existing explicit export workflow.

**Implementation:** create IR and FFT partitions off the audio thread from a bounded
copy; publish an immutable convolution engine at a block boundary. Crossfade engine
replacement. Set a per-voice CPU budget and max active convolvers; excess requests
degrade to the documented existing reverb/texture path or report unavailable.

**Acceptance:** impulse response comparison, known IR convolution reference, tail
truncation, cancellation, memory/CPU maximum and sample-rate change. Test no audio-thread
allocation/wait and ensure state contains only parameters, not raw captured audio.

## F-L22 - Musical energy tracking

**Goal:** estimate local rhythmic/spectral activity and let the user map it to fill
density or character with hysteresis.

**Contract:** energy score 0-1, confidence 0-1, attack/release times 100 ms-8 s.
User mappings are limited to allowlisted effective settings. Feature defaults off and
neutral; it never uses song structure or key inference. Low confidence freezes the last
stable value for at most one bar, then returns to neutral.

**Implementation:** consume existing detector and spectral aggregates; one-pole or
bounded envelope features only, no raw history. Publish a fixed-size snapshot. Apply
mapping at decision time so variable blocks do not change response.

**Acceptance:** fixed synthetic quiet/loud ramps recover score within defined tolerance;
hysteresis prevents toggling under threshold noise. Test sparse drums, silence, tempo
change and confidence fallback. Demonstrate block-size invariance, state identity when
off and bounded CPU.

## F-L11 - MIDI output from gap/event detection

**Goal:** optionally emit MIDI events aligned to detected gaps/fills for external
instruments or visual synchronization.

**Contract:** define an opt-in MIDI output mode with event types: gap-start note, fill
type note, and fill-end note-off. User selects channel, note range and velocity source.
Never emit MIDI when disabled; note-offs are guaranteed on panic, stop, bypass and
deactivation. Existing audio output and timing are unchanged. Do not claim sample-accurate
output where the host format cannot preserve event offsets.

**Implementation:** add MIDI output declaration only for formats/hosts verified to
support it; collect absolute sample offsets during the current block and append to the
host MIDI buffer after audio scheduling. Fixed-capacity event staging with deterministic
overflow policy (drop newest non-note-off first). Ensure note-off priority. State stores
only configuration.

**Acceptance:** plugin-format bus/port validation, host tests on Windows/Linux/macOS,
block-boundary events, variable blocks, seek/loop/stop, event overflow and no stuck
notes. Compare audio golden renders with MIDI mode both off and on. Record known host
limitations in release docs.

## F-L12 - Separate wet output bus

**Goal:** expose generated wet audio as an optional host output while retaining the
existing main output.

**Contract:** standard output bus remains byte-for-byte compatible in channel layout.
Optional wet bus carries only generated signal, aligned to the same PDC. When host does
not enable the bus, behavior is identical to current output. This is distinct from
F-M17 wet-only monitoring and must not silently change main-bus mix.

**Implementation:** change JUCE bus declarations in a dedicated platform/format PR;
verify VST3, AU and CLAP bus semantics before enabling each format. Internal DSP renders
wet once and fans out to main mix and optional wet bus. Host bus enable/disable occurs
through supported lifecycle callbacks, never mid-block allocation.

**Acceptance:** format validators and real-host tests check bus activation, mono/stereo,
sidechain coexistence, dry/wet alignment, latency, state restore and disabled-bus
compatibility. Every supported format must pass; unsupported formats omit the bus with
documented capability metadata.

## F-L13 is specified above; F-L14/F-L15/F-L18-F-L22 are independent optional modules.

## Feature dependency notes

1. F-L09 needs reliable detector event timestamps and E-2 transport continuity.
2. F-L10 needs U-1 recipe history; it stores recipe summaries only.
3. F-L11 and F-L12 require host-format research before parameter/UI work.
4. F-L14 should follow C-9 and a verified crossover fixture; F-M15 should precede it
   only if the comb module is selected for multiband use.
5. F-X01/F-X02/F-L20/F-L21 are separate DSP research spikes. Do not combine them in a
   single feature branch; benchmark a standalone prototype before production code.
6. F-X03 may feed F-L18 as a suggestion, but may not override manual scale settings.
7. F-X04/F-X05/F-X06/F-X07 require privacy, state-size, host and benchmark reviews
   before implementation.
