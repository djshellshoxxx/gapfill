# 10 - Supplemental specifications: live performance and workflow

Status: developer-ready candidate specifications, added 2026-10-20. These tickets are
not approved for implementation until Gate A and Gate B in `08` pass and one ticket
is selected. Existing E/C/U specifications remain authoritative wherever IDs overlap.

## Shared contract

All new UI belongs in a registered feature slot and uses the existing parameter and
state rules in `02` and `06`. Proposed parameter IDs below are names for discussion;
reserve their final IDs append-only in the parameter registry before implementation.
No module may call the editor, allocate, lock, log, or perform file/device I/O on the
audio thread. Commands cross threads through the bounded command path. Defaults must
preserve today's output, host state, latency and MIDI behavior. Every command has a
defined cancellation path for stop, bypass, editor close, transport discontinuity and
plugin deactivation.

## F-S01 - CPU and latency status

**Goal:** explain current host-reported latency and whether the plugin is approaching
its measured processing budget without presenting an unreliable CPU estimate as a
fact.

**Contract:** show the exact reported latency in samples and milliseconds. Show a
separate rolling processing-time estimate only when a monotonic timer is available;
label it `estimated`, include an averaging window, and display `--` when unavailable.
The estimate is editor telemetry, not a host parameter and not saved in project state.
Never claim system-wide CPU usage. Update the UI at <= 10 Hz from an atomic snapshot.

**Implementation:** collect bounded process-duration samples in a preallocated ring
or fixed-window accumulator; do not call a clock per sample. Define the denominator
and reset behavior for sample-rate changes, suspend, offline render and zero-length
blocks. Expose a POD snapshot through the processor/editor telemetry seam.

**Acceptance:** verify latency equals the host's `getLatencySamples()` after prepare,
lookahead changes and sample-rate changes; telemetry remains finite, updates within
250 ms while the editor is open, performs no audio-thread allocation, and disappears
cleanly when timing is unsupported. Benchmarks must show no measurable material
regression against the locked baseline.

## F-S06 - Level-matched A/B comparison

**Goal:** compare two plugin configurations quickly and return to the original state.

**Contract:** A is the current state when comparison is first enabled; B is a bounded
copy of the state at the instant the user presses `Capture B`. Toggle A/B without
changing host automation values or writing the inactive state into the host's
parameter tree. Provide optional loudness matching based on a fixed measurement window
and display the applied gain. Matching is preview-only, capped at +/- 6 dB, and off by
default. This ticket does not add preset management or automatic recipe generation.

**Implementation:** keep two versioned, validated state snapshots on the message
thread. Apply a requested snapshot at a block boundary using the existing atomic
parameter/state mechanism; smooth audio-affecting values over 10 ms. Store match gain
outside the saved plugin state. Preserve A and B across editor reopen, but clear both
on processor reset unless the user explicitly saved a comparison.

**Acceptance:** A/B toggles are click-free, state values restore exactly, host
automation resumes control on the next host update, malformed state is rejected
atomically, and repeated switching never allocates on the audio thread. Render tests
measure matched-window RMS within 0.5 dB where neither state is silent; silent states
show `match unavailable`.

## F-S07 - One-click Stutter Storm

**Goal:** provide a dramatic, single-action stutter performance gesture using the
existing stutter voice and capture memory.

**Contract:** a momentary command selects a documented preset profile for the next
eligible gap only: subdivision, repeat count, direction pattern and wet envelope.
It MUST respect the existing detector, protection, maximum-fill and output-safety
limits. If host tempo is invalid, use the current sample-domain fallback. The action
does not rewrite the user's stutter parameters and is not a new continuously active
mode.

**Implementation:** represent the action as a fixed-size command/profile enum, not a
random parameter write. Seed any variation from the instance seed plus command
sequence so the same saved state and event sequence render deterministically. Show
armed/fired/rejected status in the performance slot.

**Acceptance:** tests cover valid/invalid tempo, no eligible gap, bypass, repeated
presses, stop, and block-size invariance. Peak output remains within the existing
limiter contract, and the command leaves serialized parameters byte-identical.

## F-S08 - Glitch Roulette

