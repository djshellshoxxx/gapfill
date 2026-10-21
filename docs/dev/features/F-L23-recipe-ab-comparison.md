# F-L23 Intelligent A/B recipe comparison

Status: specified, not approved. Size L (mostly integration). Category FUN/UX, value M.
Overlap and boundaries (do not duplicate):
- `03` E-1 Fill Audition: owns the preview voice, `Command::Preview`, status table and the "does / does not" rules. Reused as is.
- `05` U-1 History and `04` C-5 Lock / C-11 More Like That: own recipe storage, lock and bias. Reused.
- `10` section 4.2 "A/B audition and level-matched recipe compare" (bounded loudness estimate, click-free switching, no host-state mutation, undo-safe): this file is its implementation spec for *recipes*.
- The header A/B button already exists and compares whole plug-in states (editor-owned slots `slotA/slotB`). F-S06 adds level matching to that. This ticket is different: it generates and compares two candidate *fill recipes*.

## 1 Summary and user-visible behaviour
"Duel" in the history drawer (File menu / History strip, no new header control). Press Compare: GapFill proposes two candidate recipes A and B from the current audio (B is either a Variation of A in the same family, or a Contrast from a different family). Press A or B (or the Tab-like toggle key) to hear each through the audition voice, level matched so the louder one does not win by volume. Choose the winner: it becomes the armed Lock recipe and is added to History; "Undo pick" restores the previous Lock state. Nothing alters parameters, automation, the natural recipe stream or the main RNG.

## 2 Success / quality metric
- Level consistency: for stationary test material, K-weighted loudness of A and B previews differs by at most 1.5 dB when matching is on (and the correction needed is within +-9 dB); for the mixed test corpus at most 2.5 dB.
- Isolation: during a Duel, natural fill decisions, `gapIndex`, `fillsStarted`, history and detector state are bit-identical to a run with no Duel (test compares engine counters and output with previews muted).
- Reproducibility: same seed/audio/mode gives the same A and B recipes.
- Switch latency: A to B restart within one block; 5 ms fade of the outgoing preview; no click above -50 dBFS.
- One undo step restores the previous Lock exactly (16-byte recipe equality).

## 3 Design
Files: `Source/features/duel/RecipeDuel.{h,cpp}` (message-thread state machine), `DuelEngine.{h,cpp}` (audio-side glue, small), `LoudnessEstimate.{h,cpp}` (K-weighting + gated mean square, pure, shared with F-S06), `DuelRender.{h,cpp}` (worker), `Gui.{h,cpp}`, `Help.cpp`.

### 3.1 Candidate generation (audio thread, no RNG stream consumption)
New commands (F0-4 `Command` additions): `DuelGenerate {mode, salt}`, `PreviewRecipe {recipe, gainDb}` (the latter is the U-1 addendum; reuse). On `DuelGenerate` the audio thread runs the recipe-building stage `buildRecipe`/`chooseAlgorithm` exactly as `startPreview` does, with `Rng (hashCombine (hashCombine (seed*0x9E3779B1, ++duelCounter), kSalt_Duel))` (a separate stream; `duelCounter` reset only by `reset()`), without starting a voice. A = effective settings as they are now. B: Variation = same call with `likeRecipe = A, likeRemaining = 1` (C-11 bias, same family); Contrast = same call with weights of A's family multiplied by 0 (falls back to Variation if everything is zero). Both recipes are flagged `flags |= 1` (preview) and posted as `FillEvent::DuelCandidates {A, B, snapshotPos}` (F0-4 `fillEvents`).
Rejections reuse E-1's table; plus "Compare needs audio playing" for the same conditions.

