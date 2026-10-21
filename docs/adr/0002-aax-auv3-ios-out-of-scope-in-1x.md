# ADR 0002: AAX, AUv3 and iOS are out of scope in 1.x

- Status: Proposed (awaiting maintainer approval; code must not treat it as authorisation)
- Date: 2026-10-21
- Owners: project maintainer
- Related spec/issue/PR: docs/dev/12-SPEC-BACKLOG-AND-NEW.md section 2.5 (F-P03)

## Context
Supported formats are VST3, AU (macOS), CLAP and Standalone. Users have asked about Pro Tools (AAX) and iOS. AAX needs Avid's SDK under licence
terms that must be reviewed against the AGPL; AUv3/iOS needs a different packaging, sandbox and UI model, a CI runner and a device to validate.

## Decision
The CMake `FORMATS` list stays VST3/AU/CLAP/Standalone for all 1.x releases. No AAX, AUv3 or iOS code, targets or CI jobs are added.

## Alternatives considered
| Option | Benefits | Costs/risks | Why selected or rejected |
|---|---|---|---|
| Stay with current formats (chosen) | Every shipped format has CI and host checks | No Pro Tools / iOS users | Selected |
| Add AAX | Pro Tools market | SDK licence vs AGPL, signing (PACE), no host to test | Rejected until licence terms are reviewed |
| Add AUv3/iOS | Mobile market | Sandbox, different UI, no device or CI runner | Rejected until entry criteria are met |

## Consequences
- Positive: smaller validation matrix; no licensing exposure.
- Negative/tradeoffs: not available in Pro Tools or on iOS.
- Compatibility/migration: none.
- Real-time/threading: none.
- CPU/memory/latency: none.
- Licensing/security/privacy: avoids shipping a proprietary SDK inside an AGPL project.

## Verification
CMake `FORMATS` lists exactly the supported formats; `tools/validate_repo.py` and the release workflow build only those.

## Rollback or revisit trigger
Written licence/SDK terms from Avid reviewed by the owner, a CI runner able to build the format, a host or device to validate on, and a recorded
user-demand count.
