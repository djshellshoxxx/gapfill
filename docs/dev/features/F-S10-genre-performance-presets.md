# F-S10 Genre-inspired performance presets
Status: implementation spec, Gate C candidate. Category FUN/LIVE, value M, size M (mostly content). **No new DSP, no new parameters.** Context: `05-SPEC-USABILITY-VALUE.md` U-10 (preset pass owns factory data and the quality gate) and `10-SPEC...` section 4.3 (performance use). Today: 17 factory presets in `Source/Presets.cpp` (`getFactoryPresets()`, index 0 "Init"); `applyPreset` = `resetToDefaults` then overrides.

## 1 Summary & user-visible behaviour
Eight new factory presets for live use, two each for **Breaks, Dub, Trance, Ambient**, appended after the existing 17 (indexes 17-24, so saved `program` numbers stay valid). They appear under a "Performance" heading in the preset menu, step with the header arrows, and are exposed as host programs. "Genre-inspired" means a sound direction only: no artist/track/brand names, no claims of imitation.
Performance-safe rules (every preset must satisfy): never sets `lookahead` (so stepping between them never changes reported latency), `dry/wet/output/bypass`, `detSource`, `capture`, `audition`, `seed`, band crossovers; sets only creative, detection-shape, protection and weight parameters; uses the transient/next-event protections generously so a mid-set switch never steps on a hit.

## 2 Success/quality metric
- All eight pass the U-10 gate: `preset_loads_valid`, 2 <= fills in 8 bars <= 60 on the drum-loop scenario A (the two ambient presets are scored on the pad+drum scenario with the same band), peak finite and below +3 dBFS, only documented parameters changed, unique names <= 32 chars.
- Reported latency identical across all eight and equal to the default (40 ms lookahead).
- Blind listening note by the owner: each preset is recognisably its genre direction (recorded in `docs/progress/F-S10.md`; subjective, not a CI gate).

## 3 Design
- Data lives in a new file so the preset-pass hot file is not edited for content: `Source/features/genrepresets/GenrePresets.cpp` exposing `const std::vector<Preset>& getPerformancePresets()` (same `Preset{ name, description, values }` struct from `Presets.h`). Add `category` support through U-10's field when it exists; until then a constant `kPerformanceFirstIndex = 17` and a menu heading.
- One minimal hook in `Source/Presets.cpp`: `getFactoryPresets()` builds its static vector as `core` followed by `getPerformancePresets()`. This is the single allowed edit to that file by this ticket; **owner decision** (U-10 reserves factory data to the preset pass): allow the hook now, or fold the table into the U-10 pass and ship this ticket as pure content then. `PluginEditor::refreshPresetList` adds the "Performance" section heading using `kPerformanceFirstIndex`.
- `tools/validate_repo.py` currently scans only `Source/Presets.cpp` for preset parameter IDs (regex `PRESET_PARAM_RE`); extend it to scan `Source/features/genrepresets/*.cpp` too, and add a check that every performance preset avoids the forbidden IDs above.
- Content (values in real units; unspecified parameters stay at defaults; Division indexes: Eighth 3, Sixteenth 4, ThirtySecond 5, EighthT 7, SixteenthT 8, EighthD 10; `fillType` 0 = Auto):

| Name (<= 32) | Description | Key values |
|---|---|---|
| Breaks: Chop Shop | Tight stutters and reverses in the gaps of a breakbeat. | fill 62, intensity 60, complexity 52, chaos 38, density 80, minGap 40, maxFill 240, sensitivity 62, gapThresh 60, retrigGuard 22, protectHold 28, protTrans 85, protNext 88, protLow 100, stutterDiv 5, delayDiv 4, wStutter 100, wReverse 80, wScramble 85, wTapeStop 40, wCrush 20, wDistort 10, wSpectral 0, wFreeze 0, wComb 0 |
| Breaks: Rewind Roll | Reverse swells and tape stops that pull the loop back. | fill 48, intensity 55, complexity 35, chaos 25, density 60, minGap 90, maxFill 520, protTrans 80, protNext 92, protLow 100, wReverse 100, wTapeStop 85, wPitchFall 50, wFilter 40, wStutter 35, wCrush 0, wDistort 0, wSpectral 0, wComb 0 |
| Dub: Echo Throw | Filtered echoes thrown off the last hit. | fill 44, intensity 42, complexity 30, chaos 14, density 55, minGap 140, maxFill 700, delayDiv 10, stutterDiv 10, pingPong 1, width 150, protNext 90, protLow 140, wDelay 100, wFilter 80, wComb 45, wReverse 30, wStutter 10, wCrush 0, wDistort 0, wSpectral 0 |
| Dub: Tape Wobble | Slow tape bends and soft echoes in the space. | fill 40, intensity 48, complexity 28, chaos 20, density 50, minGap 160, maxFill 800, delayDiv 7, protNext 90, protLow 120, wTapeStop 80, wPitchFall 60, wDelay 70, wFilter 70, wReverse 25, wStutter 0, wCrush 0, wDistort 0, wSpectral 0 |
| Trance: Riser Lift | Phrase-end risers and filter sweeps. | detMode Phrase (1), fill 46, intensity 64, complexity 55, chaos 22, density 52, minGap 240, maxFill 1600, maxFillSync 1, maxFillDiv 1, gapRelease 160, protNext 95, protLow 100, width 140, wPitchRise 100, wFilter 90, wSpectral 55, wFreeze 40, wGranular 20, wStutter 15, wCrush 0, wDistort 0, wScramble 0 |
| Trance: Gate Chop | Rhythmic sixteenth stutters that tighten the groove. | fill 56, intensity 58, complexity 40, chaos 18, density 72, minGap 50, maxFill 300, stutterDiv 4, delayDiv 4, protTrans 85, protNext 90, protLow 100, wStutter 100, wFilter 60, wScramble 40, wCrush 30, wReverse 20, wSpectral 0, wFreeze 0 |
| Ambient: Frozen Wash | Held textures and spectral smears between notes. | detMode Phrase (1), fill 36, intensity 36, complexity 70, chaos 22, density 40, minGap 200, maxFill 1400, width 170, protTrans 70, protNext 92, wFreeze 100, wSpectral 85, wGranular 90, wTexture 60, wReverse 40, wStutter 0, wCrush 0, wDistort 0, wScramble 0, wComb 0 |
| Ambient: Granular Cloud | Soft grain clouds that drift in the gaps. | fill 40, intensity 30, complexity 62, chaos 32, density 48, minGap 160, maxFill 900, width 160, protTrans 70, protNext 90, wGranular 100, wTexture 90, wReverse 55, wFreeze 30, wPitchRise 25, wPitchFall 25, wStutter 0, wCrush 0, wDistort 0, wScramble 0 |

