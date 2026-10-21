# F-S08 Glitch Roulette

Status: specified (this pass). Size S-M. Depends on F-L07 (overlay + QuantClock), Surprise (existing), Undo (existing). Extends Surprise without duplicating it.

## 1. Summary and user-visible behaviour
One **SPIN** button picks a random, coherent "glitch profile" (a bundle of fill settings) and applies it **temporarily** for 1, 2, 4 or 8 bars, then automatically returns to the previous sound. **RESTORE** returns immediately. The knobs are not changed: the profile is a temporary layer, so nothing is lost and the Undo stack is not polluted. Surprise (permanent curated change) is untouched.

## 2. Success / quality metric
Spin never repeats the previous profile; each profile keeps all protected parameters unchanged (denylist test over all profiles); restore is exact (effective settings `memcmp` equal to pre-spin); auto-restore happens on the bar boundary within one chunk; 1000 spins with the same seed sequence give the same profile sequence.

## 3. Design
`Source/features/roulette/{Profiles,Roulette}.{h,cpp}`, `Gui.*`, `Help.cpp`, `Info.cpp`.
* **Profiles (12, constexpr table of `{ id, name, {paramId, value}[] }`, values in real units; proposed, tuned by ear in the preset pass):**
  1 Stutter Storm `{fill 85, density 95, complexity 70, chaos 55, wStutter 100, wScramble 80, wReverse 20, others 0, stutterDiv 1/32}` | 2 Tape Wreck `{fill 75, intensity 80, wTapeStop 100, wPitchFall 85, wCrush 40, others 0, chaos 60}` | 3 Bit Rot `{fill 80, wCrush 100, wDistort 70, wScramble 60, complexity 75, chaos 70}` | 4 Reverse Swell `{fill 70, wReverse 100, wFilter 60, wPitchRise 50, intensity 55, chaos 20}` | 5 Granular Cloud `{fill 70, wGranular 100, wTexture 70, wFreeze 50, complexity 80, chaos 45, density 80}` | 6 Delay Throw `{fill 65, wDelay 100, wComb 40, delayDiv 1/8D, pingPong on, chaos 25}` | 7 Pitch Dive `{fill 75, wPitchFall 100, wPitchRise 30, wTapeStop 50, intensity 70}` | 8 Scramble Slicer `{fill 85, wScramble 100, wStutter 60, wReverse 40, complexity 65, chaos 65}` | 9 Spectral Smoke `{fill 60, wSpectral 100, wFreeze 60, wTexture 50, complexity 85, chaos 30, density 70}` | 10 Metal Ring `{fill 70, wComb 100, wFilter 50, wDistort 40, chaos 40}` | 11 Everything `{fill 90, density 100, complexity 90, chaos 80, all weights 70}` | 12 Half-Time Drag `{fill 55, wTapeStop 80, wReverse 60, wDelay 50, density 50, intensity 45, maxFill 600}`.
  Excluded forever: `dry, wet, output, bypass, lookahead, protect*, minGap, sensitivity, gapThresh, retrigGuard, seed, capture, fillLock`, mixer/detector/protection ids.
* **Selection:** `Rng sub; sub.seed (hashCombine (hashCombine (userSeed, kSalt_Fs08), spinCounter))`; reject equal to previous index. `spinCounter` is runtime (reset on editor-independent processor reset), so sequences are repeatable per session.
* **Application:** exactly F-L07's overlay in display-only mode (no APVTS writes); activation at the spin press (Immediate) or the next bar (`fs08_quant`); duration `fs08_bars` in bars via ppq (auto restore at `startBoundary + bars * beatsPerBar`); without transport: duration in seconds = bars * 2 s and a message "Using time, no tempo". Restore drops the overlay with a 30 ms crossfade-free design (only affects new fills; running voices keep their setup).
* Panic and F-S12 cancel it; scene recall (F-L07) cancels it; loading a preset cancels it.

## 4. Parameters and state
Group `fs08`, file `Source/params/fs08.h`, lock `tools/param_ids/fs08.lock`, appended to `GAPFILL_PARAMS_EXT`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fs08_bars` | choice: 1, 2, 4, 8 | 2 | bars |
| `fs08_quant` | choice: Now, Next bar | Now | - |
No subtree; active spin is runtime-only. Neutral proof: no spin -> empty overlay, goldens identical.

## 5. Real-time safety
Reuses overlay machinery (SnapshotBox, SPSC command); profile table is constexpr; no allocation. Reset table: reset/releaseResources cancel the spin; sample-rate none; bypass: the spin timer pauses (ppq-based), restore still works; transport jump: end boundary recomputed from original start ppq (if the jump moves outside the window the spin ends).

## 6. UI
Creative tab Advanced / Performance tab: SPIN and RESTORE buttons with a text badge "Roulette: Tape Wreck, 2 bars left". Keyboard reachable, accessible names, Reduced Motion (no spinning animation; text only). Help "Glitch Roulette" order 1550. Not added to the header.

## 7. MIDI / automation / CLAP / host
SPIN and RESTORE are MIDI-learnable actions. Not host parameters. No latency.

## 8. Tests (`Tests/features/TestRoulette.cpp`)
`profiles_use_only_allowed_ids` (denylist) | `profile_values_in_range` | `spin_never_repeats_previous` | `sequence_reproducible_for_seed` | `overlay_applied_no_apvts_writes` | `auto_restore_on_bar_boundary` | `restore_exact_memcmp` | `no_transport_uses_seconds` | `cancelled_by_panic_scene_preset` | `undo_stack_untouched` | `surprise_unchanged` | `neutral_golden`.

## 9. Dependencies and build order
After F-L07a/b (overlay and QuantClock). Profile values reviewed in the preset pass (U-10) by ear.

## 10. Effort and risks
~2.5 days. Risk: profile tuning is subjective: values are a proposal, marked for review by the owner by ear.

## 11. Acceptance checklist
- [ ] Denylist test green; exact restore
- [ ] Owner has auditioned and approved or edited the 12 profiles
- [ ] UI checklist; Help text