**Goal:** extend Surprise with a recoverable, constrained random variation for live
experimentation.

**Contract:** one press creates one candidate profile from currently enabled fill
algorithms and user-approved ranges. Never alter bypass, dry/wet, output gain,
lookahead, protection, routing, seed, or user-disabled algorithm weights. Provide
`Keep` and `Revert`; a second spin before Keep replaces only the pending candidate.
The current Surprise feature remains unchanged and this feature must not duplicate
its control.

**Implementation:** reuse the existing recipe/profile randomizer through a separate
bounded policy object. Use a dedicated deterministic sub-seed and keep pending,
accepted and original values distinct. State stores only the accepted profile through
the normal parameter state; pending preview is transient.

**Acceptance:** property tests prove protected parameters never change, disabled
algorithms remain disabled, every candidate is finite/in range, Revert restores the
exact pre-spin state, and fixed seed + spin count is deterministic. UI automation
checks button labels and keyboard operation.

## F-S10 - Genre-inspired performance presets

**Goal:** offer useful starting points built from existing algorithms, without
implying genre presets are exclusive or new DSP modes.

**Contract:** ship named, categorized factory presets for Breaks, Dub, Trance,
Ambient and Industrial. Each preset documents the parameters it intentionally sets;
all unspecified settings retain the current preset loading semantics. Names and copy
must describe musical behavior, not promise imitation of a specific artist or track.

**Implementation:** use the U-10 factory preset pipeline and quality gate. Do not add
special runtime branches for genre labels. Include at least two varied starting
points per category; each preset must have a distinct validated parameter fingerprint.

**Acceptance:** every preset passes U-10 tests for valid load, finite bounded output,
nonzero but reasonable fills, documented parameter changes and stable serialization.
Manual evaluation records short synthetic loop renders and reviewer notes for each
category. Existing presets and their IDs/order remain unchanged.

## F-S12 - Panic / safe return

**Goal:** provide an immediate, predictable user command that stops generated audio
and returns output to the normal input path.

**Contract:** panic cancels queued and active generated voices, clears transient
feedback state, and ramps the wet contribution to zero over 5 ms while preserving the
latency-compensated dry path. A separate optional `Safe Return` action also disables
feature commands until explicitly re-armed. Neither action modifies the host bypass
parameter. Host bypass behavior remains governed by the plugin format contract.

**Implementation:** add an atomic/queue command consumed at the next audio block;
voice cancellation must not reset shared state unsafely. Define behavior for a panic
received during offline render, deactivation and a zero-length block. Panic is
momentary and never serialized.

**Acceptance:** stress panic during every algorithm, at every supported block size,
while automation and transport changes occur. Output becomes finite and wet-silent
within 5 ms after command consumption; dry latency stays unchanged and no click
exceeds the established transient threshold. Re-arming restores the exact prior
settings.

## F-M03 - Visual gap timeline

**Goal:** show why and where fills are scheduled against detected transients and
protection regions.

**Contract:** display a bounded rolling window (default 4 bars when valid tempo,
otherwise 4 seconds) with input events, eligible gaps, protection zones, and fired or
rejected fill markers. Distinguish estimated analysis from exact fired events. The
view is observational; it must not affect scheduling.

**Implementation:** consume a fixed-capacity immutable visualization frame via the
F0-4 snapshot path. Add no more than 128 event records per frame; coalesce overflow
into an `events omitted` marker. Render only on the message thread and throttle to
<= 30 fps. Zoom and horizontal scroll are editor-only state.

**Acceptance:** synthetic event fixtures verify pixel-to-time mapping, tempo/loop
resets, overflow indication, resize, HiDPI and accessible text alternatives. Golden
audio hashes remain unchanged with the view open or closed.

## F-M10 - Momentary effects and latch controls

**Goal:** make supported performance gestures usable from mouse, keyboard and MIDI.

**Contract:** each gesture explicitly declares `momentary` or `latch` behavior.
Momentary ends on release, focus loss, editor close, MIDI note-off, host stop, or
deactivation. Latch ends on a second press or panic. Initial gestures are limited to
existing Stutter, Freeze, Reverse and Tape Stop algorithms; this ticket does not add
new effect algorithms or override protection limits.

