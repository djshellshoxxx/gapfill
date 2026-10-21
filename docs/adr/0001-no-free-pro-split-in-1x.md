# ADR 0001: No Free/Pro edition split in 1.x

- Status: Proposed (awaiting maintainer approval; code must not treat it as authorisation)
- Date: 2026-10-21
- Owners: project maintainer
- Related spec/issue/PR: docs/dev/12-SPEC-BACKLOG-AND-NEW.md section 2.5 (F-P01), docs/dev/features/P-01-free-pro-build-flag.md, docs/dev/features/DECISIONS.md item 4

## Context
P-01 asked for a compile-time free/pro flag. GapFill is one AGPL codebase; every feature shipped so far (WP3-WP11) lives in one build and
nothing in the code gates a feature by edition. A split would affect parameter availability, saved state and presets, and the support
burden, and no commercial or licensing decision has been made.

## Decision
Ship all features in one build in 1.x. No `GAPFILL_PRO` flag exists in the code. If a Pro build is wanted later it may gate only UI entry
points from an enumerated list (macro editor, history export, calibration, evolve, scene-bank file exchange, effect cards beyond the first four),
never DSP code paths or parameter IDs, and a free build that loads a project using Pro features must keep and play the stored values.

## Alternatives considered
| Option | Benefits | Costs/risks | Why selected or rejected |
|---|---|---|---|
| One build (chosen) | Identical state, presets, automation everywhere; simplest QA matrix | No product tiering | Selected: nothing requires tiering today |
| Compile-time GAPFILL_PRO gating UI only | Possible later without state changes | Two artefacts to validate and support | Deferred behind a new decision |
| Runtime licence key | Single binary | Licence handling, privacy statement change (no network today) | Rejected for 1.x |

## Consequences
- Positive: no edition-dependent behaviour to test; all features documented in the README apply to every user.
- Negative/tradeoffs: no built-in path to monetise features without a new ADR.
- Compatibility/migration: none; parameter IDs and state are unaffected either way.
- Real-time/threading: none.
- CPU/memory/latency: none.
- Licensing/security/privacy: AGPL unchanged; no key checks, no network.

## Verification
`tools/audit/completeness_audit.py` reports the ticket as out of scope; `grep -r GAPFILL_PRO` finds nothing in `Source/` or CMake.

## Rollback or revisit trigger
The owner decides to publish a paid edition, or a licensing review requires one.
