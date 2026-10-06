# GapFill Creative Performance Features

**Status:** Design specification for review  
**Date:** 2026-10-06  
**Target:** GapFill VST3, CLAP, and Standalone  
**Excluded:** Fill Morphing and Rhythm Memory

## 1. Purpose

Add performance-oriented controls that make GapFill easier to direct, explore, and repeat during production or live use. GapFill remains a gap-aware fill processor: new features influence whether, when, and how fills are selected or rendered inside eligible gaps. They must not turn it into a continuous multi-effect.

The features must preserve GapFill's existing core guarantees: stable parameter IDs, deterministic scheduling from the configured seed and transport position, protection of important events, bounded fill lengths, no audio-thread allocation or locking, and graceful operation when host tempo or transport data is unavailable.

## 2. Current-code findings and compatibility

The master branch already defines 14 fill algorithms, including an algorithm named Freeze. That algorithm is a fill choice; it does not imply a latched capture of recent input. Freeze/Capture below is a separate source-history latch.

The existing Audition Detector parameter is a detector diagnostic. It is not the proposed Fill Audition function.

The existing visualizer already receives event, fill-active, protected, gap-eligible, and lookahead flags through VizFrame and displays the incoming activity and fill decisions. The feature below is therefore an audit-and-completion requirement. The implementation must document what is already present, identify missing labels or states, and avoid duplicating the existing visualizer.

All existing parameter IDs are permanent. New parameters must be appended with new IDs; no existing IDs, meanings, defaults, or ordering may be repurposed. Saved states that omit new fields must load with feature-safe defaults. New recipe and macro-mapping data must be validated before application, using the same atomic state-loading principle as current preset handling.

## 3. Shared audio, state, and interaction rules

- The audio thread owns DSP state. GUI actions reach it through existing parameter atomics or fixed-size, lock-free command/event queues.
- No feature may allocate, take a mutex, open a file, or call UI code from processBlock or its callees.
- Any settings derived from several controls are captured once per block, then remain stable for that block. Scheduler decisions still occur on the existing analysis-hop grid.
- Events sent from audio to UI use bounded, preallocated storage. Overflow drops the oldest or newest UI-only event according to the feature's documented policy; it never delays audio.
- Feature modes are host-automatable when exposed as parameters. Trigger actions use momentary parameters or the existing MIDI-learn route and must not retrigger from stale restored values.
- Macro mappings, locked recipes, and user recipe records are validated, bounded, and persisted in plugin state. Audio history and temporary preview buffers are never serialized.
- With no valid playhead, transport-dependent modulation and phrase bias turn off or hold their neutral value. They must not infer tempo from wall-clock time.
- Protection remains authoritative. No recipe, personality, macro, envelope, mistake, preview, or capture action may extend a fill beyond the current allowed gap, configured maximum, or next protected event.
- Every new control has a tooltip and Help entry. It participates in opt-in keyboard navigation, has a useful accessible name, and follows current visual and reduced-motion conventions.
- Where a control represents a setting, it has a stable parameter ID and is host-visible. UI-only editing data lives in versioned plugin state, not in ad hoc editor-only state.

## 4. Feature specifications

### 4.1 Before/after activity visualizer

**Goal:** Make it clear why GapFill accepted or rejected a candidate region and what it did there.

**Required display:** Show incoming activity, detected events, gap-eligible intervals, scheduled/active fills, protected intervals, and lookahead information. Keep spectral-band confidence available in spectral mode. Provide a compact legend or equivalent labels; distinguish a region that is merely quiet from a region accepted as fillable.

**Current baseline:** VizFrame already carries event, fill-active, protected, gap-eligible, and lookahead flags, plus energy/confidence/band-confidence/duck values. WaveformGapView already renders this stream. First audit each displayed state against the flag producer and existing tests. Add only missing legend, visual distinction, or status explanation. Do not add another visualization pipeline or increase audio-thread work when the editor is closed.

