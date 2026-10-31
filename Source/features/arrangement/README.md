# Arrangement models

`SectionTracker` and `Modulators` are allocation-free, JUCE-free algorithms with
one audio-thread owner. `ContextState` feeds fixed 0.5-second input frames and
publishes a display snapshot. `StageArrangement` applies decision-local factors;
it never bypasses eligibility, protection, voice limits or the existing RNG.

The default C++ model is released version1. The host adapter explicitly supplies
`ModelVersions.arrangement`; old projects without model metadata resolve to1.
Changing version resets derived history and returns to Learning. No learned
boundaries, source audio or private features enter persistent state.

Version1 keeps the released normalization, boundary timestamps, overdue
prediction clamp and density-only Calm behavior. A frozen960-frame trace checks
all public state float bits and boundary indices against release e89aecd.

Version2 compares each new similarity row using the same current feature range
for both raw observations. It reports a section edge at the start of its first
frame. Equal observed periods forecast their next edge while confirmation is
pending through the bounded seven-frame detector delay, then become unknown if
that edge is not confirmed. This predicts recurring structure; it cannot know
an arbitrary unseen section change. Irregular observed periods stay unknown.
The context adapter aligns a forecast to the nearest host bar (or enabled phrase
grid); invalid transport and a snapped target already in the past yield unknown.

Calm in version2 multiplies the complete existing decision probability through
`perf.probabilityMul`. Merely lowering Density cannot meet the specified reduction
because the released probability function has a0.4 floor. Settle still changes
density and Lift still adds density/length. Off, Learning and low confidence
remain identity operations. Version1 retains its original density-only mapping.

Resource limits: one128x128 similarity matrix plus bounded histories; tracker
size is tested at80KiB maximum. No files, clocks, shared references or host APIs
are used by the algorithms. Prepare/reset/model changes clear bounded storage.
See `docs/progress/F-X04-20261031.md` for measured evidence and integration limits.
