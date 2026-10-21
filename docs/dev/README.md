# GapFill developer documentation set

Status: documentation roadmap and maintenance index, checked 2026-10-21 against the current repository and active stabilization work. The docs/dev specifications describe intended work; their presence does not mean the feature is implemented. For shipped status, check the source, tests, compliance table, and release evidence.

The current implementation priority remains docs/dev/08-STABILIZATION-MODULARITY-FEATURE-BACKLOG.md. Stabilization and platform gates come before the new feature candidates. This set builds on docs/superpowers/specs/2026-10-06-creative-performance-features-design.md and records revisions in 02-FOUNDATIONS.md.

## Read in this order

| # | File | Read it when |
|---|---|---|
| 01 | 01-DEVELOPMENT-PLAN.md | Before starting work. Phases, branch/merge rules, ownership, and ticket list. |
| 02 | 02-FOUNDATIONS.md | Before engine work. Shared scaffolding and revisions to the creative spec. |
| 03 | 03-SPEC-ESSENTIALS.md | Essentials such as audition, sync, skip reasons, CI, formats, signing, and real-host QA. |
| 04 | 04-SPEC-CREATIVE.md | Creative personalities, macros, phrase behavior, fill lock, breathing, per-band fill type, and evolution. |
| 05 | 05-SPEC-USABILITY-VALUE.md | History, export, presets, MIDI trigger, Simple/Advanced, calibration, first-run, scale, and status. |
| 06 | 06-ENGINEERING-STANDARDS.md | Always. Real-time rules, state, determinism, performance, tests, accessibility, and review. |
| 07 | 07-QA-AND-RELEASE.md | Before large merges and releases. Test matrix, manual checks, and release evidence. |
| 08 | 08-STABILIZATION-MODULARITY-FEATURE-BACKLOG.md | Current priority override: stabilize platform gates, modularize safely, defer and rank feature work. |
| 09 | 09-FAST-INCREMENTAL-BUILDS.md | Use target-specific builds during local development; it does not replace full CI/release gates. |
| 10 | 10-SPEC-LIVE-CREATIVE-EFFECTS.md | Live performance workflow, scenes, MIDI/transport behavior, and creative effect specifications. |
| 11 | 11-GUI-PLACEMENT-AND-FEATURE-PLAN.md | Evaluate useful feature candidates and plan their placement before changing the GUI. |

## Additional developer references

| File | Purpose |
|---|---|
| ../DEV-SETUP.md | Reproducible local setup and platform notes. |
| ../architecture/MODULES.md | As-built module ownership, contracts, threading, and seams for safe extraction. |
| ../TEST-TRACEABILITY.md | Maps requirements to automated tests, CI jobs, host checks, and release evidence. |
| ../TROUBLESHOOTING.md | Reproduction, build/test failures, audio timing, validator crashes, and flaky-test policy. |
| ../adr/README.md | When and how to record long-lived architecture decisions. |
| ../COMPLIANCE.md | Shared baseline profiles and current compliance status. |
| ../VALIDATION.md | Full validation steps and release acceptance. |
| ../theme.md | Visual design and interface constraints. |

## Ticket naming and status

Each feature spec uses ticket IDs such as F0-1, E-1, C-3, and U-2. Use the ID in branch names, PR titles, and commit subjects. 01-DEVELOPMENT-PLAN.md lists dependencies, size, and phase.

Use these status rules consistently:

- A specification is a proposal until the implementation, tests, and acceptance evidence are complete.
- Do not mark a feature shipped because it appears in a spec or GUI mockup.
- The priority override in document 08 governs sequencing where older creative roadmap documents conflict.
- Update docs/TEST-TRACEABILITY.md when the test/evidence state changes. Update docs/architecture/MODULES.md when code ownership or thread boundaries move.
- Use MUST, SHOULD, and MAY in the RFC 2119 sense.
- Neutral defaults must preserve the current output within the approved golden-test tolerance.
- File paths in the numbered specs are repo-root-relative. Sizes use S (<1 day), M (1-2 days), L (3-5 days), XL (larger; split it).
- Audio thread means processBlock and anything it calls. See document 06 for forbidden work on that thread.