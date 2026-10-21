# 10 - Live, creative and effects specification

Status (2026-10-21): requirements approved for implementation (the Gate A/B sequencing gate in `08` is lifted). This document is the requirements overview; the implementable specs (parameter IDs, ranges, defaults, state, tests, work packages) are in `12-SPEC-BACKLOG-AND-NEW.md`, which wins on any conflict. Nothing here is implemented yet. Differences resolved in `12`: scenes hold up to 64 per bank (quick page of 8) instead of 8; the effects reuse existing `Crush`/`Comb`/`Reverse`/`Granular` algorithms only as separate cards, never replacing them.

## 1. Capability map and build order

| Module ID | Responsibility | Depends on |
|---|---|---|
| `explain` | Explain detector decisions and display a gap/event timeline | Stable event/reason data; F0-2/F0-4 |
| `audition-recipes` | Preview, retain, compare, lock, and export generated fills | E-1, F0-3/F0-4/F0-6/F0-9 |
| `live-performance` | Macros, scenes, quantized changes, momentary controls, MIDI feedback, panic/recovery | F0-5/F0-9; `audition-recipes`; host QA |
| `musical-context` | Phrase, groove, tension, and call/response behavior | Transport correctness; `explain`; `live-performance` |
| `rhythmic-fx` | Ratchet, bitcrush, tape destruction, resonator and delay effects | Fill voice/effect module seam; golden and CPU baselines |
| `spectral-fx` | Formant shift and spectral freeze/morph | Stable spectral processor and memory/CPU budget |

Build order: verified baseline → `explain` → `audition-recipes` → `live-performance` → `musical-context` → `rhythmic-fx` → `spectral-fx`. The transport-jump issue and Windows editor automation investigation must be resolved or explicitly accepted before live scheduling is treated as release-ready.

## 2. Shared product and engineering contract

These requirements apply to every feature below:

- At neutral/default settings, existing output, parameter IDs, saved state, latency/PDC, host behavior, and presets MUST remain compatible. New parameters use append-only IDs and versioned state migration.
- Audio-thread work MUST be bounded and allocation-free. No locks, file access, logging, heap allocation, or unbounded loops may run in `processBlock` or a function it calls.
- Any queued command, event, history, or MIDI message MUST have fixed capacity, defined overflow behavior, and a test for full/empty/wraparound conditions.
- Random decisions MUST remain reproducible from seed and musical position. UI-only actions MUST not consume the audio engine's random stream.
- Effects MUST process generated wet material only unless a clearly named global routing option says otherwise. Existing dry audio and transient/next-event protection remain intact.
- Feedback, resonance, output level, and modulation MUST be bounded. Invalid host values, NaN/Inf, denormals, transport loss, sample-rate changes, and plugin reset MUST fail safely.
- Every feature needs unit tests, state round-trip/migration coverage where state is saved, and golden-output regression checks. Cross-platform acceptance follows `07-QA-AND-RELEASE.md`; real DAW and hardware behavior cannot be inferred from CI alone.
- New UI controls MUST be discoverable, keyboard accessible, screen-reader labeled, automatable only when the parameter is stable and meaningful, and covered by at least one supported window-size/layout test.

## 3. Existing spec items this addendum builds on

These already have canonical requirements. Implement from the cited section and use this addendum only for the integration/acceptance additions shown here; do not create duplicate parameter IDs or competing behavior.

| Capability | Canonical specification | Additional integration required here |
|---|---|---|
| Fill audition | `03-SPEC-ESSENTIALS.md` E-1 | Preview is isolated from committed history and never steals or changes the live recipe. |
| Skip explanations | `03-SPEC-ESSENTIALS.md` E-3 | Feed the timeline below from the same reason/event record; never infer a reason from UI state. |
| Fill history and recipes | `05-SPEC-USABILITY-VALUE.md` U-1 | Add level-matched A/B compare and safe history navigation; history remains bounded. |
| Wet fill export | `05-SPEC-USABILITY-VALUE.md` U-2 | Export must contain wet contribution only, carry sample rate/channel metadata, and keep disk work off the audio thread. |
| Fill Lock, personalities, macros, intensity envelope, density breathing, phrase logic, per-band type, Capture v2, More Like That, Evolve | `04-SPEC-CREATIVE.md` C-1 through C-11 | Integrate with the live scene model, status display, and effect chain without changing existing semantics. |
| UI scaling, info/status, first-run guide, simple/advanced, preset browser | `05-SPEC-USABILITY-VALUE.md` U-4 through U-9 | Include the new performance/status controls in the same responsive layout and help registry. |

