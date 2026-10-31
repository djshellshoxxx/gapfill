# Macro engine and versioned target dispatch

MacroModel owns message-thread editing, bounded codecs and publication.
MacroEngine is pure bounded audio-thread arithmetic. The four knobs, eight
mappings per macro, persisted 36-target order and existing per-macro LFO
behavior are preserved. Core target ownership retains the released 21-bit
mask; MacroV2 adds an independent 15-bit mask for targets 21..35. Scene
projection observes both masks. No stage writes mapped APVTS parameters.

Released `<Macros v="1">` configurations load with target engine schema 1:
the formerly disconnected advanced targets remain inaudible. New projects use
schema 2, which dispatches all 36 targets. Writers emit only
`<MacrosV2 v="2" targets="1|2">`; saving a legacy configuration preserves its
old engine schema until the user explicitly upgrades. Names, mappings, LFOs,
indices and host parameter IDs survive that upgrade. A valid MacrosV2 takes
precedence over legacy Macros; invalid new plus valid old falls back to old.
Unknown versions and malformed tuples are rejected atomically. Both forms
are limited to 2 KiB. Released readers ignore the new optional subtree;
complete new-to-old recall is not promised.

Targets 21..31 control the shipped effect Amount fields. Targets 33 and 34
control Energy Adapt and Scale Amount. The two shipped virtual identities
retain their persisted names: `grooveAmount` controls `fl09_amount`, and
`tensionAmount` is a runtime master over the four configured tension depths.
Tension amount 0 zeros those depths; 100% uses their configured nominal
values. Repeated decision LFO evaluations use the nominal depth snapshot and
never multiply an already scaled depth. This does not turn Tension on or
change any saved depth/parameter. Availability uses `fm21_on` and
`fl09_amount`, respectively.

Tests: TestMacroV2 covers all advanced fields, ownership, legacy restore and
explicit upgrade, strict versions, atomic malformed input, tension/LFO
interaction, portable macros, and allocation-free cached evaluation. Existing
TestMacros preserves core/LFO behavior. The separate eight-macro/global-LFO
proposal differs from the shipped and doc12 contract; that conflict is recorded
in docs/progress/macro-v2-20261031.md and is not resolved by reinterpreting any
persisted index.
