# F-S06 A/B comparison with level matching (also delivers F-L23 Intelligent A/B recipe comparison)

Status: specified (this pass). Size M (state compare) + M (recipe compare). Depends on the existing A/B/Copy buttons, Undo, E-1 (preview), U-1 (History), F0-4. Refines `10` section 4.2.

## 1. Summary and user-visible behaviour
**State A/B (S06a):** the existing A/B toggle can optionally match perceived loudness: when on, GapFill measures the output loudness while each side plays and applies a small attenuation to the louder side so switching does not fool the ear. The applied gain is shown ("Matched: B -1.8 dB"). It only attenuates (never boosts), is limited to 6 dB, can be turned off, and is never written to the project or host automation.
**Recipe A/B (S06b, F-L23):** in the History strip, select two recipes and press **Compare**: GapFill plays them alternately through the audition voice (E-1) with matched loudness and lets the user pick one to Lock or More-like-this. Undo restores the previous working recipe.

## 2. Success / quality metric
| # | Metric | Target |
|---|---|---|
| M1 | Loudness estimator vs ITU-R BS.1770 reference on pink noise, sine, drum loop | within 0.5 LU |
| M2 | After matching, loudness difference of A vs B | <= 0.7 dB |
| M3 | Switch click: max step at the switch | no larger than unswitched material; trim ramp 30 ms |
| M4 | Matching never boosts and never exceeds 6 dB | property test |
| M5 | State written by matching | none (project and presets byte-identical) |
| M6 | Recipe compare: preview loudness difference after matching | <= 1 dB |

## 3. Design
`Source/features/compare/{LoudnessMeter,MatchTrim,RecipeCompare}.{h,cpp}`, `Gui.*`, `Help.cpp`, `Info.cpp`.
* **LoudnessMeter:** K-weighting per BS.1770 (high-shelf +4 dB at ~1.68 kHz and 38 Hz high-pass; coefficients designed per sample rate with the standard bilinear formulas, unit-tested against the 48 kHz published values), mean-square over 400 ms blocks, gated (absolute -70 LUFS) integrated over the time the side has been active (cap 30 s, reset when the side's parameters change). Measures the **plugin output** (dry+wet after output gain), mono sum of channel energy.
* **S06a:** on switch from X to Y: if both sides have >= 3 s measured, `diff = LUFS_Y - LUFS_X` (computed against the current sound); the louder side gets `trim = -min (|diff|, 6 dB)`. Trim is a `juce::SmoothedValue<float>` (30 ms) applied on the final output; stored in an atomic for the UI badge. If a side lacks data the badge says "Play both sides for 3 s to match". Silence or < -70 LUFS: no match. The existing A/B state swap is unchanged (parameters set via the message thread as today), so switching clicks only if parameters themselves click; trim ramp covers level.
* **S06b:** `RecipeCompare` (message thread) takes two `FillRecipe`s; sends `Command::PreviewRecipe(recipe)` alternately every `fs06_period` (1 or 2 bars, or 2 s without transport) using E-1's preview voice; the preview voice reports its measured wet loudness (integrated over the preview, in dB, via `FillEvent::PreviewLevel`); per-recipe trim is `-(louder - quieter)` capped at 6 dB, applied to the preview voice amplitude (`setup.amp`). The comparison state is UI state only; pressing **Keep A/B** locks or likes that recipe. It never writes to History.

## 4. Parameters and state
Group `fs06`, file `Source/params/fs06.h`, lock `tools/param_ids/fs06.lock`, appended to `GAPFILL_PARAMS_EXT`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fs06_match` | bool | off | - |
| `fs06_period` | choice: 1 bar, 2 bars, 2 s | 1 bar | - |
No subtree; measured values and trim are runtime only. `stateVersion` unchanged. Neutral proof: `fs06_match` off -> meter not run, trim == 1.0 (skipped), goldens A-F identical, A/B behaviour unchanged byte for byte.

## 5. Real-time safety
Biquads and accumulators only (~64 bytes of state per channel); no allocation; trim smoothing per sample. Reset: `reset()` clears history and trim to 1; sample-rate change recomputes coefficients; bypass: measurement paused; transport jump: none.

## 6. UI
Top bar: no new button. A/B group gets a small **Match** toggle inside the existing File/overflow menu entry "A/B level match" (guardrail: no new permanent header controls) and a badge shown in `FooterStatus` while active. History strip: **Compare** action in the chip action row, a small two-slot compare bar with "A" / "B" labels (text) and Keep buttons. Keyboard: arrow keys flip A/B in the compare bar; accessible names "Compare slot A recipe Stutter 1/32"; Reduced Motion safe. Help "A/B compare" order 1610.

## 7. MIDI / automation / CLAP / host
`fs06_match` automatable bool (low value); compare actions are UI commands (no MIDI). No latency.

## 8. Tests (`Tests/features/TestCompare.cpp`)
`kweight_coefficients_match_reference_48k` | `loudness_matches_reference_within_0_5LU` (M1) | `gating_ignores_silence` | `trim_attenuates_only_and_caps_6dB` (M4) | `matched_difference_under_0_7dB` (M2) | `trim_ramp_no_click` (M3) | `no_state_written` (M5) | `insufficient_data_no_trim` | `recipe_compare_alternates_on_period` | `preview_loudness_matching` (M6) | `compare_never_touches_history` | `undo_restores_previous_working_recipe` | `off_is_bit_identical_golden` | `no_audio_thread_allocation`.

## 9. Dependencies and build order
S06a standalone (needs only A/B + the output stage). S06b needs E-1 and U-1. LoudnessMeter is reusable by U-2 (export normalisation) and F-L17.

## 10. Effort and risks
~5 ideal days total. Risk: loudness of short gap-based material varies with settings; integration needs 3 s of both sides, stated in the UI; K-weighting is a perceptual proxy, not a guarantee.

## 11. Acceptance checklist
- [ ] M1-M6 recorded in `docs/progress/F-S06.md`
- [ ] No new permanent header control
- [ ] UI checklist; Help text
