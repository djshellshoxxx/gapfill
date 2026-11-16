# Status update (2026-11-16)

Current candidate: `integration/gapfill-release-20261116`, draft PR #40. See
[HANDOFF.md](HANDOFF.md) for the native QA failure evidence, repairs and next steps.

Ordinary Windows/macOS/Linux builds and validators passed at `43c1c6f`; full
assurance did not. This continuation repairs XML recursion bounds, disabled-mode
MIDI pass-through and platform QA issues. It adds Glitch capture coverage and
progress diagnostics without relaxing the long-running test requirements.

109 local tooling tests pass. Native execution of the new head and real-host
acceptance are still pending; macOS co-op/editor lifecycle timeouts block release.
No beta2 release or merge has been performed. The historical results below describe
older heads and must not be treated as acceptance of the current candidate.

---

# Status (2026-11-07)

Branch `claude/zealous-gauss-7b4omr` (PR #39, base #36 head `1c94ce4`). Evidence-based; nothing here is host-verified.

## Verified locally (Linux, GCC 13.3, Release, Xvfb)

| Check | Result |
|---|---|
| #36 head + `-ffp-contract=off`, built with `-march=x86-64-v3` (FMA, like the CI runners) | full suite 1674 passed, 0 failed; the 4 CI failures (MacroExt, StateQa, WetBus, StateCompat002) pass |
| Standalone repro of the snapping arithmetic | `-march=x86-64-v3` reproduces the CI values; `-ffp-contract=off` restores 50 / 0 |
| This branch, full suite before the range-interval fix | 1702 passed, 2 failed (StateQa, StateCompat002: glitch defaults drifted on resave) |
| This branch after the fix | GlitchDsp + GlitchPlugin 31/31, StateQa 6/6, StateCompat002 5/5 |
| Preflight | validate_repo OK, completeness audit 0 failing, layout literals OK, i18n inventory current, 81 + 35 tool tests OK |

## Not verified

Real DAW hosts, listening tests, Windows/macOS interactive use, screen readers, screenshots of the GLITCH tab,
release ISA baseline of the hosted toolchain (it emits FMA; whether release binaries now require AVX2/FMA is open).

## Completion against the owner's list

See `FEATURE_MATRIX.md` / `feature-matrix.json`. Features B-I are implemented and tested; J is implemented with
screenshots pending; A is pre-existing; release artefacts for the new version are not built (owner approval and
green CI on #36 first).