## 4. Feature specifications

### 4.1 Gap timeline and actionable skip reasons (`explain`)

**Objective.** Make the detector and scheduler understandable without requiring users to guess why a fill was or was not generated.

**Behavior.**
- Present a scrolling timeline with detected transients, candidate gaps, protected regions, accepted fills, skipped candidates, and preview-only audition events.
- Each skipped candidate displays one authoritative reason from the engine's reason catalogue, with a concise explanation and (when applicable) a control the user can adjust.
- Show signal/gap confidence and the active protection window as separate visual layers. Confidence must not be presented as a guarantee that a fill will occur.
- Support pause/freeze of the visual display for inspection; freezing the display must not freeze audio analysis.
- Provide a reduced-motion mode, accessible text descriptions, and a legend that can be hidden without hiding essential state.
- Timeline history is bounded (default target: last 8 seconds, configurable only if memory remains bounded). When events are dropped, indicate that the view is incomplete.

**Data and threading.**
- Publish immutable, fixed-size event records through a bounded lock-free queue. Include sample/PPQ position, event type, reason enum, confidence, and generation ID.
- The engine is the sole source of skip reasons. GUI code MUST NOT duplicate detector thresholds or reverse-engineer state.
- Define overflow as dropping oldest display-only events and setting an overflow flag; overflow MUST never change audio decisions.
- Paint from a UI-owned snapshot at a capped refresh rate; no allocations or locks on the audio thread.

**Verification.**
- Unit test every reason enum and reason-to-advice mapping, event ordering, overflow, wraparound, transport reset, stopped transport, and variable buffer lengths.
- Verify screenshots at minimum/default/large window sizes and with reduced motion and screen reader labels.
- Acceptance: a user can identify why a representative candidate was skipped from the timeline and the explanation matches the engine event record exactly.

### 4.2 A/B audition and level-matched recipe compare (`audition-recipes`)

**Objective.** Help users compare two fill recipes fairly and recover a preferred one.

**Behavior.**
- Provide A/B snapshots of recipe/parameter state; switching sides MUST be click-free and MUST not mutate host automation values or the saved base state.
- Level-match only the audition path, using a bounded loudness estimate; show when matching is active and the applied gain. Include a bypass for level matching.
- Preview through the bounded audition voice specified by E-1. Preview start/stop, replacement, and cancel behavior must be deterministic.
- Provide Undo/Redo-safe state changes and integrate with U-1 history when implemented. Do not store unbounded audio in the history.
- A/B state is per plugin instance and is saved only if consistent with the project's state-size and migration rules.

**Verification.**
- Test recipe and plugin-state round trips, A/B toggling, undo/redo, host automation isolation, level-match bounds, silent input, and rapid preview replacement.
- Golden tests confirm no output change when audition is inactive and A/B is not being switched.
- Acceptance: users can compare two recipes at matched preview loudness and return to either without losing the working recipe.

### 4.3 Live performance system: scenes, macros, quantized changes, controls, recovery (`live-performance`)

**Objective.** Make GapFill safe and expressive on stage and in real-time recording sessions.

**Scenes.**
- Support a bank of up to 64 scenes per instance with an 8-slot quick page (revised by `12` F-L07/N-VAL). A scene stores only approved, versioned performance parameters and recipe/preset identity; it MUST NOT store audio buffers or hidden host state.
- Scene selection supports immediate, next-beat, next-bar, and next-phrase modes. If transport/tempo is unavailable or invalid, show the condition and use an explicitly configured immediate fallback.
- Pending scene changes are visible and cancellable. A scene transition is sample-safe or uses a short click-free crossfade; no transition may cause an unbounded CPU spike.
- Scene load must preserve global output safety, dry/wet mix, latency, bypass, and the panic state unless the user explicitly mapped a safe parameter for that purpose.

