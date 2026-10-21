# F-S10 Genre-inspired performance presets

Status: specified (this pass). Size S. Uses only existing algorithms and parameters. Edited only in a preset-pass PR (U-10 rule); counts toward the >= 40 factory bank.

## 1. Summary and user-visible behaviour
Eight new factory presets aimed at common live situations: breaks, dub, trance, ambient, drum and bass, house, hip-hop, techno. Each is a starting point with a descriptive name, one-line description and category from the fixed list (Subtle, Rhythmic, Glitch, Ambient, Dub, Destructive, Utility, Favourites). No new DSP, no new parameters.

## 2. Success / quality metric
Pass the U-10 quality gate (`preset_loads_valid`, `preset_produces_fills_on_drum_loop` 2..60 fills in 8 bars, `preset_output_peak_below_plus_3dBFS_and_finite`, `preset_changes_only_documented_params`, `preset_neutral_new_features_unless_intended`, `preset_names_unique_and_short`). Owner listening sign-off on each (subjective, recorded as "approved/edited" in the PR).

## 3. Design
Append to `getFactoryPresets()` in the preset-pass PR (never in feature PRs). Values are real units, only differences from defaults. `Division` indices: Whole 0, Half 1, Quarter 2, Eighth 3, Sixteenth 4, ThirtySecond 5, QuarterT 6, EighthT 7, SixteenthT 8, QuarterD 9, EighthD 10, SixteenthD 11. Proposed values (starting points, to be tuned by ear):
| Name (<= 32) | Category | Description | Values |
|---|---|---|---|
| Breaks Chop Shop | Rhythmic | Snare-roll and stutter edits between break hits. | `fill 58, intensity 60, complexity 50, chaos 38, density 72, minGap 50, maxFill 280, sensitivity 62, gapThresh 58, retrigGuard 20, protTrans 82, protNext 88, syncEnable 1, stutterDiv 4, wStutter 100, wScramble 80, wReverse 55, wSpectral 0, wFreeze 0` |
| Dub Echo Throwbacks | Dub | Tempo-synced echoes thrown from the last hit. | `fill 45, intensity 55, complexity 28, chaos 15, density 55, minGap 140, maxFill 900, syncEnable 1, delayDiv 10, pingPong 1, wDelay 100, wFilter 70, wComb 40, wReverse 30, wStutter 0, wCrush 0, wDistort 0, lookahead 60, protNext 100, wet 75` |
| Trance Riser Fills | Rhythmic | Pitch risers and reverse swells into the downbeat. | `fill 55, intensity 66, complexity 42, chaos 22, density 70, minGap 120, maxFill 700, lookahead 90, protNext 100, wPitchRise 100, wReverse 90, wFilter 60, wStutter 40, wCrush 0, wDistort 0, wSpectral 0` |
| Ambient Drift Fills | Ambient | Slow grains and spectral haze in the spaces. | `fill 35, intensity 35, complexity 70, chaos 30, density 50, minGap 220, maxFill 1400, lookahead 120, protTrans 70, protNext 100, protLow 140, wGranular 100, wSpectral 80, wFreeze 70, wTexture 80, wStutter 0, wCrush 0, wDistort 0, wScramble 0, wet 70` |
| Liquid DnB Rollers | Rhythmic | Smooth micro-rolls that keep the amen groove intact. | `fill 50, intensity 50, complexity 45, chaos 28, density 74, minGap 32, maxFill 170, sensitivity 68, retrigGuard 16, protectHold 20, gapRelease 32, lookahead 28, syncEnable 1, stutterDiv 5, protTrans 92, protNext 94, protLow 110, wStutter 100, wReverse 70, wScramble 55, wFilter 40, wCrush 0, wDistort 0` |
| House Groove Edits | Subtle | Light filter sweeps and short reverses that do not move the groove. | `fill 30, intensity 38, complexity 25, chaos 14, density 55, minGap 100, maxFill 300, syncEnable 1, stutterDiv 4, protTrans 80, protNext 90, wFilter 100, wReverse 80, wDelay 50, wStutter 40, wCrush 0, wDistort 0, wSpectral 0, wFreeze 0` |
| Hip-Hop Dust Chops | Glitch | Dusty vinyl-style stutters and tape stops after the snare. | `fill 40, intensity 48, complexity 30, chaos 24, density 56, minGap 90, maxFill 360, syncEnable 1, stutterDiv 3, protTrans 78, protNext 88, wStutter 90, wTapeStop 85, wCrush 45, wReverse 40, wFilter 50, wSpectral 0, wGranular 0` |
| Techno Rumble Gaps | Destructive | Dark comb rings and distortion rumbling between kicks. | `fill 55, intensity 62, complexity 52, chaos 36, density 68, minGap 80, maxFill 420, protLow 150, protTrans 85, protNext 82, wComb 100, wDistort 80, wFilter 70, wScramble 40, wTexture 40, wSpectral 0, wFreeze 30` |
Each preset also leaves `capture`, `seed`, `bypass`, `output` at defaults. Descriptions above are the UI text (<= 90 chars).

## 4. Parameters and state
No new IDs. Preset values may only reference existing IDs (validated by `validate_repo.py`). Old sessions unaffected.

## 5. Real-time safety
None (data only).

## 6. UI
Appears in the preset dropdown/browser under the stated categories. Help text lists new preset names if the preset list is documented there.

## 7. MIDI / automation / CLAP / host
Program lists change length: `getNumPrograms()` grows; CLAP/VST3/AU hosts re-read programs (verify in REAPER and one more host that saved projects keep their program index by name or index: indices of existing presets must not move; **append only after the existing 17**).

## 8. Tests
The U-10 gate tests above plus `genre_presets_append_only_indices_stable` (first 17 names/indices unchanged) and `genre_presets_have_category_and_description`.

## 9. Dependencies and build order
Within the next preset-pass PR after U-4 (categories). Independent of F0 except the hot-file rule.

## 10. Effort and risks
~1 day plus listening. Risks: subjective tuning; program index shifts (append-only rule); names clash with later presets (uniqueness test).

## 11. Acceptance checklist
- [ ] Owner listening sign-off (per preset)
- [ ] Gate tests green; indices stable
- [ ] Help/README preset list updated if present
