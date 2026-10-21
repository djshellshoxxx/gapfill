# Spec gap report

Date: 2026-10-20. Scope: every unfinished or unspecified to-do found in PROGRESS 'Next up', the 2026-10-20 audit, tickets in `01`-`05`, `08` backlog, `10` and `11`.
Status values: **complete spec** (already implementation-complete in `01`-`07`, `10`, `11`), **completed by this pass** (spec in `docs/dev/features/`), **needs owner decision** (spec written, one or more decisions block implementation).

## 1. Already complete (no new file needed)
| Ticket | Where | Status |
|---|---|---|
| F0-1 .. F0-9 | `02-FOUNDATIONS.md` (maps to `08` B1-B10: B1 -> F0-1, B2 -> F0-1/F0-5, B3 -> F0-2/F0-3/F0-5, B4 -> F0-4, B5 -> F0-9, B6 -> F0-6, B7 -> F0-7, B8/B9 -> F0-8, B10 -> F0-1 sample module; B0 baseline lock -> A0-05 + Q-01; B11 -> `06` merge rules) | complete spec |
| E-1 Fill Audition, E-2 tempo sync, E-3 reasons/legend | `03-SPEC-ESSENTIALS.md` (E-1 = F-M01, E-3 = F-M02, E-2 -> F-S05 relation) | complete spec |
| E-4 CI, E-5 macOS/AU, E-6 signing, E-7 real-host QA | `03` (A0, M-01..M-04, C-01..C-04, L-01..L-04 gates; A0-03/04/06 -> E-4; M-* -> E-5/E-6; real-host rows -> E-7 + Q-01) | complete spec; evidence harness completed by Q-01 |
| C-0 .. C-11 | `04-SPEC-CREATIVE.md` (C-0 = F-S05; C-1 = F-M06; C-2 = F-L06; C-3 = F-M13; C-4 = F-M05; C-5 = F-L01; C-6 = F-M07; C-7 = F-L08; C-8 = F-L16; C-9 = F-M09; C-10 = F-M08; C-11 = F-L03) | complete spec |
| U-1 .. U-10 | `05-SPEC-USABILITY-VALUE.md` (U-1 = F-L02; U-2 = F-L04; U-3 = F-S04; U-4 = F-M12 and F-S11; U-5 = F-M04; U-6 = F-L05; U-7 = F-S09; U-8 = F-S03; U-9 = F-S02; U-10 = preset QA) | complete spec |
| F-M03 timeline, FX parameters | `10` section 4.1 / 5; per-ticket files below restate them | complete spec |
| A0-01 .. A0-06 | `08`, `07`, `09` | complete spec (process); A0-02 and PR reconciliation recorded in the PR 12/9 reconcile task |
| A5 single release pipeline | `07-QA-AND-RELEASE.md` plus Q-01 | complete spec |
| W-02, W-04, W-05, W-06 | W-02 inside W-01 file; W-04 -> F0-6 + Q-01 state fixtures; W-05/W-06 -> Q-01 + D-01 | completed by this pass |

## 2. Ticket -> spec file -> status
Files are in `docs/dev/features/`. Where two writers produced the same ticket, the surviving file is the one listed; the superseded duplicates were moved out of the tree (see section 4).