**Implementation:** GUI/MIDI/keyboard send the same command enum to the engine. Track
command source so release from one controller cannot cancel a gesture started by
another. Debounce repeated button events and define command priority with panic highest.
Controller bindings are specified in F-L24.

**Acceptance:** test mouse-up outside control, focus loss, stuck MIDI note, disconnect,
rapid duplicate events, panic priority, host stop, and multiple editors. All cases
return to idle within one processing block; command handling remains bounded and
allocation-free.

## F-M11 / F-L07 - Quantized state changes and performance scenes

These IDs are one feature family; F-M11 is the scheduling slice and F-L07 is the
scene storage/UI slice. Do not create separate incompatible implementations.

**Contract:** store 8 scenes; each scene captures an allowlisted set of creative
parameters and commands, never routing, I/O, latency, bypass, dry/wet safety controls,
or plugin identity. Recall can be immediate, next beat, next bar or next phrase. On
invalid transport, show `quantization unavailable` and apply immediately only if the
user enabled that fallback. New recall replaces a pending recall; panic cancels it.

**Implementation:** immutable scene snapshots, versioned subtree <= 16 KiB, strict
validation and atomic rejection. Quantization uses PPQ/time signature at the decision
sample and the same discontinuity handling as E-2. Parameter ramps use the established
10 ms smoothing policy where applicable. UI may rename scenes; names are printable
UTF-8, <= 24 bytes.

**Acceptance:** test beat/bar/phrase boundary, tempo and meter changes, loop wrap,
seek, stopped transport, invalid PPQ, simultaneous recalls, old state and malformed
scene. Recall must be deterministic and block-size invariant; all stored parameters
round-trip, with excluded controls unchanged.

## F-M17 - Wet-only render mode

**Goal:** allow audition/export workflows to hear only generated material while
keeping the plugin's standard bus layout.

**Contract:** switch between normal mix and wet-only monitoring with a 10 ms ramp.
Wet-only changes monitoring/output mix only; it does not change detector input,
recipe selection, latency or host bus declarations. Muted/empty generated output is
silence, not an automatic dry fallback. Existing U-2 export remains a separate file
render operation.

**Implementation:** use a host-automatable boolean or choice parameter after
latency-aligned dry/wet summing. Include the value in state and automation; keep it
out of the audio-thread command queue if the existing parameter snapshot is suitable.

**Acceptance:** null-input, dry-only, wet-only, bypass and state-round-trip tests;
measure 10 ms transition envelope and confirm no peak overrun or latency change.
Verify output is exactly the wet path in wet-only mode within golden tolerance.

## F-M18 - Beat Battles / alternating styles

**Goal:** alternate between two existing style profiles on musical boundaries.

**Contract:** two named profiles A/B alternate once per configured bar count (1, 2 or
4). Starting side is user-selectable. Missing/invalid tempo pauses alternation rather
than guessing; host seek/loop reanchors phase using the current PPQ. Profiles obey the
existing zero-weight and protection rules.

**Implementation:** reuse the scene/profile snapshot from F-L07, but only when that
feature is selected; no third preset storage format. A/B phase is derived from
transport position and session seed, not callback count. Manual next command is
optional and uses the same pending state machine.

**Acceptance:** fixed transport tests prove exact alternation over 100 bars, loop and
seek behavior, invalid-tempo pause, profile persistence and no callback-size
dependence. Render comparison proves boundaries are click-safe.

## F-M19 - Freeze and Scrub control

**Goal:** expose bounded manual scanning of the existing captured source buffer.

**Contract:** Freeze captures the same source and maximum duration as the current
Capture feature. Scrub position selects within that frozen window; it cannot read
outside valid captured samples. Release resumes live capture only when Capture is
off. Scrub changes generated source playback, not the detector's source timeline.

**Implementation:** command/state POD owns capture generation, valid length and
normalized read position. Use preallocated memory and interpolation already supported
by the voice. Smooth position changes over 5 ms; reject stale capture generations.

