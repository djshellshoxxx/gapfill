# Consolidation plan (2026-11-07)

Canonical detail: `docs/progress/SESSION-NOTES.md` (P0-P6 plan, owner decisions of 2026-11-04) and
`docs/progress/P2-branch-dispositions-20261104.md` (every stale branch, its SHA and disposition). This file is the
short merge order as of 2026-11-07; re-check live PR state before acting on it.

## Live state (read from GitHub 2026-11-07)

| PR | Head -> base | State | CI |
|---|---|---|---|
| #32 | integration/gapfill-completion-20261031 -> master | draft | preflight only (old run) |
| #36 | claude/stoic-brown-92wmta -> integration | draft, `hot-file-ok` | unit-tests red: 4 float-exactness failures (fixed by the first commit of this branch) |
| #37, #38 | stacked on #36 (CI speed, build-target tests) | draft | red for the same 4 tests |
| #35 | docs prompt -> master | draft | docs-only |
| this branch | claude/zealous-gauss-7b4omr -> claude/stoic-brown-92wmta | draft | pending |

Open PRs exceed the 3-PR cap of doc 13 rule 1; that cap is an owner policy and is reported, not enforced, here.

## Merge order (owner)

1. Land the `-ffp-contract=off` commit on #36 (cherry-pick `build: never contract ...` or merge this PR first).
   It is a one-file build change and makes #36/#37/#38 testable on the FMA-capable runners.
2. Merge #36 -> integration (merge commit), then #32 -> master (merge commit). #37/#38 rebase onto the result.
3. Merge the F-G01 commits of this branch (feature, lifted freeze: DECISIONS 19) after its CI is green.
4. Owner actions still open from 2026-11-04: the 30 approved branch deletions (proxy refuses them for sessions),
   doc 13 decisions 1, 2, 3, 5, and confirming the F-X01 Random All exception.