| Ticket | Spec file | Status | Decision needed |
|---|---|---|---|
| D-01 | `D-01-compliance-and-docs-refresh.md` | completed by this pass |  |
| F-FX0 | `F-FX0-effect-module-seam.md` | needs owner decision | D3: FillRecipeEx 24-byte history format |
| F-L01 | `F-L01-fill-lock.md` | completed by this pass |  |
| F-L02 | `F-L02-fill-history-save-recipe.md` | completed by this pass |  |
| F-L03 | `F-L03-more-like-that.md` | completed by this pass |  |
| F-L04 | `F-L04-fill-export-daw-drag.md` | completed by this pass |  |
| F-L05 | `F-L05-auto-calibrate.md` | completed by this pass |  |
| F-L06 | `F-L06-performance-macros.md` | completed by this pass |  |
| F-L07 | `F-L07-performance-scenes.md` | needs owner decision | scene count, seed in scenes |
| F-L08 | `F-L08-phrase-aware-scheduling.md` | completed by this pass |  |
| F-L09 | `F-L09-groove-inheritance.md` | completed by this pass |  |
| F-L10 | `F-L10-call-and-response.md` | completed by this pass |  |
| F-L11 | `F-L11-midi-output.md` | needs owner decision | plugin-format MIDI output support |
| F-L12 | `F-L12-wet-only-output-bus.md` | completed by this pass |  |
| F-L13 | `F-L13-enhanced-sidechain-fills.md` | completed by this pass |  |
| F-L14 | `F-L14-multiband-routing.md` | completed by this pass |  |
| F-L15 | `F-L15-dub-feedback-network.md` | completed by this pass |  |
| F-L16 | `F-L16-controlled-evolve.md` | completed by this pass |  |
| F-L17 | `F-L17-mutation-lab.md` | completed by this pass |  |
| F-L18 | `F-L18-manual-scale-key.md` | completed by this pass |  |
| F-L19 | `F-L19-frequency-shifted-delays.md` | completed by this pass |  |
| F-L20 | `F-L20-formant-transformations.md` | completed by this pass |  |
| F-L21 | `F-L21-transient-convolution-reverb.md` | completed by this pass |  |
| F-L22 | `F-L22-musical-energy-tracking.md` | completed by this pass |  |
| F-L23 | `F-L23-recipe-ab-comparison.md` | completed by this pass |  |
| F-L24 | `F-L24-controller-integration-feedback.md` | needs owner decision | MIDI feedback depends on F-L11 |
| F-L25 | `F-L25-live-safety-recovery.md` | needs owner decision | default safeStart/guard values |
| F-M01 | `F-M01-fill-audition.md` | completed by this pass |  |
| F-M02 | `F-M02-why-didnt-it-fill.md` | completed by this pass |  |
| F-M03 | `F-M03-visual-gap-timeline.md` | completed by this pass |  |
| F-M04 | `F-M04-simple-advanced-view.md` | completed by this pass |  |
| F-M05 | `F-M05-fill-intensity-envelope.md` | completed by this pass |  |
| F-M06 | `F-M06-fill-personalities.md` | completed by this pass |  |
| F-M07 | `F-M07-density-breathing.md` | completed by this pass |  |
| F-M08 | `F-M08-capture-v2-recapture.md` | completed by this pass |  |
| F-M09 | `F-M09-per-band-fill-type.md` | completed by this pass |  |
| F-M10 | `F-M10-momentary-effects-and-latch.md` | completed by this pass |  |
| F-M11 | `F-M11-quantized-scene-preset-changes.md` | completed by this pass |  |
| F-M12 | `F-M12-preset-browser.md` | completed by this pass |  |
| F-M13 | `F-M13-controlled-mistakes.md` | completed by this pass |  |
| F-M14 | `F-M14-rhythmic-bitcrushing.md` | completed by this pass |  |
| F-M15 | `F-M15-comb-filter-resonator.md` | completed by this pass |  |
| F-M16 | `F-M16-rhythmic-ratcheting.md` | completed by this pass |  |
| F-M17 | `F-M17-bit-perfect-wet-only-output.md` | completed by this pass |  |
| F-M18 | `F-M18-beat-battles.md` | completed by this pass |  |
| F-M19 | `F-M19-freeze-and-scrub.md` | completed by this pass |  |
| F-M20 | `F-M20-tape-destruction-expansion.md` | completed by this pass |  |
| F-M21 | `F-M21-tension-release-curves.md` | completed by this pass |  |
| F-M22 | `F-M22-smart-preset-suggestions.md` | completed by this pass |  |
| F-M23 | `F-M23-advanced-modulation-macro-targets.md` | needs owner decision | macro-to-macro modulation |
| F-S01 | `F-S01-cpu-latency-display.md` | needs owner decision | 06 clock-call amendment |
| F-S02 | `F-S02-parameter-info-status-line.md` | completed by this pass |  |
| F-S03 | `F-S03-ui-scale.md` | completed by this pass |  |
| F-S04 | `F-S04-midi-note-trigger-audition.md` | completed by this pass |  |
| F-S05 | `F-S05-effective-value-feedback.md` | completed by this pass |  |
| F-S06 | `F-S06-ab-compare-level-matching.md` | completed by this pass |  |
| F-S07 | `F-S07-stutter-storm.md` | completed by this pass |  |
| F-S08 | `F-S08-glitch-roulette.md` | needs owner decision | listening sign-off |
| F-S09 | `F-S09-first-run-guide.md` | completed by this pass |  |
| F-S10 | `F-S10-genre-performance-presets.md` | needs owner decision | listening sign-off |
| F-S11 | `F-S11-preset-favorites-next-previous.md` | completed by this pass |  |
| F-S12 | `F-S12-safe-return-panic.md` | completed by this pass |  |
| F-X01 | `F-X01-harmonic-granular-resynthesis.md` | completed by this pass |  |
| F-X02 | `F-X02-spectral-freeze-and-morph.md` | completed by this pass |  |
| F-X03 | `F-X03-automatic-key-detection.md` | completed by this pass |  |
| F-X04 | `F-X04-multi-section-arrangement-awareness.md` | completed by this pass |  |
| F-X05 | `F-X05-local-preference-learning.md` | needs owner decision | Skip semantics (negative vs neutral) |
| F-X06 | `F-X06-multi-track-cooperative-gapfill.md` | needs owner decision | multi-instance ownership policy (in-process only) |
| F-X07 | `F-X07-adaptive-performance-quality-modes.md` | needs owner decision | 06 clock-call amendment |
| P-01 | `P-01-free-pro-build-flag.md` | needs owner decision | edition option A/B/C |
| Q-01 | `Q-01-release-qa-matrix-harness.md` | completed by this pass |  |
| T-01 | `T-01-meter-peak-hold-verification.md` | completed by this pass |  |
| T-02 | `T-02-top-bar-8px-grid-pass.md` | completed by this pass |  |
| W-01 | `W-01-windows-editor-automation-crash.md` | completed by this pass |  |
| W-03 | `W-03-variable-block-transport-prediction.md` | completed by this pass |  |