Descriptions are final copy. Values are a starting point to be tuned by ear within the stated rules; ranges are validated by tests, not by this table.

## 4 Parameters & state
No new APVTS IDs (reserved `fs10_` unused). No new state. Existing sessions are untouched because presets only run on user selection; `program` indexes 0-16 keep their meaning; indexes 17-24 are append-only and permanent once released (a lock list `tools/preset_names.lock` of factory names in order prevents reorder). Neutral proof: loading Init (0) or any old preset yields exactly today's parameter values (test compares all 66 values pre/post hook); host `getNumPrograms()` grows 17 -> 25 (documented).

## 5 Real-time safety
Preset application is message-thread (`selectPreset` -> `applyPreset`) exactly as today; nothing new on the audio thread. Switching presets mid-playback relies on existing parameter smoothing; because `applyPreset` first resets to defaults then sets values one by one, a block may briefly see a half-applied state (existing behaviour; characterized, not changed here).

## 6 UI
- Placement: existing preset menu gains a "Performance" heading; header arrows (existing) step through all 25. No new controls (docs/dev/11).
- Tooltips/descriptions: each preset's one-line description appears as menu item tooltip; Help section "Performance presets" (order 1100) explains the rules above and that mix and latency are left as they were set by Init defaults.
- Accessibility: menu items have names and descriptions; heading is announced. Reduced motion unaffected. Narrow: long names (<= 32) truncated with ellipsis, full in tooltip.

## 7 MIDI/automation/CLAP/host impacts
Hosts list 25 programs in order; MIDI Program Change mapping (F-S11 option) can reach 0-24. Programs changed from the host's own list use `setCurrentProgram`, same path as now. Presets referencing parameter IDs that older plug-in versions lack are impossible (IDs are unchanged). CLAP/VST3/AU preset-menu behaviour follows the program list.

## 8 Tests (`Tests/features/TestGenrePresets.cpp`)
`performance_presets_load_valid` | `performance_presets_produce_fills_on_drum_loop` (2-60 per 8 bars) | `performance_presets_peak_finite_below_3dBFS` | `performance_presets_change_only_documented_params` (forbidden-ID list) | `performance_presets_do_not_change_latency` | `performance_preset_names_unique_and_short` | `performance_preset_values_in_range` | `old_presets_unchanged_by_hook` | `program_indexes_stable` | `preset_name_lock_matches` | `validate_repo_scans_genre_file` (Python test in `tools/test_validate_repo.py`) | `step_through_all_25_with_arrows_wraps` | `preset_switch_during_playback_no_nan` (alternating blocks). Fixtures: scenario A drum loop, pad+drum mix, 44.1-192 kHz.

## 9 Dependencies & build order
Soft: U-10 (quality gate, category field), F-S11 (favourites/stepping UX). No hard dependency; it can ship any time after Gate B. If U-10 runs first, the table merges into it unchanged.

## 10 Effort & risks
M (content/tuning heavy, code is small). Risks: subjective quality (owner listening pass), user confusion that mid-set switching resets mix to Init defaults (existing `applyPreset` behaviour; call out in Help), second place for factory data (mitigated by the lock list and validator), name collisions with user presets. Owner decisions: allow the `Presets.cpp` hook now vs wait for U-10; final preset names.

## 11 Acceptance checklist
- [ ] Eight presets appended at indexes 17-24; old indexes and values unchanged
- [ ] U-10 gate tests pass for all eight; latency identical across presets
- [ ] Forbidden-parameter rule enforced by test and validator; validator scans the new file
- [ ] Owner listening sign-off recorded
- [ ] Help text and preset-menu heading in place; screenshot of menu at minimum size
- [ ] No new APVTS IDs; golden output unchanged for existing presets
