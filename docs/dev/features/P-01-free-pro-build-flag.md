# P-01 Free / Pro build split (`include.md` mention; parked item in `05`)

Status: specified with options (this pass). **Needs owner decision** before U-2 and C-2 UI ship (the backlog says to decide before then). Size S for the mechanism, product decision is not engineering.

## 1. Summary and user-visible behaviour
`include.md` allows a possible pro build. This ticket defines the mechanism so one codebase builds both, and a decision table of which features are pro-only. Until the owner decides, every build is "Full" (all features) and the mechanism compiles to nothing.

## 2. Success / quality metric
`-DGAPFILL_PRO=OFF` and `ON` both build and pass tests; the free build has the same parameter IDs and state format as pro (so projects open in both, with pro-only features inert and clearly labelled); no pro feature code runs in the free build.

## 3. Design
* CMake option `GAPFILL_EDITION` = `Full` (default) | `Free` | `Pro`; defines `GAPFILL_EDITION_FREE`/`PRO` and `PRODUCT_NAME`/bundle ids per edition (decision needed: same plugin ID or different, see options).
* `Source/features/edition/Edition.h`: `constexpr bool isAvailable (Feature)` with a `enum class Feature { Macros, History, Export, Calibrate, Evolve, Scenes, Taste, Cooperate, ... }` table; features check it in their `Gather` and in their UI slot factory.
* Parameters of unavailable features still exist (IDs permanent) and keep defaults; their `Gather` returns neutral; UI shows a locked card "Available in GapFill Pro".
* No licence check, no network (README privacy promise). If a licence key is ever wanted, that is a separate ADR (offline signature verification).
### Decision table (owner fills in)
| Feature | Suggested edition | Rationale |
|---|---|---|
| Core fills, Audition, Reasons, Tempo sync, Presets, Undo | Free | the product's identity |
| History/Lock/Like, Capture v2 | Free | core workflow |
| Macros, Scenes, Performance tab | Pro (suggested) | live performance value |
| Export, Calibrate, Evolve, Taste, Cooperate, spectral/rhythmic FX packs | Pro (suggested) | advanced value |
### Options
A: single binary, features unlock by edition build only (this spec). B: two products with different plugin IDs (hosts treat them separately; users can install both). C: stay fully free and open (AGPL project: note that AGPL allows anyone to build all features from source regardless of edition).

## 4. Parameters and state
No new parameter. State identical across editions. Unavailable-feature subtrees are preserved on save (round trip) so opening a pro project in free and saving does not lose data.

## 5. Real-time safety
Compile-time constants only.

## 6. UI
Locked cards use text + icon (not colour only), accessible name "Pro feature, locked"; no nag popups.

## 7. MIDI / automation / CLAP / host
If plugin IDs differ (option B) AU/CLAP/VST3 identifiers and `PLUGIN_CODE` must be unique and permanent: decide before first release of the second product.

## 8. Tests
`edition_table_covers_every_feature` | `free_build_neutral_for_pro_features` (golden A-F) | `pro_subtree_survives_free_roundtrip` | `locked_ui_has_accessible_names` | `both_editions_build_in_ci`.

## 9. Dependencies and build order
After B1 (CMake fragments). Before C-2 UI / U-2 UI. Needs the owner decision on licensing/editions and the AGPL implications (license text is in `LICENSE`).

## 10. Effort and risks
~1.5 days mechanism. Risks: AGPL compliance and commercial terms (legal review, not engineering); users confused by two products.

## 11. Acceptance checklist
- [ ] Owner decision recorded (option A/B/C, table filled)
- [ ] Both editions build and pass; goldens unchanged for Full
- [ ] COMPLIANCE.md and README updated
