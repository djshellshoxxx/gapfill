# T-02 Top bar 8 px grid layout pass (PROGRESS session 13 open deviation)

Status: specified (this pass). Size M. Needs someone who can look at the rendered window; it must not be done blind. Schedule inside the Phase 5 UI window (serial, hot files `PluginEditor.cpp`).

## 1. Summary and user-visible behaviour
`theme.md` requires spacing in multiples of 8 (or 4). PROGRESS counts 40 non-conforming values, mostly in the top bar: `reduced (2, 0)`, `reduced (1, 0)`, `reduced (10, 8)`, `reduced (10, 6)`, `removeFromLeft (10)`, `removeFromRight (6)` (see `PluginEditor.cpp` `resized()`, about lines 810-880). Fix them without overlap or clipping at the minimum window size (880 px wide today).

## 2. Success / quality metric
Zero spacing literals outside {0, 4, 8, 16, 24, ...} in the top-bar and tab layout code (checked by a script); screenshots at minimum, default and large sizes show no overlap/clipping and unchanged control order; every top-bar button keeps a hit target >= 24 px high and >= 32 px wide.

## 3. Design
1. Capture screenshots (min/default/large, 100/150/200 % scale) of the current build as the before set.
2. Replace literals with `metrics::grid` multiples; hairline gaps between adjacent buttons become 4 px (`metrics::gridHalf` if present, else add one constant). Compute the minimum width from the button widths (78+62+64+58+84+92+80+52+34+56+26+26 + gaps) and set `kMinWidth` from the total, not from a guess. If 880 px cannot hold the row with 4 px gaps, group Random/Random All/Surprise into the existing overflow (a "More" menu) per `11` guardrails, not by shrinking targets.
3. After F0-9 the top bar is slot-based (`TopBarRight`): do this pass after F0-9 so slot spacing is set once.
4. `tools/check_layout_literals.py` (new, unit-tested): scans `Source/PluginEditor.cpp` and `Source/gui/Panels.cpp` for `reduced (`, `removeFrom*(`, `withTrimmed*(` integer literals and fails on values not multiple of 4 outside an allow-list file with justification (hairlines inside custom painting).

## 4. Parameters and state
None. Saved editor size remains valid; `kMinWidth` may grow, so on load clamp saved size to the new minimum.

## 5. Real-time safety
None (message thread layout).

## 6. UI
Tooltips, accessible names and tab order unchanged; focus ring must not be clipped by the new gaps.

## 7. MIDI / automation / CLAP / host
Hosts that restore a saved smaller editor size: clamp, never reject.

## 8. Tests
`layout_literals_multiple_of_4` (script) | `top_bar_no_overlap_min_size` (compare child bounds pairwise) | `top_bar_min_target_size` | `editor_min_size_matches_measured_row` | `saved_size_clamped_to_new_minimum` | screenshot tests at three sizes against stored images (tolerance) | `keyboard_focus_ring_not_clipped`.

## 9. Dependencies and build order
After F0-9 (slots), inside Phase 5 serial UI window, together with U-8 (scale) so both are checked at once.

## 10. Effort and risks
~2 days. Risk: layout shifts users notice; announce in release notes. Risk: needs visual review (cannot be proven by tests alone); owner signs off screenshots.

## 11. Acceptance checklist
- [ ] Before/after screenshots attached for 3 sizes x 3 scales
- [ ] Literal checker green; allow-list justified
- [ ] No overlap at minimum size; hit targets verified
- [ ] PROGRESS session 13 deviation closed