**Acceptance:** Each state has a visible, distinguishable indication; disabling/closing the editor disables visualization production as it does now; the visualizer remains correct after resizing and spectral-mode changes; a focused test covers every flag/state mapping.

### 4.2 Surprise Me, But Safely

**Goal:** Create a playable, coherent setup in one action without independently randomizing every parameter.

**Behavior:** Generate one constrained configuration from a curated set of compatible profiles. A profile may select a fill personality, algorithm-weight shape, activity/density, complexity, timing divisions, and a restrained set of creative modifiers. It must preserve the user's detector source/mode, input/output mix, routing, latency settings, maximum-fill ceiling, and transient/protection settings. It must avoid zero-weight or contradictory configurations that cannot generate a fill.

Each press produces a different valid profile, with deterministic seeded generation and no immediate repeat. The action is one undoable state change where host/editor undo support permits. It uses a distinct button and label from Random and Random All.

**Acceptance:** Generated states pass range and coherence checks; protected/mix/routing settings remain unchanged; repeated presses change the creative configuration without repeating the prior complete signature; saved/reloaded state preserves the chosen result.

### 4.3 Performance Macros

**Goal:** Control several creative parameters with a small set of performance-friendly knobs.

**Controls:** Four macro values with default names Activity, Chaos, Space, and Destruction. Each macro has a bounded set of editable target mappings with minimum, maximum, and response curve. Defaults must be musically useful and documented. Users may rename macros and edit mappings.

**Signal path:** Macro values and mappings produce an effective parameter snapshot; they do not overwrite the underlying parameter values. Host automation of a base parameter and macro value must combine predictably. Define the combination as normalized-domain modulation with explicit clamping, and make the effective value visible in the editor. One target may be assigned to more than one macro only if contributions combine by a documented rule.

**Persistence:** Macro controls are automatable with stable, new IDs. Mapping definitions and names are plugin-state data. Invalid target IDs or out-of-range mappings are rejected atomically. All four macros default to useful mappings and can be reset.

**Acceptance:** Macro changes move each mapped target through its range and curve, never exceed target limits, preserve base parameter values, respond without zipper noise, survive save/load, and work through MIDI Learn and automation.

### 4.4 Fill Personality modes

**Goal:** Let users select a coherent behavior instead of tuning every algorithm weight individually.

**Modes:** Subtle, Percussive, Glitchy, Psychedelic, Dub, Aggressive, Ambient, and Unhinged.

**Behavior:** A personality is a non-destructive overlay on effective algorithm weights and selected creative ranges such as density, complexity, pitch range, reverse tendency, and stereo movement. It must not rewrite the stored base weights or protection settings. The user can still adjust existing controls; define and display how those controls combine with the personality overlay. Unhinged increases variety and surprise while respecting all duration and protection limits.

**Acceptance:** Each mode has a documented, audibly distinct profile; the same seed and transport reproduce the same decisions; switching modes does not erase user weights; all modes remain bounded and safe.

### 4.5 Freeze / Capture

**Goal:** Let a performer latch recent main-input material as the source for subsequent fills while live input continues.

**Behavior:** A momentary or latched Capture control freezes a configurable recent history window into a preallocated capture buffer. While capture is active, fills read from that snapshot; analysis, dry audio, protection, and live pass-through continue to use current input. Provide recapture and release actions. If insufficient history exists, keep the current source behavior and show a non-blocking status message. A host restore must never serialize audio samples or accidentally restore an active latch.

**Engineering:** Allocate storage during prepareToPlay for the maximum supported capture length. Capture copying must be bounded and synchronized without a blocking lock. Use a generation/index handoff or another race-safe scheme that does not make the GUI touch audio memory. Define behavior across sample-rate changes, reset, transport jumps, and releaseResources.

**Acceptance:** Captured fills remain sourced from the latched material while live input changes; release returns to live history; capture cannot read unwritten samples; no allocations/locks occur in processBlock; sample-rate changes and reset cannot expose stale or invalid memory.

### 4.6 More Like That

**Goal:** Bias upcoming fills toward the character of a fill the user liked while preserving variation.

