# 01 - Development plan

Goal: ship the features in `03`-`05` with **few regressions and no painful merges**.
The plan's three levers: (1) one up-front refactor that gives every feature its own
files (`02`), (2) a strict merge discipline, (3) automated gates that catch the
bugs this kind of plugin is prone to (audio-thread safety, determinism, state
compatibility, "neutral means unchanged").

## 1. Phases and ticket list

Sizes: S <1 d, M 1-2 d, L 3-5 d, XL split first. Estimates are ideal developer-days
of focused work **including tests and docs**; plan on ~1.5x for review and host
testing. Streams are lanes that can run in parallel without touching the same hot
files (section 3).

| Phase | Goal | Tickets (size) | Stream |
|---|---|---|---|
| **A. Trust** | Verified baseline and CI | E-4 CI (M) · E-7 baseline QA of 0.0.1 in 2 hosts (M) · bug fixes found (B-n) | Infra, QA |
| **0. Foundations** | Remove merge hotspots, add seams | F0-1 (L) F0-2 (L) F0-3 (M) F0-4 (M) F0-5 (M) F0-6 (M) F0-7 (L) F0-8 (S) F0-9 (M) | one developer, serial |
| **1. Essentials** | The three essential features | E-1 Audition (L) · E-3 Reasons (L) · E-2 Tempo sync (L) · U-3 MIDI note (S) · U-10a preset pass (S) | DSP, UI |
| **2. Performance core** | Repeatable, reusable fills | C-4 Envelope (M) · C-5 Lock (L) · U-1 History (L) · C-10 Capture v2 (M) · C-11 More Like That (L) · U-10b preset pass (M) | DSP, UI |
| **3. Shaping** | Fast, playable control | C-0 effective ring (S) · C-1 Personalities (M) · C-6 Breathing (M) · C-7 Phrase (M) · C-2 Macros (XL = 2a+2b+2c) · U-10c preset pass (M) | DSP, UI |
| **4. Depth** | Surprise and value | C-3 Mistakes (L) · C-9 Band types (M) · U-2 Export (L) · U-6 Calibrate (L) · U-10d preset pass (M) | DSP, UI |
| **5. UI window** | Usability overhaul (serial) | U-9 Param info (M) · U-4 Preset browser (L) · U-5 Simple/Advanced (L) · U-8 UI scale (M) · U-7 First-run (M) | UI only, serial |
| **6. Reach** | Platforms and polish | E-5 macOS/AU (XL = 5a-5c; **starts in Phase 1, parallel track**) · E-6 Signing (M) · C-8 Evolve (L) · U-10e final preset pass (M) | Infra |

Approximate effort, using S = 0.5 d, M = 1.5 d, L = 4 d, XL = 8 d (my estimates, not
measured on this codebase): Phase A ~4 d, Phase 0 ~20 d, Phase 1 ~13 d, Phase 2 ~17 d,
Phase 3 ~15 d, Phase 4 ~15 d, Phase 5 ~13 d, Phase 6 ~15 d = **about 112 ideal
developer-days**. With the 1.5x allowance for review, host testing and rework that is
roughly **170 elapsed days: ~34 weeks for one developer, ~20 weeks with two parallel
streams**. Re-estimate at the end of Phase 0, when real numbers (build time, test time,
`sizeof`) exist. Phases 1-4 each end in a usable beta, so the plan can stop after any of
them with a coherent product.

### Dependency graph (arrows = "must merge before")

```
E-4 CI ──────────────────────────────► everything (required checks)
E-7 baseline QA ─► B-n fixes ─► F0 ──► all features
F0 ─► E-1 ─► U-3, U-1(audition action), U-2
F0 ─► E-3 (needs E-1's VizFrame/preview flag reserved in F0)
F0 ─► E-2
F0 ─► C-4 ─► C-3 (recipe carries mistake) ; C-5 ─► C-3, U-1(lock action)
F0 ─► U-1 ─► C-11 ; C-5
F0 ─► C-6, C-7 (decision stage) ─► C-8
F0 ─► C-1, C-2 (block stage) ; C-0 ─► C-1, C-2
F0 ─► C-9, C-10
U-4 settings file ─► U-7, U-8
E-5 (macOS) independent of features after F0-1; goldens added per platform
```

### Why this order (differs from the creative spec §9 in two places)
* Audition (E-1) and Reasons (E-3) are first: they are the biggest first-impression
  and design-loop wins and are prerequisites for judging every later feature by ear.
* Fill Lock/History/Like come **before** Macros/Personalities because they need only
  the recipe plumbing, while Personalities/Macros need the visual effective-value ring
  (C-0) and the larger UI work. The creative spec's "easy first" order is otherwise kept
  (Envelope, Breathing, Phrase are small and early).
* The UI overhaul (Phase 5) is deliberately **late and serial**: it edits `Panels.cpp`,
  `PluginEditor.cpp` and tooltips everywhere, so no other UI work runs beside it.

