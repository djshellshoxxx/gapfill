# Portable scene banks

BankFile parses and validates on the message thread before touching a live
bank. Import builds the entire candidate and then swaps it in, with one Undo
snapshot. Invalid reports or candidates leave the old bank unchanged.

The v2 UTF-8 XML format supports 64 scenes and at most 1 MiB. It identifies the
scene scope version explicitly and writes enough scientific digits to preserve
every float32 parameter value, including tiny nonzero values. The SHA-256
integrity payload covers that precision and scope version. Format v1 remains
readable with its original six-decimal digest and released scope semantics.
Older GapFill releases reject a formatVersion 2 bank through their existing
newer-format gate; this is distinct from optional project-subtree behavior.

Parsing validates the complete numeric grammar, supported versions, bounds,
duplicate indices and excluded parameters. A source file with unknown IDs can
still report ignored fields and a digest warning according to the existing
forward-compatible import contract. Export refuses an invalid/oversize bank;
an empty export result is never written as a successful file.

Tests: SceneBank and SceneFormat. Actual cross-OS file exchange remains an
acceptance check until performed on the generated artifacts.