**Behavior:** A Like action uses the latest completed fill recipe: algorithm, duration class, band, rhythmic division, pitch direction/range, modifier family, and stereo behavior. For the next four eligible fills, apply bounded weighting toward similar characteristics. It must not replay identical audio or force an unavailable algorithm. A new Like action replaces the current bias window and starts it again. Bias expires after four successful fill starts, not after rejected gaps.

**Acceptance:** Recipe capture is deterministic; bias changes candidate selection measurably but does not guarantee exact repetition; unavailable/disabled bands fall back safely; the bias count advances only on started fills and resets on transport discontinuity according to the documented rule.

### 4.7 Fill Intensity Envelope

**Goal:** Let each fill evolve across its duration instead of holding one character from start to finish.

**Controls:** Shape choices Flat, Rise, Fall, Rise+Fall, and Random Walk, plus an amount control. The envelope is evaluated from normalized voice age, not wall-clock time.

**Behavior:** Shape modulates a bounded set of fill attributes such as intensity, wet contribution, modulation depth, or grain density. Define the mapping and amount so the default is Flat/zero change. Random Walk is seeded per fill recipe and remains deterministic. Envelope movement must be smoothed and must not alter protection or the maximum-fill boundary.

**Acceptance:** Each shape has unit tests at start/mid/end points; random walk is repeatable; modulation remains within safe bounds; Flat reproduces the pre-feature sound within existing test tolerances.

### 4.8 Fill Lock

**Goal:** Turn a good generated fill into a repeatable recipe.

**Behavior:** Lock stores the most recently completed fill recipe and uses it for later eligible gaps until unlocked. Lock stores decisions, not audio. If a recipe is incompatible with the current gap length, fill mode, or enabled spectral bands, adapt or skip according to an explicit safe fallback. Protection, current wet level, and maximum-fill rules remain live and authoritative. Provide a visible locked indicator and a clear unlock action.

**Persistence:** Persist the recipe and lock state, but validate both on load. A stale/corrupt recipe must not prevent loading the rest of a valid preset. Restore must not replay an audio trigger.

**Acceptance:** Lock holds algorithmic character across eligible gaps; unlocking returns to normal selection; unsuitable gaps do not overrun or mask the next event; recipe state survives save/load.

### 4.9 Fill Audition

**Goal:** Let users hear a representative fill immediately while designing settings.

**Behavior:** An Audition Fill action asks the processor for one one-shot preview from recent SourceHistory, without waiting for a naturally detected gap. The preview must use the current creative settings and obey output, wet/dry, max-length, and transient-safety limits. It must not change detector state, count as a natural gap, alter deterministic gap indexing, or be recorded as a natural fill in More Like That history. A separate, short-lived UI status identifies preview playback.

**Real-time behavior:** Send a bounded preview command to the audio thread. If the host is not processing, show a clear status and retain or cancel the request according to a documented policy; do not start a private audio device in plugin mode. Standalone may use its already-open device. Repeated clicks coalesce to one pending preview.

**Acceptance:** Preview can run during active host playback, produces no audio-thread allocation or locking, does not change natural fill decisions, and cancels/fades safely on bypass, reset, or transport teardown.

### 4.10 Fill History

**Goal:** Expose recent fill decisions so users can compare and reuse them.

**Behavior:** Keep the most recent eight natural fill recipes in a compact history strip, newest first. Each entry displays concise attributes such as algorithm, division/duration, pitch direction, and band. Selecting an entry offers Audition, Lock, More Like That, and Save Recipe actions. History records decisions only, never PCM audio. Preview fills are not inserted into the natural-history list.

**Threading/state:** Audio thread publishes fixed-size recipe records to the editor through a bounded queue. The editor owns the visible ring and recipe menu. Persist the eight recipe records only if they are validated and small; do not persist sample buffers. On overflow, discard the oldest visible item.

**Acceptance:** Eight entries display correctly, new fills roll history, preview is excluded, each action uses the selected entry, and history is safe when the editor is closed or recreated.

### 4.11 Density Breathing

