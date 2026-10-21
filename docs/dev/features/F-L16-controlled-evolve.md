# F-L16 Controlled Evolve

Status: specified, not approved. Size M (on top of C-8). Category FUN/MUS, value M.
Canonical spec: `04-SPEC-CREATIVE.md` C-8 "Evolve" (parameter `evolve`, waypoint sequence, 25% blend, 0.6 pull cap, tests). Registry entry in `02` lists `evolve` only. Do not re-specify those; implement C-8 as written, then add the gaps below. This ticket is the "controlled" part: strength, transition control, recovery, visibility and well-defined interaction.

## 1 Summary and user-visible behaviour
C-8 already drifts the creative character through Surprise-profile waypoints every 8/16/32 bars with deterministic, seek-safe transitions. Added here:
1. Strength and transition-length controls (C-8 hard-codes 0.6 and 25%).
2. Recovery: "Hold" freezes the current blend; "Commit" writes the current blended creative values into the base parameters as one Undo step; Evolve Off always returns exactly to the base values (they are never modified by the overlay).
3. Visible state: current and next waypoint name, progress bar across the period, effective-value rings via C-0.
4. Defined behaviour for time-signature changes, loops, tempo changes and invalid transport.
5. Defined order against macros, personality, energy tracking (F-L22), Mutation Lab (F-L17), Lock and Like.

## 2 Success / quality metric
- `evolve` Off or `fl16_strength` 0: `EngineSettings` identical to baseline (memcmp test, as required by F0-5).
- Play-through and random-access evaluation of `profile(cycle)` and blend weight at 10 000 random PPQ values agree exactly.
- With default strength the effective Density/Fill/Intensity/Complexity/Chaos stay within 0.6 of the way between base and waypoint centre and inside parameter ranges.
- Commit then Undo restores the exact previous parameter values (bit equal normalised floats).

## 3 Design
Files: `Source/features/evolve/Evolve.{h,cpp}` (pure functions, in `Effective::applyAtDecision` per F0-5), `EvolveRecovery.{h,cpp}` (message thread), `Gui.{h,cpp}`, `Help.cpp`.