### 3.2 Level matching (bounded estimate)
Alongside the event the audio thread schedules a source snapshot: copies the last 1.5 s of `SourceHistory` into a preallocated staging buffer, at most 4096 samples per block (same incremental pattern as C-10). When complete it posts `DuelSnapshotReady`. The worker (`DuelRender`, low-priority `juce::Thread`, own prepared `FillVoice` and `SpectralSmear` instance, all allocated in `prepare`/thread start, not on the audio thread) renders A and B offline from the snapshot with the same `toVoiceSetup/start` path and the recipe seed, then computes loudness `Lk` (ITU-R BS.1770 K-weighting: high-shelf about +4 dB near 1.7 kHz followed by the 38 Hz high-pass; mean square over samples within 25 dB of the render peak, gate to ignore tails). Gains: `target = 0.5 * (La + Lb)`, `gA = clamp (target - La, -9, +9)`, `gB = clamp (target - Lb, -9, +9)` dB (each side moves by half the difference). The result posts back as `DuelLevels {gA, gB, limited}` and is stored in two relaxed atomics read when `PreviewRecipe` is issued; until it arrives previews play unmatched and the UI says "matching..." (never blocks). The gain multiplies `VoiceSetup.amp` of the preview only (capped by the engine's existing `amp <= 1.5`; if capped, `limited` is shown). The recipe itself is never changed by matching.
Why an estimate: live previews read the live history, so material differs slightly from the snapshot; the metric above states the tolerance.

### 3.3 State machine (message thread)
`Idle -> Generating -> Rendering(levels pending) -> Ready{A|B selected} -> Picked`. Events: Compare (restarts from Generating, previous candidates discarded), SelectA/SelectB (issues `PreviewRecipe`; the engine fades the previous preview over 5 ms, per E-1), Pick (calls `LockRecipe(recipe)` and `HistoryAdd`, records `previousLock` for Undo pick), Cancel, editor close (drops state). Candidates are never serialised.

## 4 Parameters and state
No APVTS parameters. UI preferences (`levelMatch` default on, `mode` Variation/Contrast, default Variation) live in the `<Perf>` subtree from F0-6 (non-automatable, validated, dropped to defaults if invalid). Runtime only: candidates, levels, `previousLock`. Neutral proof: with no Duel started none of the new commands is issued; `Command` queue contents and engine output are unchanged (queue test + golden).

## 5 Real-time safety
Audio thread additions: handling two small commands, one recipe build (the existing bounded code path), bounded snapshot copy (<= 4096 samples/block), posting events to fixed-size SPSC queues. No allocation. Worker owns all heavy work; if it stalls, previews play unmatched. Queue overflow: `fillEvents` full drops the newest and counts it; the UI times out the Duel after 2 s with "Try again". Staging buffers 2 x 1.5 s x fs preallocated in `prepare`.

## 6 UI
History drawer section "Compare two fills" (per `11`: occasional actions in menu/drawer). Elements: Compare button; two large buttons A and B showing `describeRecipe()` text (algorithm, division, band, length) and the applied gain ("-2.3 dB match"); Mode toggle (Variation / Contrast); Level match toggle; Pick A / Pick B; Undo pick. Tooltips: A/B "Hear this fill now. Both are played at the same loudness." Pick "Use this fill as your Lock. You can undo this." Help section order 1230 "Compare fills". Accessible names "Fill A: Stutter 1/32, high band, 180 milliseconds, level match minus 2.3 decibels"; status via polite live region. Keyboard: A and B keys when the drawer is focused (also buttons in tab order). Reduced motion: no flip animation. Narrow: buttons stack. Distinct visual cue for preview (shape as well as colour, per E-1).

## 7 MIDI / automation / CLAP / host
Commands only, never automation. MIDI: bindable through F-L24 (`DuelCompare`, `DuelSelectA`, `DuelSelectB`, `DuelPick`) and U-3 note trigger rules apply (stale commands older than 500 ms discarded). Previews follow E-1 rules in stopped transport. CLAP/VST3/AU unaffected; no state/preset format change.

## 8 Tests (`Tests/features/TestDuel.cpp`)
`generation_deterministic_per_seed` | `generation_does_not_consume_main_rng` | `natural_decisions_identical_with_duel_running` | `variation_same_family` | `contrast_different_family_or_fallback` | `rejections_match_e1_table` | `snapshot_copy_bounded_per_block` | `levels_within_tolerance_on_stationary_corpus` | `level_limit_flagged_when_capped` | `matching_off_leaves_amp_unchanged` | `switch_ab_fade_no_click` | `rapid_switching_collapses_commands` | `stale_command_discarded` | `pick_sets_lock_and_history_once` | `undo_pick_restores_previous_lock_exact` | `editor_close_drops_state` | `worker_stall_plays_unmatched` | `perf_subtree_roundtrip_and_invalid_defaults` | `loudness_estimate_matches_reference_pink_noise` (BS.1770 test signals) | `no_audio_thread_allocation`.

## 9 Dependencies and order
Hard: F0-3 (`FillRecipe`), F0-4 (queues), F0-6 (`<Perf>`), F0-9, E-1 (preview voice and statuses), U-1 (History, `PreviewRecipe`), C-5 (Lock). Soft: C-11 (Variation uses its bias; fall back to re-draw with the same family table if absent). Shares `LoudnessEstimate` with F-S06. Build after U-1 and C-5.

## 10 Effort and risks
L (many couplings, small new DSP). Risks: loudness estimate vs live material mismatch (tolerance stated, correction can be learned in a later version from the live preview's own measured level); a second worker thread (lifecycle tests as F-L21); users may expect A/B to compare whole presets (Help distinguishes it from the header A/B).

## 11 Acceptance checklist
- [ ] Natural decisions and RNG unaffected during Duel (tests)
- [ ] Level tolerance met on the corpus; matching can be switched off
- [ ] A/B switching click-free and within one block
- [ ] Pick and Undo pick exact; nothing written to parameters
- [ ] Rejection and "matching..." states visible as text
- [ ] UI reviewed (drawer, keyboard A/B, screen-reader names, min size)
- [ ] Worker lifecycle verified over 30+ create/destroy cycles