**Addendum 2026-10-21:** the tickets in `12-SPEC-BACKLOG-AND-NEW.md` (N-*, F-*, X-1, FX-0, SP-0, UI-0, F0-10) extend this list; their phases, merge order and parallel work packages are `12` sections 12-14. Rule 7 below is relaxed to one open PR per work package (`12` section 11).

## 2. Branch, PR and merge rules

1. **Trunk-based.** `master` is always buildable and releasable. Work happens on
   short-lived branches `claude/<ticket>-<slug>` (or `<initials>/<ticket>-<slug>`).
   A branch lives <= 3 working days; if a ticket is bigger, split it into
   independently mergeable slices (e.g. C-2a engine without UI, C-2b UI behind the
   neutral default).
2. **One ticket per PR**, <= ~600 changed non-test, non-generated lines. Larger =>
   split. Title `C-5: Fill Lock - engine`, body from the PR template
   (`.github/pull_request_template.md`, add in Phase 0): ticket link, what changed, neutral-at-default
   evidence (golden unchanged), tests added, hot files touched (should be none),
   unavailable checks listed honestly.
3. **Merge method: squash.** Linear history, single-commit revert. Keep the PR title
   as the commit subject. Commit trailers per repo convention.
4. **Update from master daily**: `git fetch origin && git merge origin/master` into the
   feature branch (merge, not rebase, on a shared branch; never force-push someone
   else's branch). Resolve conflicts the same day. Regenerate, never hand-merge, the
   lock files and goldens (section 3).
5. **Required checks** (E-4): preflight, linux build + tests, windows build + tests,
   pluginval. The branch must be up to date with `master` before merge (merge queue
   if available, otherwise "require branches to be up to date").
6. **Red master protocol.** If `master` goes red: stop merging, the author of the last
   merge reverts within 1 hour unless the fix is a few lines. Never "fix forward" with
   unrelated changes.
7. **Open-PR limits.** At most **3** feature PRs open at once, and no two touching the
   same hot file (table below). If you need a hot file that another open PR touches,
   wait or stack your PR on theirs (branch from theirs, base = their branch, retarget
   to `master` when theirs merges).
8. **Release branches.** When a phase is done and QA'd: tag `v0.<minor>.<n>-beta` on
   `master`; hotfixes only on `release/0.<minor>` cherry-picked back. Version bump
   (`project(VERSION)` in `CMakeLists.txt`) happens only in the release PR.

## 3. Hot-file ownership

After Phase 0 these files are changed only as listed. `tools/check_hot_files.py`
(CI, preflight) fails a PR otherwise; an override needs the label `hot-file-ok`
with a justification in the PR body.

| File | Rule |
|---|---|
| `Source/Parameters.h`, `Source/Parameters.cpp` | Frozen after Phase 0. New params go in `Source/params/<group>.*` + `tools/param_ids/<group>.lock`. |
| `CMakeLists.txt` | Frozen. Add `cmake/sources/<feature>.cmake` and `cmake/tests/<feature>.cmake`. |
| `Source/Presets.cpp` | Only the preset-pass tickets (U-10x) and C-8's table-sharing refactor. |
| `Source/PluginProcessor.cpp/.h` | At most the 1-line hook calls defined in F0 (`02`). Feature logic lives in `Source/features/<f>/`. |
| `Source/dsp/FillEngine.cpp` | Stage functions only (F0-2). A ticket edits the one stage it owns; `trySchedule` orchestrator changes need a design note. |
| `Source/dsp/FillVoice.cpp` | A feature adds a member stage object + <= 3 hook lines (C-3, C-4). Algorithm internals are changed only by ticket E-2 (tempo hooks) and bug fixes. |
| `Source/PluginEditor.cpp`, `Source/gui/Panels.cpp` | UI goes in slot components (F0-9). Direct edits only in the Phase 5 UI window (serial). |
| `Source/gui/HelpView.cpp` | Frozen; sections register themselves. |
| `Tests/TestPlugin.cpp` | Frozen (no new tests); new tests in `Tests/features/Test<Feature>.cpp`. |
| `Tests/golden/**` | Changes only in a PR titled with `[golden]` that states which feature intentionally changed the sound. |
| `PROGRESS.md` | Maintainer updates it at phase end; sessions write `docs/progress/<ticket>.md`. |
| `docs/COMPLIANCE.md` | Updated in the same PR as any feature that changes a compliance row. |

If two tickets must change the same stage (e.g. C-5 and C-3 both use `adjustRecipe`),
the second rebases onto the first; the order is the dependency graph above.

## 4. Per-ticket workflow (applies to human or AI-assisted sessions)

1. **Start**: `git fetch origin && git merge origin/master`; `tools/dev/check.sh`
   must be green *before* you change anything (otherwise you cannot tell what you broke).
2. **Read** the ticket spec and `06-ENGINEERING-STANDARDS.md` §1-§4; list the files you
   will touch; confirm none are hot (or you own the stage).
3. **Tests first**: write the ticket's named tests (they fail), including the
   "neutral at default" golden/identity test.
4. **Implement** in small commits that each build. No drive-by refactors.
5. **Local gates** (`tools/dev/check.sh`): preflight, build (`-j2` on <=8 GB machines),
   the new group + `Plugin`/`Dsp` groups, then pluginval strictness 5 on the VST3.
   Full suite and strictness 10 run in CI.
6. **Review your own diff adversarially** with the checklist in `06` §9.
7. **Docs**: Help section, tooltip/ParamInfo entries, `docs/progress/<ticket>.md`
   (what was done, measured numbers, anything unverified, honest list of checks
   that could not be run).
8. **Open a draft PR**, mark ready when CI is green, address review, squash-merge.
9. **After merge**: delete the branch; if you discovered work not in scope, file a ticket,
   don't smuggle it in.

### Notes for working on the shared Linux box / limited machines
* The project has OOM'd on 4 GB machines (`PROGRESS.md`, 2026-09-16). Use 8 GB+; if
  not, build with `-- -j1` and run one test group at a time
  (`GapFillTests Plugin`, `GapFillTests Dsp`, ...). `Combinatorial` (13 min) runs in
  CI nightly, not locally.
* Provide setup/run commands as a single paste-able block (heredoc) where possible so
  one paste writes the script, makes it executable and runs it; `tools/dev/bootstrap.sh`
  and `tools/dev/check.sh` follow that convention.

## 5. Definition of Ready / Done

**Ready**: spec section exists with ticket ID, dependencies merged, parameter IDs
registered in `02`'s registry (or none needed), test names listed, owner knows which
files are hot.

**Done** (all must be true):
* Neutral at default: golden scenarios unchanged; `memcmp` identity test for its
  effective-settings stage; RNG-untouched test when applicable.
* New tests pass on Linux **and** Windows CI; no new warnings
  (`-Wall -Wextra -Wconversion` on `Source/features/**`, MSVC `/W4`).
* Real-time audit: `06` §1 checklist answered in the PR; allocation-tracker test
  passes where the feature touches the audio path.
* State: save/load round trip; pre-feature state fixture loads unchanged; corrupt
  subtree/value fixture rejected without breaking the rest.
* UI: tooltip, accessibility name, keyboard path, Reduced Motion, min-size layout,
  Help section; screenshot attached.
* pluginval strictness 10 and clap-validator green in CI.
* `docs/progress/<ticket>.md` written; `docs/COMPLIANCE.md` updated if affected.
* Anything not verified (real DAW, hardware MIDI, macOS) is **listed as unverified**,
  never implied passed (`docs/VALIDATION.md` rule).

## 6. Milestones and exit criteria

| Milestone | Exit criteria |
|---|---|
| **M0 - Verified baseline** (end of A) | CI green on master; 0.0.1 beta passes the manual scripts M-1..M-4 in Reaper and one other DAW; B-n list triaged. |
| **M1 - Seams in** (end of 0) | All F0 merged; goldens/fixtures committed; hot-file checker live; measured baselines recorded. |
| **M2 - Essentials** (end of 1) | Audition, Reasons, Tempo fixes shipped; beta 0.1.0; manual scripts M-1..M-6 pass in 2 DAWs + standalone. |
| **M3 - Performance core** | Lock, History, Like, Envelope, Capture v2 shipped; beta 0.2.0. |
| **M4 - Shaping** | Personalities, Macros, Breathing, Phrase shipped; beta 0.3.0; first public feedback round. |
| **M5 - Depth** | Mistakes, Band types, Export, Calibrate; beta 0.4.0. |
| **M6 - Usability overhaul** | Browser, Simple/Advanced, scale, first-run; 0.5.0-rc. |
| **M7 - 1.0 candidate** | macOS/AU + signed installers, full manual scripts on all hosts, no open S1/S2 bugs, 7-day soak. |

## 7. Risks to the plan (and how it addresses them)

| Risk | Mitigation |
|---|---|
| Merge conflicts in the same big files | F0-1/F0-2/F0-9, hot-file checker, <= 3 open PRs, daily merge from master |
| Features silently change the existing sound | Neutral-default rule + goldens + `memcmp` stage identity tests |
| Determinism broken by an extra RNG draw | Frozen draw order + per-feature salted sub-RNGs (F0-2 rules) + block-size invariance test |
| Audio-thread allocation/lock introduced | Allocation-tracker test hook, review checklist, TSan/ASan nightly |
| Old projects/presets fail to load | State fixtures from 0.0.1, `stateVersion`, per-subtree atomic validation |
| Cannot verify in real hosts | Dedicated QA scripts and gate (E-7), explicit "unverified" reporting |
| CI is unavailable again | E-4 self-hosted fallback; `tools/dev/check.sh` mirrors CI so local runs are authoritative evidence |
| Scope creep into UI | Slots (F0-9), UI window (Phase 5), backlog table in `05` |
| Single-developer bus factor | Per-ticket progress notes, specs written to be executable by someone new |