**Acceptance:** test empty and partial buffers, wrap/end boundaries, 44.1-192 kHz,
mono/stereo, capture replacement and extreme scrub automation. No OOB reads, allocation
or discontinuity larger than the current click threshold.

## F-M21 - Tension / release curves

**Goal:** schedule repeatable multi-bar intensity and density arcs.

**Contract:** offer Rise, Fall, Rise-Fall and Hold-Then-Release curves over 1, 2, 4 or
8 bars, with start/end values bounded 0-100%. Curves alter only the allowlisted
effective settings (intensity, density, complexity); user base parameters remain
unchanged. Disabled is identity.

**Implementation:** calculate phase from valid PPQ at each decision, not block index.
Use exact analytic curves with documented interpolation and no random values. On seek,
loop or invalid transport, re-anchor/disable according to E-2's neutral fallback.

**Acceptance:** table-driven tests sample endpoints and midpoints, prove monotonicity
for Rise/Fall and symmetry for Rise-Fall, and verify no output beyond parameter bounds.
Test block-size invariance and old state defaults disabled.

## F-M22 - Smart preset suggestions

**Goal:** recommend installed factory/user presets from current audio characteristics
without cloud services or hidden recording.

**Contract:** suggestions are opt-in and explain the two or three measured factors
(e.g., transient density and low/high energy); never auto-load a preset. Show up to
three ranked results with confidence and a `not enough signal` state. No network,
telemetry or user-audio persistence.

**Implementation:** consume bounded aggregate statistics from the existing analysis
engine; do not retain raw audio. Rank against a versioned, local preset feature table
using a deterministic weighted distance. User can dismiss suggestions; no adaptive
learning is part of this ticket.

**Acceptance:** synthetic audio fixtures produce stable ranking, silence yields no
recommendation, confidence is calibrated on a documented fixture set, and the UI
never changes plugin state without an explicit user action. Test privacy by verifying
no file/network APIs are called.

## F-M23 - Extended modulation targets

**Goal:** let existing macros address a larger, still-safe set of continuous controls.

**Contract:** this is an extension to C-2, not a second modulation engine. Add only
reviewed continuous destinations; exclude routing, detector source, protection,
bypass, dry/wet, output, latency, random seed, commands and discrete algorithm choices.
Allow at most 8 mappings per macro; one owner per destination; values remain bounded.

**Implementation:** append destination IDs without reordering persisted C-2 IDs. Map
from normalized destination units and preserve C-2's curve semantics. If the mapping
table version changes, migrate by stable ID, not array index.

**Acceptance:** migration fixtures from each prior table version, collision rejection,
monotonicity, bounds and audio-thread cost tests. Existing C-2 mappings render
identically with the extension unused.

## F-L17 - Mutation Lab

**Goal:** explore controlled recipe variants and compare, pin, revert or save them.

**Contract:** create a batch of 4, 8 or 16 variants from a selected recipe. Each
mutation changes at most three allowlisted fields within user-set bounds; preserve the
parent's timing and source window by default. Variants carry parent ID, deterministic
seed and mutation summary. The feature never runs unattended during playback unless
the user explicitly auditions a variant.

**Implementation:** message-thread coordinator requests recipe variants from a pure
bounded mutation function. Store at most 16 fixed-size recipes (no audio) in the U-1
history budget. Pinning prevents eviction; newest unpinned entries evict first.

**Acceptance:** same parent/seed/config yields byte-identical variants; all bounds,
timing preservation and state caps are tested. Revert restores the parent; save uses
the documented recipe format; batch cancellation prevents late UI callbacks.

## F-L23 - Loudness-aware recipe comparison

**Goal:** compare two auditioned recipes at comparable perceived level.

**Contract:** extend E-1/U-1 audition to compare recipes A/B, with level match limited
to preview playback only, off by default and capped +/-6 dB. Use a fixed preview window
and show when either candidate is silent or too short for a reliable estimate.

**Implementation:** analysis runs on bounded preview buffers off the audio thread;
gain is published atomically before preview starts and ramped over 10 ms. This ticket
does not alter saved recipe levels or final live playback.

**Acceptance:** for non-silent test signals, matched preview RMS is within 0.5 dB;
silent/short candidates report unavailable; original candidate buffers and plugin
state remain unchanged. Test cancellation and rapid A/B toggles for stale-result races.