**Goal:** Vary fill activity over musical time without manual automation.

**Controls:** Enable, cycle length in bars (1, 2, 4, or 8), depth, and curve (sine, rise/fall, or pulse). Defaults are disabled and neutral.

**Behavior:** Compute a deterministic modulation from host PPQ and time signature, then apply it to effective density only. Base density remains unchanged. On invalid PPQ or stopped transport, modulation returns to neutral. On loop wrap or transport jump, phase follows the host position so playback remains reproducible.

**Acceptance:** Repeated playback from the same PPQ produces the same density curve; no-transport playback equals disabled behavior; depth never produces out-of-range density; parameter automation remains responsive.

### 4.12 Musical Phrase Awareness

**Goal:** Bias fills toward larger phrase endings using host musical position.

**Controls:** Phrase length Off, 2, 4, 8, or 16 bars, and phrase bias amount.

**Behavior:** With valid PPQ and meter, calculate position within the configured phrase and increase fill probability within the final bar by a bounded amount. The feature biases selection; it never schedules a fill where no eligible gap exists. With invalid meter/PPQ, disable the bias. Phrase phase derives from host position, not a private bar counter, so loops and seeking are repeatable.

**Acceptance:** Correct behavior at phrase boundaries and loop wraps; no effect when Off or playhead data is invalid; same position and seed produce the same decisions; protection and gap eligibility remain unchanged.

### 4.13 Controlled Mistakes

**Goal:** Add rare deliberate variations that make fills less predictable while retaining hard musical and signal-safety limits.

**Control:** Mistakes amount from zero to maximum. At zero the behavior is identical to current GapFill.

**Behavior:** At higher values, a deterministic per-gap roll may apply one bounded variation such as playback-rate wobble, octave displacement, missed repeat, abrupt-but-faded tape-stop, or stereo bounce. The distribution is weighted by amount and cannot stack multiple mistake types on one fill unless a later version explicitly specifies it. Variations must be audibly bounded, seeded by the existing deterministic decision inputs, and represented in history/lock recipes where relevant.

**Hard limits:** No spill into the next protected event, no bypass of transient protection, no extension beyond the maximum fill or available gap, no discontinuity that produces a click, and no non-finite output. If a mistake cannot fit the current gap, skip it.

**Acceptance:** Zero amount is regression-equivalent; nonzero modes are reproducible; output remains finite and bounded; no mistake crosses the configured/protected boundary.

## 5. Parameter and state compatibility

- Keep every current parameter ID and its meaning unchanged.
- Add new automatable parameters only with unique, stable IDs and current version-hint conventions.
- Keep macro mappings, macro labels, recipes, and other structured UI state in a versioned subtree with strict size/count/range limits.
- Older states missing the subtree load with all new modes disabled or neutral.
- Malformed new data is ignored or rejected atomically according to the existing state-validation contract; it must never partially mutate live parameter state.
- Momentary actions must be cleared during restore and must not fire because a saved parameter was true.

## 6. Verification requirements

For every feature, add focused tests for its acceptance criteria and update repository preflight/spec checks so the specification stays discoverable. Run the full JUCE suite and repository validation after implementation. Build VST3, CLAP, and Standalone where the current CI/toolchain permits. Run pluginval and clap-validator gates when available. Record unavailable DAW, hardware, or CI checks without claiming they passed.

Cross-feature tests must cover: feature combinations with transient protection; parameter/state migration from a pre-feature state; save/load with macros and recipes; automation/MIDI Learn for exposed controls; transport seek/loop and invalid playhead; editor closed while audio runs; reduced-motion/keyboard operation; and all sample-rate/block-size tests already required by the project.

## 7. Completion criteria

The spec is implemented only when every feature has an explicit implementation status, focused regression coverage, Help/tooltips, state compatibility, and a verified build path. The visualizer audit may conclude that no code change is necessary if its current behavior fully meets the requirements above and tests confirm the mapping. Hardware/DAW acceptance remains a separate release-validation activity as defined in docs/VALIDATION.md.
