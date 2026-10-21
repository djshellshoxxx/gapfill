# F-L01 Fill Lock
Status: implementation spec, Gate C candidate (backlog `08` F-L01, existing ticket C-5). Category LIVE/VAL, value H, size L. Canonical behaviour: `04-SPEC-CREATIVE.md` C-5 (read it first; it is not repeated). This file lists what C-5 leaves open, fixes two conflicts with later rules, and makes it implementation-complete.

## 1 Summary & user-visible behaviour
- A **LOCK** toggle pins the character (algorithm, band, division, pitch, modifiers, stereo mode, length class) of the latest natural fill. Every later eligible, probability-passed gap is filled with that recipe and a fresh seed, so repeats are related but never a literal loop. Lock does not force a fill in every gap, does not replay audio and does not relax protection or fit rules (C-5 "Behaviour" and "Fit rules" apply unchanged).
- Three visible states, shown on the button and in the status text: **Off**, **Armed** (on, nothing stored yet; next natural fill is stored), **Locked** (on, recipe valid). Text: `Locked: Stutter 1/32 - High - ~180 ms`.
- Engaging with a History chip selected (F-L02) locks that chip. Fill Type forced (not Auto) overrides Lock and the UI shows "Fill Type overrides Lock".
- Gaps skipped because the locked recipe does not fit are reported with `RejectReason::LockSkipped` (E-3 timeline) rather than silently substituted.

## 2 Success/quality metric
- Character identity: over 200 qualifying gaps on the drum-loop fixture, 100 % of locked fills have the locked `algo`, `band` (when not skipped), `division`, `modifierMask`, `stereoMode`; `seed` differs in >= 99 % of consecutive pairs.
- Gap selection unchanged: in non-Spectral modes the set of gaps that start a fill with Lock on equals the set with Lock off, except gaps skipped for fit (same RNG draw count, see 3).
- Usability: from hearing a fill to locking it takes one click (button or MIDI-learned CC) with the editor closed or open.

## 3 Design
Files (cmake fragment `cmake/sources/lock.cmake`, tests `Tests/features/TestLock.cpp`):
- `Source/features/common/RecipeCodec.{h,cpp}` (created here, reused by F-L02/F-L03/F-L10): `juce::String encodeRecipe (const FillRecipe&)` = base64 of 17 bytes (16-byte recipe + CRC-8, poly 0x07, init 0, over the 16 bytes; little-endian fields, explicit serialisation, not a `memcpy` of padding); `bool decodeRecipe (juce::StringRef, FillRecipe&)` returns false on wrong length, bad base64, CRC mismatch or `! isValid (recipe)`. 24 characters.
- `Source/features/lock/LockState.{h,cpp}`: audio-thread owned `class LockState { FillRecipe recipe; std::uint8_t status; /*0 off,1 armed,2 locked*/ ... }` with
  `void onCommand (const Command&) noexcept` (LockLatest, LockRecipe, Unlock), `bool apply (FillRecipe& r, const GapContext&, Rng& sub) noexcept` (the `adjustRecipe` hook), `void onFillStarted (const FillRecipe&) noexcept` (arms -> stores), `void setEngaged (bool) noexcept`.
- `Source/features/lock/Mirror.{h,cpp}`: message-thread mirror updated from `FillEvent::LockSet`; serialises `<Lock v="1" valid="0|1" recipe="b64"/>`.
- `Source/features/lock/Gui.{h,cpp}`, `Help.cpp`, `Info.cpp`.
Algorithm (inside `buildRecipe`/`adjustRecipe`, F0-2 stages):
1. RNG: draw exactly as without Lock (band, `chance`, length-scale, `chooseAlgorithm` all still drawn and then discarded where overridden). Rationale: the per-gap `Rng` is re-seeded from the key, so skipping draws is harmless to other gaps, but keeping the draw order makes "Lock on/off picks the same gaps" testable. Spectral mode is the exception: the probability uses the locked band's `bandProb`, so equality is only asserted outside Spectral mode.
2. Replace `algo, band, division, pitchSemis, modifierMask, stereoMode` from `lockRecipe`; set `flags |= fromLock`; `mistakeType/Amt` are re-rolled by F-M13 as C-5 says; `seed = (uint32) hashCombine (key, kSalt_Lock)` (new constant in `Salts.h`, no draw from `rng`).
3. Length: `lengthSamples = min (lockedLength, usable)`; if `< 0.6 * kProfiles[algo].minMs` return `LockSkipped`. Band fallback and `NoBand` per C-5.
4. Priority order of per-fill overrides (one rule for all tickets): Fill Type forced > Lock > Like (F-L03) > Call/Response (F-L10) > normal selection. Lower ones are skipped, not merged.
5. Preview recipes (`flags & preview`) never arm or store.
6. `onFillStarted` stores the recipe only when `status==armed` or `status==locked` and the command was `LockLatest`; "latest" means the newest natural recipe in the audio-thread `lastNatural` slot, written by every started natural fill whether or not Lock is on (16 bytes, no cost).
Data flow: UI -> `commands` (SPSC, F0-4) -> `LockState` -> `FillEvent::LockSet` -> processor timer -> `Mirror`. State written on `LockSet` so the editor may be closed.
Minimal hooks in existing files: `trySchedule` (via the F0-2 `adjustRecipe` line), `FillEngine::process` command drain (one call), `PluginProcessor::parameterChanged` for `fl01_lock`, `StateSubtrees` registry entry, `Salts.h` constant.

