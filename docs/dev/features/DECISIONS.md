# Spec decisions (resolved by lead, 2026-10-21)

Defaults chosen so implementation is unblocked. Owner may override; changes go through a spec PR.

1. Parameter IDs: `<ticketlower>_<name>` (e.g. `fm05_envShape`). `tools/validate_repo.py` regex is extended to allow `_`. IDs are append-only; old IDs never renamed.
2. Named APVTS groups FL14..FL25 reserved up front in the foundation PR (F-FX0 / F0-1) so appends never reorder.
3. MIDI output (F-L11, F-L24 feedback): implemented behind CMake option `GAPFILL_ENABLE_MIDI_OUT`, default OFF in release builds (host-visible capability change). Feature code compiles and is unit-tested either way.
4. P-01 free/pro edition flag: out of scope. Not implemented; no edition gating anywhere.
5. F-L07 scenes: 8 scenes, `seed` NOT stored in scenes. F-M11 owns the shared quantizer module; F-L07 consumes it.
6. F-L25 defaults: `safeStart` off, `guard` on.
7. F-M10: Dry duck excluded; "Respect protection" default ON.
8. F-S06 A/B writes host parameters via the normal gesture path (undoable).
9. F-S12 Panic mutes wet only (dry untouched).
10. F-S05 ships now with the personality hook inactive until F-M06 lands.
11. F-M12/F-S11 keep the existing AppData preset folder.
12. F-X05 Skip semantics: skip = neutral (no preference signal). F-X06: in-process only, 16-slot cap.
13. F-FX0 history format: 24-byte `FillRecipeEx`, versioned.
14. F-M09 band-splitter dip is NOT retrofitted (would change golden output); new multiband code (F-L14) uses a phase-compensated crossover and is default-off.
15. F-M23 moves macro evaluation to the decision stage; F-L06 keeps unsmoothed scheduler-read targets (block-size invariance).
16. F-L15/F-L19 report extended tail only while active.
17. F-L20 accepts ~10.7 ms onset for formant fills.
18. IDs defined in docs/dev/12 (WP8 features) keep their descriptive names; all other new IDs use `<ticketlower>_<name>`.
