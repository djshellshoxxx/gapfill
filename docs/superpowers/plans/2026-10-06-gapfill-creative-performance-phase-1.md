# GapFill Creative Performance Features — Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Verify and finish the existing activity visualizer, then deliver a safe, coherent Surprise Me action that is distinct from Random and Random All.

**Architecture:** Keep the existing real-time visualization FIFO and FillEngine scheduler. First audit the visualizer's frame flags, rendering, and tests; make only the missing status/rejection explanations. Implement Surprise Me as a curated profile selector that writes only allowed creative parameters, then expose it through a distinct editor action and the MIDI action-learning path specified in the design. Do not add DSP work to audio callbacks for Surprise Me.

**Tech Stack:** C++20, JUCE, CMake, custom GapFill test harness, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-06-creative-performance-features-design.md`

## Global Constraints

- Existing parameter IDs are permanent; this phase adds no APVTS parameter IDs.
- Surprise Me must preserve detector source/mode, mix/output, routing, latency, maximum-fill ceiling, and transient/protection parameters.
- No feature may allocate, take a mutex, open a file, or call UI code from processBlock or its callees.
- Changes to settings are applied on the message thread using JUCE parameter notifications.
- Every new control has a tooltip and Help entry, participates in opt-in keyboard navigation, and has an accessible name.
- Deterministic choices use the configured seed and a monotonically increasing request sequence; consecutive Surprise Me presses cannot repeat the same profile.
- No new external dependencies.

## Review Focus

- Older saved projects: no Surprise Me metadata or parameters are required to load; behavior starts from the processor's current seed and sequence.
- Seed boundaries (0 and 9999) and sequence wrap: profile selection stays in bounds and remains deterministic.
- GUI narrow sizes: the new action remains reachable and named without overlapping existing top-bar controls.
- MIDI CC chatter: the action triggers once on a rising crossing and rearms only after the configured release threshold; it does not retrigger on every high-valued MIDI message.
- Profiles with disabled or zero-weight algorithms: every profile remains capable of producing a fill under normal gap conditions and never alters protection settings.

---

### Task 1: Audit and close the activity visualizer requirements

**Files:**
- Modify only if needed: `Source/dsp/VisualisationState.h`
- Modify only if needed: `Source/dsp/FillEngine.cpp`
- Modify only if needed: `Source/gui/WaveformGapView.cpp`
- Test: `Tests/TestDsp.cpp`
- Test: `Tests/TestPlugin.cpp`
- Update: `PROGRESS.md`

**Interfaces:**
- Consumes: `VizFrame::flagEvent`, `flagGapEligible`, `flagFillActive`, `flagProtected`, `flagLookahead`, `VisualisationFifo::push/pop`.
- Produces: a verified mapping from event, eligible-gap, active-fill, protected, lookahead, and spectral-band state to the existing view; if rejection status is missing and can be shown without changing scheduling, a bounded `GapReason` enum carried in `VizFrame`.

- [ ] **Step 1: Add a focused FIFO/state regression test** named `visualisation_fifo_preserves_activity_flags`. Push a frame with all five flags and representative band/confidence values through an enabled FIFO; assert the popped frame preserves all bits and values. Also assert a disabled FIFO emits no frame.
- [ ] **Step 2: Run the focused/full test target before changing rendering.** Configure/build as in Task 4; run `xvfb-run -a build-ci/artefacts/GapFillTests`. Record whether the test exposes a current defect.
- [ ] **Step 3: Compare each state in the spec against the producer and paint path.** Confirm that signal, event, usable gap, protected region, fill lane, lookahead, and spectral lanes have distinct visual explanations. Add a rejection label only for an actual recorded scheduler outcome (for example too-short gap or no free voice); do not infer a reason in the GUI. Rejection display must be decimated/bounded and must not add per-sample work.
- [ ] **Step 4: Implement only the missing visual state or label.** If the current rendering already satisfies the approved spec, make no production-code change and record the audit result in `PROGRESS.md`. If a new `GapReason` is needed, define it in `VisualisationState.h`, set it in `FillEngine::trySchedule`, and render it as a short-lived status label in `WaveformGapView::paint`.
- [ ] **Step 5: Add focused assertions for any new reason mapping.** Cover every enum value, the no-reason state, FIFO transport, and editor-disabled behavior. Ensure the visualizer does not read processor-owned mutable state from the paint thread.
- [ ] **Step 6: Run the full JUCE test executable and repository validation.** Commands are listed in Task 4.

**Dependencies:** None.

---

### Task 2: Add deterministic, constrained Surprise Me profiles

**Files:**
- Create: `Source/CreativeProfiles.h`
- Create: `Source/CreativeProfiles.cpp`
- Modify: `CMakeLists.txt`
- Modify: `Source/PluginProcessor.h`
- Modify: `Source/PluginProcessor.cpp`
- Test: `Tests/TestPlugin.cpp`

**Interfaces:**
- Consumes: `creativeParameterIDs()`, `ParamCache`/APVTS ranges, current `seed` parameter.
- Produces:
  - `struct CreativeProfile { const char* name; std::array<ProfileOverride, N> values; int valueCount; };`
  - `int chooseSurpriseProfileIndex (int seed, std::uint32_t sequence, int previousIndex, int profileCount) noexcept;`
  - `bool applySurpriseProfile (juce::AudioProcessorValueTreeState&, const CreativeProfile&);`
  - `void GapFillAudioProcessor::surpriseMe();`

- [ ] **Step 1: Write tests** named `surprise_profile_selection_is_deterministic_and_avoids_immediate_repeat` and `surprise_profiles_only_modify_allowed_creative_parameters`. Verify identical seed/sequence inputs select the same profile; adjacent requests do not; seed values 0 and 9999 and a wrapping sequence stay in range; every profile has at least one selectable fill algorithm; all values are within APVTS ranges; excluded IDs remain unchanged.
- [ ] **Step 2: Run the tests and confirm they fail** because the profile selector/application functions are not yet defined.
- [ ] **Step 3: Define a fixed set of at least 12 named profiles** in `CreativeProfiles.cpp`. Profiles use only creative targets allowed by the spec. Keep current detector, protection, mix, output, routing, latency, and maximum-fill settings untouched. Profile identity and values are immutable data; selection uses no heap work in DSP.
- [ ] **Step 4: Implement `chooseSurpriseProfileIndex`** using a small deterministic integer PRNG seeded from the current seed and request sequence. Exclude `previousIndex` from the candidate range. Handle profile counts of 0 and 1 without out-of-range access.
- [ ] **Step 5: Implement `applySurpriseProfile` and `GapFillAudioProcessor::surpriseMe`.** Validate every profile override against the named parameter and its legal range before changing anything. Apply all values on the message thread using begin/set/end host gesture notifications. Keep the processor-local sequence and previous profile index; do not write a new seed or alter preset state outside the changed creative controls.
- [ ] **Step 6: Run the profile tests** and verify old serialized states load unchanged.

**Dependencies:** None.

---

### Task 3: Add the Surprise Me action and MIDI action mapping

**Files:**
- Modify: `Source/MidiLearn.h`
- Modify: `Source/MidiLearn.cpp`
- Modify: `Source/PluginProcessor.h`
- Modify: `Source/PluginProcessor.cpp`
- Modify: `Source/PluginEditor.h`
- Modify: `Source/PluginEditor.cpp`
- Modify: `Source/gui/HelpView.cpp`
- Test: `Tests/TestPlugin.cpp`

**Interfaces:**
- Consumes: the bounded profile application from Task 2 and the existing `MidiLearnManager::processMidi/applyPendingChanges` message/audio-thread split.
- Produces: `enum class MidiAction : std::uint8_t { SurpriseMe, NumActions };`, `void startLearningAction(MidiAction action);`, `bool setActionMapping(MidiAction action, int cc);`, `int getMappedActionCC(MidiAction action) const;`, and `int popPendingAction() noexcept` (returns -1 when empty; call on the message thread). Add action mappings to the existing `MIDIMAP` subtree without changing existing parameter mappings.

- [ ] **Step 1: Write tests** named `midi_learned_surprise_action_fires_once_per_rising_edge`, `midi_action_mapping_survives_state_round_trip`, and `surprise_action_does_not_replay_after_state_restore`. Use a CC sequence crossing 64 then releasing below 48; assert exactly one action per crossing and no action on restore.
- [ ] **Step 2: Run the focused tests and confirm they fail** before the action mapping exists.
- [ ] **Step 3: Add the action mapping table and bounded queue** to `MidiLearnManager`. Audio thread detects only threshold crossings and enqueues the fixed action ID; it does not call the processor, editor, or APVTS. Message thread drains and dispatches actions. Persist action-to-CC assignments as named action IDs in `MIDIMAP`, validate CC range 0–127, and retain compatibility with old trees that contain no action child.
- [ ] **Step 4: Add the editor button** with a distinct `SURPRISE` label, tooltip, accessible name, Help entry, keyboard focus support, and a right-click MIDI Learn menu. Place it where it fits the current minimum editor size; adjust responsive layout only as needed. The button calls the same `proc.surpriseMe()` method as the MIDI action.
- [ ] **Step 5: Add UI tests** asserting the action is visible, named, keyboard-reachable when the option is on, and its tooltip/Help text describes constrained creative randomization. Resize the editor to its minimum supported dimensions and assert the button remains within the editor and does not overlap adjacent top-bar controls.
- [ ] **Step 6: Run the full JUCE test executable** and verify MIDI CC delivery through `processBlock()`, message-thread action dispatch, and a save/load round trip.

**Dependencies:** Task 2.

---

### Task 4: Update preflight, documentation, and CI validation

**Files:**
- Modify: `tools/validate_repo.py`
- Modify: `tools/test_validate_repo.py`
- Modify: `README.md`
- Modify: `PROGRESS.md`
- Verify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: the approved feature spec, completed visualizer audit, and Surprise Me control/help text.
- Produces: preflight coverage that requires the feature spec and checks that the Surprise Me control, tests, Help entry, and implementation-status note are present.

- [ ] **Step 1: Add failing preflight tests** named `creative_performance_spec_is_required` and `surprise_me_help_and_control_are_discoverable`.
- [ ] **Step 2: Run `python -m unittest tools/test_validate_repo.py`** and confirm those tests fail before checker updates.
- [ ] **Step 3: Update `tools/validate_repo.py`** using the current required-spec conventions; avoid fragile whole-file string checks for C++ implementation details.
- [ ] **Step 4: Update README and PROGRESS** with the button's user-facing purpose, completed visualizer audit result, and current implementation status. Do not claim host/hardware validation beyond the checks actually run.
- [ ] **Step 5: Run repository checks:** `python tools/validate_repo.py` and `python -m unittest tools/test_validate_repo.py`.

**Dependencies:** Tasks 1–3.

---

### Task 5: Build and release-gate the Phase 1 branch

**Files:**
- Verify: `CMakeLists.txt`
- Verify: `.github/workflows/ci.yml`
- Verify: all files touched by Tasks 1–4.

- [ ] **Step 1: Configure and build locally when the JUCE checkout/toolchain is available:** `cmake -S . -B build-ci -G Ninja -DCMAKE_BUILD_TYPE=Release`, then `cmake --build build-ci --target GapFillTests GapFill_VST3 GapFill_CLAP GapFill_Standalone -- -j2`.
- [ ] **Step 2: Run tests and preflight:** `xvfb-run -a build-ci/artefacts/GapFillTests`, `python tools/validate_repo.py`, and `python -m unittest tools/test_validate_repo.py`.
- [ ] **Step 3: Open/update a draft pull request for the feature branch** so the existing `pull_request` CI trigger runs. Review Linux and Windows builds, strictness-10 pluginval, clap-validator, standalone smoke, and preflight. If CI fails before step 1, report that as infrastructure failure and do not describe the code as fully validated.
- [ ] **Step 4: Review the complete diff** for stable IDs, untouched safety parameters, parameter notification correctness, and help/accessibility wiring. Leave the branch in draft for the next feature plan unless the user separately requests merge.

**Dependencies:** Tasks 1–4.

## Checkpoint: Phase 1 complete

- [ ] Activity visualizer requirements are verified and any missing labels/status are implemented and tested.
- [ ] Surprise Me creates deterministic, non-repeating coherent profiles and preserves all excluded parameters.
- [ ] Button and MIDI action paths share the same processor action and restore safely.
- [ ] Help, tooltips, accessible names, opt-in keyboard navigation, and preflight are updated.
- [ ] Local tests and repository preflight pass; CI outcomes are recorded accurately.
- [ ] Remaining features proceed under the approved order in the spec, each with a focused subsystem plan before implementation.

## Risks and Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Existing CI is currently unreliable before job steps start | Build confidence unavailable | Use local tests where possible; open/update a draft PR for CI; distinguish infrastructure failures from code failures. |
| Surprise profiles accidentally edit safety or mix values | Unsafe/unwanted behavior | Validate allowed parameter IDs before any write; test every profile against a denylist of excluded IDs. |
| MIDI action implementation disrupts parameter learning | Existing mappings stop working | Add action mappings alongside the existing parameter table; retain old MIDIMAP trees and add processor-path regression tests. |
| Top bar becomes crowded | Controls overlap or become inaccessible | Measure current minimum size and use responsive placement in the Creative tab if needed. |
| Large feature roadmap obscures review | Hard to isolate regressions | Keep Phase 1 limited to the visualizer and Surprise Me; plan each independent remaining subsystem separately. |

## Open Questions

- None. The implementation choices above follow the approved spec and the current JUCE/GapFill patterns.