## 4 Parameters & state
| ID | Type / range | Default | Unit | Notes |
|---|---|---|---|---|
| `fl01_lock` | bool | off | - | host-automatable, MIDI-learnable (existing learn works on any parameter) |
Naming note (needs owner OK, see decision list): this replaces the ID `fillLock` in `04`/`02` registry. No spec'd ID has shipped, so renaming costs nothing now and is forbidden later. `tools/param_ids/lock.lock` gets `fl01_lock`; the ID regex in `validate_repo.py` must be widened to `^[a-z][A-Za-z0-9_]*$` (underscore) and the JUCE `ParameterID` gets version hint 1.
State: subtree `<Lock v="1">` (<= 64 bytes). Validation: `v==1`, `valid` in {0,1}, recipe decodes (CRC + `isValid`). Any failure drops only this subtree (F0-6); with `fl01_lock==true` the state becomes **Armed**. Old sessions: no subtree and param absent -> default off. Neutral proof: with `fl01_lock` off `LockState::apply` returns on its first line, no RNG or float work; golden scenarios A-F bit-identical (`memcmp`); `lastNatural` store is a 16-byte write that cannot affect output.
Excluded from Random/Surprise/Undo: add `fl01_lock` to `performanceParameterIDs()`.

## 5 Real-time safety
Audio thread: command drain (<= 16 per block), one 16-byte copy per started fill, one struct compare in the hook. No allocation, no locks, no `String`. `FillEvent::LockSet` push is non-blocking; on a full queue the newest event is dropped and counted (F0-4), and the processor re-sends `LockSet` on the next started fill, so the mirror self-heals. Resets: `reset()`/sample-rate change keep the recipe but cancel nothing audible; transport jump keeps it; bypass keeps it.

## 6 UI
Slot `CreativeExtra` (`11`: Creative tab, small control; no header button). Padlock icon plus text "LOCK" (never icon-only), state text under it, tooltip: "Reuse the character of the last fill in every gap that gets filled. Fit rules: shortens to the gap; skips gaps that are too short; Fill Type overrides Lock." Accessible name "Fill Lock", description updates with state ("Locked, Stutter 1/32, high band, about 180 milliseconds"). Keyboard: focusable in keyboard mode, Space toggles. Reduced Motion: no pulse on Armed (static dashed outline instead). Narrow size: text collapses to icon + state word; summary moves into the tooltip. Help section "Fill Lock" registered at order 1100 via `HelpRegistry`.

## 7 MIDI/automation/CLAP/host impacts
`fl01_lock` is an ordinary parameter: automatable, CC-learnable, exposed to CLAP/VST3/AU; host automation lanes should show "Fill Lock". Toggling by automation while transport runs is allowed; the command is applied at the next block. Stored recipe is not automatable and not part of host param state; it travels in plugin state only. Offline render: lock behaviour is deterministic given seed + PPQ because the fresh seed derives from the decision key. No latency change.

## 8 Tests
Group `Lock`: `lock_off_is_bit_identical` (golden A-F) | `lock_pins_character_across_gaps` | `lock_uses_fresh_seed_per_fill` | `lock_selects_same_gaps_non_spectral` | `lock_adapts_length_to_short_gap` | `lock_skips_unfit_gap_with_reason` | `lock_band_fallback_and_skip` | `lock_armed_captures_next_fill` | `lock_ignores_preview_recipes` | `unlock_returns_to_selection` | `fill_type_forced_overrides_lock` | `lock_priority_over_like` | `recipe_codec_roundtrip_and_crc` | `recipe_codec_rejects_garbage` (fuzz: truncation, bit flips) | `lock_state_roundtrip` | `lock_corrupt_recipe_does_not_block_load` | `lock_restore_plays_nothing` | `lock_no_audio_thread_allocation` | `lock_block_size_invariant` (32/64/512/4096). Edge: 192 kHz, 32-sample block, transport jump while locked, reset while armed, queue overflow of `LockSet`.

## 9 Dependencies on other tickets
Gate B: B3 (`FillRecipe`, stages), B4 (queues), B6 (subtrees), B5 (slots). Reuses E-1 recipe `flags`. Required by F-L02 (Lock this), F-L03 (precedence), F-L07 (scene stores the boolean only, not the recipe), F-L10. Build order: after B3/B4/B6, first of the F-L group.

## 10 Effort & risks
Effort L (shared codec + engine hook + state + UI). Risks: (1) ID rename vs `04` text; (2) RNG-order promise breaks if `chooseAlgorithm` draw count differs with forced Fill Type, so the test fixes Auto; (3) Spectral-mode band semantics; (4) `LockSet` mirror staleness while the editor is closed, handled by timer-side mirror.

## 11 Acceptance checklist
- [ ] `fl01_lock` appended, locked in `lock.lock`, regex widened, old-state fixtures load with Lock off
- [ ] Golden A-F unchanged with Lock off; block-size invariance passes with Lock on
- [ ] Character identity, fit-skip reasons, band fallback tests green
- [ ] Armed/Locked/Off states visible, accessible, keyboard operable, narrow layout checked
- [ ] Corrupt `<Lock>` drops only itself; recipe codec fuzzed
- [ ] AllocGuard test green; no new audio-thread locks
- [ ] Help, tooltip and `Info.cpp` entries present
- [ ] Real-host check (REAPER Windows) for save/reopen with Lock engaged