## F-L24 - Controller profiles and feedback

**Goal:** make MIDI mappings portable and predictable across hardware controllers.

**Contract:** support MIDI CC and note learn for approved parameters/commands, up to 4
profiles, one active profile per instance. Provide import/export through a bounded,
versioned JSON document. Hardware LED feedback is optional and only sent when the host
supports output MIDI; never open MIDI devices directly. Disconnect or host routing
loss clears held momentary commands and preserves mappings.

**Implementation:** reuse JUCE host MIDI input/output and existing MIDI-learn support.
Mappings key by profile plus status/channel/number; define pickup behavior for absolute
CC and relative modes. Output feedback is rate-limited to <= 20 messages/second and
echo-suppressed.

**Acceptance:** test channel filtering, CC/note learn, absolute/relative pickup,
feedback echo, unavailable MIDI output, disconnect, state import/export and malformed
JSON limits. No device scan, file I/O or allocation occurs in process callbacks.

## F-L25 - Live safety and recovery suite

**Goal:** combine panic, safe output bounds and last-good-state recovery under one
auditable live safety contract.

**Contract:** build on F-S12 panic and existing output sanitizer/limiter. Track a
validated last-good creative snapshot; restore it only by explicit command. Never
override host bypass, routing, output gain or latency. Report clipping/invalid-state
events without realtime logging. Safety mode remains active until user clears it.

**Implementation:** fixed-size status counters and snapshot; no unbounded error queue.
Define precedence: deactivate/host bypass, panic, safety mute, then ordinary feature
commands. Recovery validates the full snapshot before atomic apply.

**Acceptance:** fault injection covers NaN/Inf settings, corrupted optional state,
feedback runaway, transport discontinuity and command flood. Output remains finite,
recovery is deterministic, panic response meets F-S12, and counters are race-free.

## Deferred decisions from `05`

### Free / Pro split

Do not introduce build flags until pricing, upgrade and serialization policy are
approved. If selected, one source tree MUST produce both variants from a shared core;
factory presets/state are mutually loadable, and a free project opened in Pro must
retain every supported setting. Pro-only controls must fail closed without silent
parameter remapping. Acceptance requires reproducible dual-variant builds and license
UX review. This is a product decision, not a current engineering commitment.

### Quantized Fill Now

If selected, this is a distinct command from automatic gap filling. It requests one
fill at the next beat boundary using current settings; it does not fabricate a gap or
override protection. Queue one pending request; a new request replaces it. Invalid
tempo follows an explicit user-selected immediate/disabled fallback. Test PPQ seek,
loop, tempo ramp, stop, overflow and cancellation. Reuse E-2 timing code and report a
clear rejection reason when no safe source window exists.

### Fill morphing / rhythm memory

Remain excluded until a separate design can define bounded state, licensing of any
training material, and a measurable musical benefit. A future proposal must distinguish
motif memory from ordinary U-1 recipe history, define forgetting/reset controls, and
prove that state never grows with song duration. No implementation implied here.

### Localization

Start only after strings from `describeRecipe`, parameter help and status messages are
centralized in stable message IDs. Ship English as the reference locale; translated
strings must preserve parameter names/units and fit minimum-size layouts. Locale changes
are message-thread-only and do not alter parameter/state serialization. Validate with
pseudo-localization, long strings, keyboard navigation and screen readers.

### Preset sharing service

Default remains offline. Any later service requires explicit opt-in for each network
operation, privacy/data-retention review, authentication and signing of downloaded
preset data. Downloads are data-only, size-limited, schema-validated and quarantined
before import; never execute code or silently replace local presets. No service endpoint
or account design is assumed by this specification.

### AAX / AUv3 / iOS

Do not start before licensing, distribution, host and hardware requirements are
confirmed. AAX needs the applicable Avid SDK/licensing and native host validation;
AUv3/iOS requires a separate touch/mobile UI, sandboxed persistence and iOS host
acceptance. A future platform brief must list exact supported formats, minimum OS,
signing, SDK access and release owners before code or build-matrix changes.
