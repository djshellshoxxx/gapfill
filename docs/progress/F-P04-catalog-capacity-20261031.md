# F-P04 catalog capacity and inventory correction

This work prepares catalog generation while the second UI wave is still in
progress. It does not freeze or publish final language resources. Regenerate
them from the final integrated source before candidate validation.

The inventory lexer previously treated the C++ character literal `'"'` as a
string opener. In `history/RecipeFile.cpp` that produced the bogus entry
`).getChildFile (` and hid subsequent complete recipe-error sentences. The
lexer now skips character literals, including escaped/prefixed characters,
without treating a numeric separator such as `100'000` as a character literal.
Adjacent strings, comments and raw strings still preserve their actual values.

Compact JSON removes one formatting newline per key while preserving all
translation keys and values, UTF-8, full templates, placeholder order and the
at-least-40-percent pseudo expansion. Both generator and runtime still reject
catalogs larger than 262144 UTF-8 bytes. No UI sentence is shortened or discarded
to fit the cap.

Additional context exclusions cover XML element/attribute identifiers, fixed
`getChildFile` directory names, the MIDI-map state type and character-validation
alphabets. These bytes represent file/state syntax, rather than display text.
The same value remains cataloged whenever it also occurs at a presentation
sink. They stay in the inventory with a null key. User-facing model/file errors
remain included; pending sink categories remain unverified coverage.

For the isolated source snapshot `608e08a`, the old generator produces 2746
keys and a 253323-byte pseudo catalog. The corrected compact generator produces
2739 keys, English 191851 bytes and pseudo 250468 bytes (11676 bytes below the
cap). The changed inventory removes only actual protocol/character-set entries,
fixed directory names or the bogus lexer output, and restores five previously
missed complete recipe-error sentences. Existing retained English values and
stable keys are identical. These figures are capacity observations for that
snapshot; later integrated UI/help may add more text.

Regression checks cover escaped character literals, numeric separators,
adjacent/raw strings, compact JSON round trips, deterministic ordering,
multibyte UTF-8 byte limits and shared protocol/display words. All seven catalog
tests pass against a freshly generated temporary snapshot, leaving tracked
resources untouched. Final integrated freshness and actual localized native
layout/accessibility evidence remain pending.