### 3.1 Gaps filled in the C-8 maths
- **Permutation:** N = `getNumSurpriseProfiles()` (<= 16). `perm(e)` = Fisher-Yates of `0..N-1` using `Rng` seeded `hashCombine (seed, e)` where `e = cycle / N`. Boundary rule: if `perm(e)[0] == perm(e-1)[N-1]` swap `perm(e)[0]` with `perm(e)[1]`. `profile(cycle) = perm(cycle / N)[cycle % N]`. Cost O(N), no state, so seek-safe. `cycle < 0` (negative PPQ) clamps to 0.
- **Beats per bar:** C-8 divides by `beatsPerBar` read per decision. A time-signature change mid-play would move period boundaries. Define: `beatsPerBar` is latched at transport start/jump (`noteTransportJump`) and when `evolve` is switched on; a signature change applies at the next latch. Missing or invalid signature: 4/4 only if `transportValid`, else neutral.
- **Pull:** `lerp (effective, target, strengthCap * w')` with `strengthCap = fl16_strength / 100 * 1.0` clamped to 0.0..1.0; default 60 reproduces C-8's 0.6. `w'` = smoothstep blend over the first `fl16_transition` percent of the period.
- **Hold:** `Hold` stores `(cycle, w')` and the decision stage keeps returning that blend (a pure function of the stored values) until released; release resumes the schedule at the current PPQ with a new transition of the same length (no jump: blend from the held values).
- **Commit:** message thread: read the current effective values (the C-0 publish, `getEffectiveCreative()`), `pushUndoSnapshot()`, write normalised values for the five creative knobs and the algorithm weights through `setValueNotifyingHost` inside change gestures, then set `evolve` Off. Seed, detector, protection, mix, latency and Fill Type are never written.
- **Precedence** in the decision stage (extends `04` two-stage table): base -> Mutation overlay (F-L17, block stage) -> personality -> macros -> **Evolve** -> Energy (F-L22) -> Breathing -> Phrase -> decision -> Like/Lock. Lock and Like act after, as in `04`.

### 3.2 Data flow
`Evolve::apply (EngineSettings& sLocal, double ppq, const TransportInfo&, const EvolveSettings&)`; `EvolveSettings` (neutral default) gains `strength01`, `transition01`, `hold` (bool plus stored cycle/weight). Publication for the UI: audio thread writes `waypointCurrent`, `waypointNext`, `progress01` as relaxed atomics once per block (`MeterState` extension, one line each).

## 4 Parameters and state
Group `FL16`, locked in `tools/param_ids/fl16.lock`. (C-8's own `evolve` stays in group EVOLVE; do not duplicate.)
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fl16_strength` | float 0..100 | 60 | % of distance to waypoint |
| `fl16_transition` | float 10..75 | 25 | % of period |
Commands (not parameters): `EvolveHold` toggle, `EvolveCommit`. Hold state is runtime only (never serialised, per F0-6). Old sessions: IDs absent, defaults reproduce C-8 exactly. Both IDs and `evolve` are in `performanceParameterIDs()` (not randomised, not touched by Surprise).

## 5 Real-time safety
Decision-stage function: fixed loops over <= 16 profiles and 5 values, no allocation, `Rng` on stack. Commands travel through the F0-4 `commands` queue (add `EvolveHold`; `EvolveCommit` is message-thread only and never enters the audio thread). Overflow: queue full drops the command and the UI shows "busy" (existing F0-4 rule). Atomics only for UI publication.

## 6 UI
Slot `CreativeExtra` (next to the EVOLVE combo from C-8): Strength and Transition knobs, `Hold` and `Commit` buttons, and a label "Now: Tape Breakdown -> Glitch Storm (62%)" with a thin progress bar. Tooltips: Strength "How far the sound is pulled toward each waypoint. 0 keeps your settings." Commit "Keep the current blended sound as your settings. You can undo this." Help section order 1160. Accessible: label announced as polite live region at waypoint change only (not per frame). Keyboard: buttons in tab order. Reduced motion: progress bar updates in steps of 10%, no easing. Narrow: label shortens to the waypoint name.

## 7 MIDI / automation / CLAP / host
Strength/Transition automatable and learnable. Hold/Commit are command bindings for F-L24. Evolve depends on valid PPQ: in hosts without PPQ the status shows "Evolve paused: no transport" (reason text, not a green tick). Offline render reproduces exactly with the same seed and PPQ.

## 8 Tests (`Tests/features/TestEvolve.cpp`)
All C-8 tests (`evolve_off_identity`, `profile_sequence_has_no_immediate_repeat`, `random_access_seek_equals_play_through`, `blend_bounds`, `ignores_protected_params`, `invalid_transport_neutral`, `surprise_unchanged_after_refactor`) plus: `strength0_identity` | `default_strength_matches_c8_0p6` | `transition_length_respected` | `timesig_change_applies_at_next_latch` | `loop_wrap_reproducible` | `tempo_change_does_not_move_cycle_index` | `hold_freezes_blend_and_release_is_continuous` | `commit_writes_exact_values_and_undo_restores` | `commit_leaves_protected_params` | `permutation_boundary_swap_unit` | `precedence_order_with_macros_and_energy` | `block_size_invariant_decisions` | `state_roundtrip_old_session`.

## 9 Dependencies and order
C-8 (and its prerequisites C-1, F0-5, C-0, F0-4, F0-6) first. Then this ticket. F-L22 and F-L17 slot into the precedence list without changes here.

## 10 Effort and risks
M. Risks: refactor of `surpriseProfiles()` (C-8 allows exactly this edit to `Presets.cpp`; Surprise tests must stay byte-identical); Commit writing many parameters at once (single gesture group, one Undo step).

## 11 Acceptance checklist
- [ ] C-8 implemented and its tests green before this work starts
- [ ] Neutral memcmp test passes for strength 0 and Evolve Off
- [ ] Seek/loop/tempo/time-signature cases deterministic
- [ ] Commit is one undo step and bit exact
- [ ] Hold and release continuous (no jump above 1% of range)
- [ ] UI states reviewed (disabled, enabled, held, no transport) at min size, keyboard, reduced motion
- [ ] IDs locked; old sessions load unchanged