**Macros and controllers.**
- Provide four assignable macro controls with per-target min/max, polarity, curve, smoothing, and a neutral reset. Support MIDI Learn, stable host automation IDs, and persistence.
- One incoming CC maps to at most one target unless the user explicitly chooses a macro; mappings have deterministic conflict resolution and a visible duplicate warning.
- Hardware feedback MAY be supported for controllers that advertise it. Missing/disconnected devices MUST not block audio or invalidate stored mappings.
- Macro targets write to the effective-settings layer; they do not overwrite base knob values. Display the effective value separately.

**Momentary/latch commands.**
- Support press-and-hold and latch modes for stutter, freeze/scrub, reverse, tape stop, and scene recall where the effect is supported.
- Commands enter the engine through a bounded queue. Define behavior for a lost note-off: timeout to safe release, with timeout duration configurable only within a safe range.
- MIDI command learning MUST be distinguishable from regular parameter automation.

**Safety and status.**
- Provide a panic/instant-safe action that cancels queued previews and active fills, disables performance FX, and returns the wet path to silence or the configured safe state with a click-free transition. It MUST NOT interrupt or spike the dry signal.
- Provide a separate “last good scene” recovery action that restores the last successfully applied scene, not an unvalidated pending scene.
- Show host-reported latency/PDC and clearly labeled performance telemetry. CPU display is an estimate or sampled measurement, never a host-independent claim of exact CPU use.
- Panic is always available, keyboard reachable, and has an optional MIDI mapping. Its action is never delayed by quantization.

**Verification.**
- Test scene serialization/migration, quantized scheduling at tempo changes and loops, no-transport fallback, scene queue overflow, MIDI disconnect/reconnect, stuck-note timeout, host automation interaction, panic during each effect, and recovery from invalid scene data.
- Manual QA in at least one real DAW per supported desktop OS plus hardware MIDI where available.
- Acceptance: a performer can map a controller, change scenes on a bar, cancel a stuck effect immediately, and preserve stable output through transport/device loss.

### 4.4 Musical context: phrase, groove, tension/release, call/response (`musical-context`)

**Objective.** Make fills follow the performance's phrasing and feel, rather than only reacting to local energy.

**Phrase awareness.**
- Allow explicit 4/8/16-bar phrase length and bar offset. Use host bar/beat data when valid; never silently infer an incorrect meter or phrase.
- Prefer eligible fills near configured phrase boundaries. Weighting is bounded and user-controlled; phrase awareness never forces a fill into a protected or ineligible gap.
- Handle loop wrap, seek, stop/start, tempo changes, meter changes, and missing playhead data without generating duplicate or stale events.

**Groove inheritance.**
- Estimate stable onset offsets from recent input within a bounded analysis window. Display confidence and allow a manual amount from 0% (straight) to 100% (full measured offset).
- Do not treat transport jitter or variable host block sizes as musical swing. Only apply groove offsets to fill subdivisions, not to host timing or dry audio.
- Provide reset/learn controls and a bypass that restores current baseline scheduling.

**Tension/release curves.**
- Provide a small set of editable multi-bar curves for density, intensity, width, and effect-send amount, ending at a defined resolution point.
- Curves are deterministic, transport-aligned, and cancellable. Parameter values remain within established ranges and are smoothed.
- User curves are bounded in count and size and are stored in versioned plugin state.

**Call and response.**
- Retain a compact, bounded descriptor of recent accepted recipes (algorithm family, rhythm, density, pitch/space attributes), not raw audio.
- Offer a response mode that selects complementary or contrasting settings in later eligible gaps. Users can set response length and strength and inspect/lock the result.
- If confidence in the motif is insufficient, fall back to ordinary deterministic selection and show that no response was generated.

**Verification.**
- Test deterministic scheduling at fixed seed/PPQ; true seeks/loops; host tempo/meter changes; confidence thresholds; bounded histories; and no false fills in protected gaps.
- Test comparisons against a straight-timing reference and ensure only wet subdivisions move.
- Acceptance: phrase/groove features improve intentionality without changing dry timing, creating surprise fills at transport jumps, or violating user density/protection controls.

### 4.5 Wet-only export and render handoff (`audition-recipes`)

**Objective.** Let users print GapFill's contribution for editing, resampling, and mixing.