## 3. Owner decisions (consolidated)
1. **P-01** edition model: A (single build, runtime flag), B (separate targets), C (defer).
2. **06 amendment**: allow `juce::Time::getHighResolutionTicks` (F-S01 load meter, F-X07 governor, benchmark).
3. **D3** (F-FX0): `FillRecipeEx` 24-byte history/lock entries (v2) alongside the 16-byte `FillRecipe`.
4. **F-X05** Skip semantics: negative signal vs neutral.
5. **F-L25** defaults for `safeStart` and `guard` (recommend off/off to keep legacy sessions bit-identical).
6. **F-L07 / F-M18** scene count (8 proposed) and whether `seed` is part of a scene.
7. **F-L11 / F-L24** whether plugin formats may emit MIDI (VST3/CLAP yes, AU needs a different type); standalone only otherwise.
8. **F-S08 / F-S10** listening sign-off by owner for roulette profiles and genre presets.
9. **F-M23** macro-to-macro modulation allowed (cycle guard) or not.
10. **F-X06** confirm in-process only (no networking, no shared files) and the 16-slot cap.

## 4. Notes
- Duplicate specs from parallel writers were consolidated: superseded copies of F-L07, F-L24, F-L25, F-M10, F-M14, F-M18, F-M23, F-S01, F-S06, F-S12 were moved to the scratchpad (`.../scratchpad/dupes/`) and not committed. The lead may diff them against the survivors for unique content (notably QuantClock, SnapshotBox and LoadMeter sharing notes).
- F-FX0 is a new prerequisite ticket (effect module seam); F-M14, F-M16, F-M20 and the other FX tickets depend on it.
- Items honestly **not** specified: nothing from the backlog lacks a file. Items needing real-host evidence (W-01 repro, M-03, C-03, L-03) cannot be completed by documents; Q-01 defines how the evidence is recorded and they remain NO-GO until captured.
