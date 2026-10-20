# GapFill developer documentation set

Status: proposed roadmap originally written against `master` @ `01f2f56` (v0.0.1 beta). The current execution-priority override is `08-STABILIZATION-MODULARITY-FEATURE-BACKLOG.md` (2026-10-19).
Supersedes nothing: it builds on
`docs/superpowers/specs/2026-10-06-creative-performance-features-design.md`
(the "creative spec") and says explicitly where it **revises** it (see
`02-FOUNDATIONS.md` §Revisions to the creative spec).

## Read in this order

| # | File | Read it when |
|---|---|---|
| 01 | `01-DEVELOPMENT-PLAN.md` | Before starting any work. Phases, branch/merge rules, hot-file ownership, ticket list. |
| 02 | `02-FOUNDATIONS.md` | Before touching engine code. The shared scaffolding (Phase 0) that every feature plugs into. |
| 03 | `03-SPEC-ESSENTIALS.md` | Fill Audition, tempo-sync audit, "why didn't it fill?" reasons, CI, macOS/AU, signing, real-host QA. |
| 04 | `04-SPEC-CREATIVE.md` | Personalities, Macros, Mistakes, Intensity Envelope, Fill Lock, More Like That, Phrase, Breathing, per-band fill type, Evolve. |
| 05 | `05-SPEC-USABILITY-VALUE.md` | Fill History, fill export, preset browser, MIDI note trigger, Simple/Advanced view, Auto-Calibrate, first-run, UI scale, status line. |
| 06 | `06-ENGINEERING-STANDARDS.md` | Always. Real-time rules, determinism, state rules, testing, review checklist, performance budgets. |
| 07 | `07-QA-AND-RELEASE.md` | Before merging anything big and before every release. Test matrix, manual scripts, risk register. |
| 08 | `08-STABILIZATION-MODULARITY-FEATURE-BACKLOG.md` | **Current priority override:** Windows, Linux, macOS and CLAP build gates first; modular regression-safe foundation second; all new features deferred and catalogued with descriptions, relative difficulty and value. |
| 09 | `09-SPEC-LIVE-CREATIVE-EFFECTS.md` | Candidate specifications for gap timeline/reasons, audition and recipe comparison, live performance scenes and safety, musical context, wet-only export, and eight new fill effects. Read after Gate A/B; canonical E/C/U specs remain authoritative for existing tickets. |

## Ticket naming

Every spec section has a ticket ID (`F0-1`, `E-1`, `C-3`, `U-2` ...).
Use it in the branch name, PR title and commit subjects, e.g.
`claude/E-1-fill-audition`, `E-1: add preview voice slot`.
`01-DEVELOPMENT-PLAN.md` lists every ticket with dependencies, size and phase.

## Conventions used in the specs

* **MUST / SHOULD / MAY** are used in the RFC 2119 sense.
* "Neutral" means: with the feature's controls at their defaults the audio
  output is bit-identical (within the golden-test tolerance) to the build
  before the feature. This is the project's central regression guarantee.
* File paths are relative to the repo root. New files are marked **(new)**.
* Sizes: S = under 1 day, M = 1-2 days, L = 3-5 days, XL = more (split it).
* "Audio thread" means `processBlock` and anything it calls. See
  `06-ENGINEERING-STANDARDS.md` §1 for what is forbidden there.