**Behavior.**
- Record only generated wet output into a fixed-capacity buffer with explicit maximum duration and channel count. Record start/end markers and sample rate.
- Provide offline save/export and, where host APIs support it safely, a UI drag/drop handoff. File creation, encoding, hashing, and drag preparation occur on a worker thread.
- Export metadata includes sample rate, channel layout, tempo/PPQ range when available, seed, selected scene/recipe, and plugin version. Metadata excludes host project paths and unrelated user data.
- Overflow, disk errors, cancellation, and unsupported drag/drop produce a clear UI result and preserve the current plugin state.
- No export action changes latency/PDC, dry output, or the live recipe.

**Verification.**
- Test exact wet-only content with deterministic fixtures, stereo/mono layouts, maximum-duration boundary, buffer wrap, cancellation, disk failure, and sample-rate change.
- Acceptance: exported audio aligns to the captured event range, contains no dry signal, and can be imported into a DAW at its recorded sample rate.

## 5. Effect specifications

All effects are wet-fill modules with a neutral bypass state, bounded processing, stable parameter smoothing, and deterministic settings. The first implementation should expose each as a module that can be selected in a fill recipe; do not add a global serial effect chain that changes every fill unless separately specified.

### 5.1 Rhythmic ratchet (`rhythmic-fx`)

- **Purpose:** Repeat selected fill material in tempo-aware subdivisions for rolls, stutters, and accelerating or decelerating runs.
- **Controls:** Enable/weight, division (host-synced values plus free-time), repeat count, acceleration curve, gate length, and wet level. Default is off.
- **Behavior:** Operates only inside the eligible fill window; subdivisions are clipped before the next protected transient. If no valid transport exists, use the user's free-time setting or skip the effect according to a visible fallback.
- **Bounds:** Maximum repeat count and minimum interval are compile-time bounded. No overlap can exceed voice count or allocate buffers per repeat.
- **Tests/acceptance:** Verify exact repeat positions at tempo boundaries, variable block sizes, loop/seek behavior, protection clipping, deterministic output, and a neutral golden match.

### 5.2 Gap-only bitcrush (`rhythmic-fx`)

- **Purpose:** Create digital grit by reducing bit depth and sample rate on generated fill audio.
- **Controls:** Bit depth, sample-rate reduction factor, optional tempo-synced hold pattern, pre/post tone, and wet amount. Default is bypass.
- **Behavior:** Quantization is deterministic and independent of platform rounding differences. Add anti-alias filtering or define a documented, intentional aliasing character; avoid changing dry audio.
- **Bounds:** Clamp values to stable ranges; use denormal-safe state; mix/output protection prevents full-scale overload.
- **Tests/acceptance:** Test impulse, sine, silence, DC, Nyquist-adjacent content, NaN/Inf recovery, channel separation, deterministic hashes, CPU ceiling, and bit-identical neutral bypass.

### 5.3 Tape destruction expansion (`rhythmic-fx`)

- **Purpose:** Expand Tape Stop into controllable tape degradation for fills.
- **Controls:** Wow, flutter, saturation, dropout density/depth, splice/cut frequency, and speed instability, each with macro amount and a safe mix.
- **Behavior:** Effects use preallocated modulation state and deterministic seeded dropout positions. Tape Stop speed envelope remains its own algorithm; degradation may be layered on it or used as a modifier, with explicit UI distinction.
- **Bounds:** Prevent unstable DC, runaway gain, clicks at dropout/splice edges, and unbounded modulation. Dry remains untouched.
- **Tests/acceptance:** Test deterministic dropout patterns, parameter ramps, silence/noise floor, extreme settings, state migration, CPU load, and click-free engage/release.

### 5.4 Comb-filter resonator (`rhythmic-fx`)

- **Purpose:** Turn captured transients into pitched, metallic resonances.
- **Controls:** Tuned frequency or MIDI note, decay/feedback, damping, stereo spread, pitch/key mode, and wet level.
- **Behavior:** Manual tuning is always available. Optional scale constrain uses explicit user root/scale; no automatic key guess in this module. Resonator is bounded and resets safely on sample-rate change.
- **Bounds:** Feedback remains below instability; resonance gain and low-frequency accumulation are limited. No state-dependent unbounded tail.
- **Tests/acceptance:** Verify tuning error limits, feedback stability at extreme values, stereo behavior, silence/denormal handling, sample-rate changes, and transient protection.

