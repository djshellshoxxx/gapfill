# F-L09 Scramble completion — 2026-10-31

The old Groove setup stage only supplied shifts for Stutter and Delay, and Scramble ignored shifts even when explicitly supplied. This fixes both missing hooks under an independent saved Groove model version, with no new host parameter or recipe flag.

ModelVersions schema2 adds Groove in the top byte of the atomic uint32 snapshot. Strict schema1 input keeps its three previous choices and sets Groove1; absent/malformed metadata selects all legacy models. Fresh instances use all2. Groove1 keeps saved active Scramble sessions straight, while Groove2 explicitly applies inherited timing to synced Scramble slices. Options shows all four versions and offers the existing undoable upgrade. Scenes own Groove metadata under Musical scope independently from Cloud/Fx.

Scramble limits each synced slice to half its usable captured material. Consequently a requested125ms period can actually become62ms with120ms of source since the last event. Shared pure timing helpers retain the original0.32s capture ceiling, source offset, rounding and clamp. Context builds the warp using that actual period. Rendering derives the absolute slice ordinal and corrects it by at most one adjacent boundary, preserving source order/read rate while moving output slice starts. The shifted duration controls boundary fades; fill onset, output deadline and plugin PDC do not move.

Validation with pinned JUCE8.0.15 on Linux x86_64/GCC Release:

- Original acec0a0 FillVoice and ContextHooks compiled independently with the corrected normal125ms fixture: two new tests fail (missing setup and unchanged audible boundary), while the off/zero-warp test passes.
- Fixed production-context/voice component:4passed,0failed. Tests measure normal125ms and source-clamped62ms boundaries, early/late±0.3step shifts, fill onset/deadline, zero/off bit identity, blocks1/13/64/1000 and no allocation during start/render.
- Independent original FillVoice comparison:20,525,400 floats exactly equal across44.1/48/96/192kHz, all15algorithms and3sync/capture configurations, with Groove off and arbitrary shift-array contents. This establishes the pure extraction separately and verifies the completed off path.
- Four ModelVersions codec tests pass, including all16 mixed-domain combinations and schema1migration.

Integrated plugin state/scene routing, the complete pre-existing Groove suite, GUI interaction and manual swung-loop listening still require the root candidate. Component evidence is not a native host certification.