### 5.5 Frequency-shifted delay (`rhythmic-fx`)

- **Purpose:** Add inharmonic metallic or sci-fi echoes distinct from pitch-preserving delay.
- **Controls:** Shift amount in Hz, direction, delay time/division, feedback, damping, stereo offset, and wet mix.
- **Behavior:** Frequency shifting is applied per delay repeat, with optional host sync. Expose the sign and unit clearly. Disabling the module flushes or fades the tail predictably.
- **Bounds:** Limit delay memory and feedback; anti-aliasing and DC control are required. Delay buffers are allocated in prepare, never per block.
- **Tests/acceptance:** Test zero-shift equivalence to documented pass-through behavior, positive/negative shift, feedback stability, aliasing at high shifts, tempo change, sample-rate change, and buffer limits.

### 5.6 Dynamic dub feedback (`rhythmic-fx`)

- **Purpose:** Create evolving filtered stereo echoes from generated fills.
- **Controls:** Delay division/time, feedback, low/high filter, filter movement, saturation, stereo/ping-pong mode, and wet amount.
- **Behavior:** Filter motion is bounded and can follow a deterministic envelope. Sync follows valid host tempo; free-time fallback is explicit. Tail can be cleared by panic or scene transition without a discontinuity.
- **Bounds:** Hard feedback ceiling, DC/low-frequency control, maximum tail duration, and output safety limiter. Parameter automation is smoothed.
- **Tests/acceptance:** Test impulse decay envelopes, max feedback, stereo routing, automation ramps, no-tempo behavior, panic/tail clear, and worst-case CPU.

### 5.7 Formant shifting (`spectral-fx`)

- **Purpose:** Change the spectral envelope of captured fill material independently from its fundamental pitch, producing vowel-like, vocal, or resonant character.
- **Controls:** Formant shift, spectral tilt, vowel/shape selector or user macro, preservation amount, and wet mix.
- **Behavior:** Operates on the fill voice's captured material; it does not imply speech recognition or intelligible voice synthesis. Choose a method with fixed latency or report any added latency precisely.
- **Bounds:** Keep spectral buffers preallocated and bounded; prevent unstable peaks and protect against transient smearing. Provide a low-CPU quality mode if measurements justify it.
- **Tests/acceptance:** Test pitch preservation, channel coherence, transient timing, silence/noise, extreme formant settings, latency report, CPU/memory limits, and neutral bypass.

### 5.8 Spectral freeze and morph (`spectral-fx`)

- **Purpose:** Sustain a captured spectral frame and morph it into a textured fill tail for breaks and transitions.
- **Controls:** Capture/hold, freeze length, spectral decay, pitch shift, formant, smear, morph target, stereo width, and wet level.
- **Behavior:** Capture commands use a bounded snapshot handoff; no UI thread reads a buffer while the audio thread writes it. Morph interpolation is deterministic and smoothed. Release and panic produce a bounded fade.
- **Bounds:** Fixed maximum frame count and memory budget; no runtime FFT-plan allocation; stable phase handling; no uncontrolled feedback.
- **Tests/acceptance:** Test repeated capture/release, thread-race stress, sample-rate changes, memory ceiling, phase continuity, latency reporting, panic, and cross-platform golden tolerance.

## 6. Prioritization and release gates

1. Resolve transport timing and editor automation issues; complete the current release QA matrix.
2. Implement the canonical E-1/E-3 and U-1/C-5 workflow with the timeline and recipe compare integration.
3. Implement live safety first (panic, pending-action cancellation, last-good-scene recovery), then macros and scenes.
4. Add phrase/groove/tension behavior only after transport correctness is verified.
5. Add rhythmic FX in this order: ratchet, bitcrush, tape degradation, resonator/delays.
6. Begin spectral FX only after profiling proves the rhythmic modules fit the CPU/memory budget.

Each feature must receive its own ticket, tests, implementation, and cross-platform validation. Do not ship a new effect solely because its UI or parameter exists; the DSP path and its neutral behavior must pass the tests above.
