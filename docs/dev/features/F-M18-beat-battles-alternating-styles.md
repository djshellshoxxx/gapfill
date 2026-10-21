# F-M18 Beat Battles (alternating styles)

Status: specified (this pass). Size M. Depends on F-L07 (scenes, QuantClock, overlay).

## 1. Summary and user-visible behaviour
Pick two saved scenes, **A** and **B**, and let GapFill alternate between them every bar, two bars, four bars or phrase, so the fill style trades back and forth. A clear indicator shows which side is playing and the next switch. Stop the battle at any time; the original sound returns. Off by default.

## 2. Success / quality metric
Switch positions fall on the chosen boundary within one engine chunk; 64 bars with Every = 1 bar yield 64 switches in the right order; the rendered result equals a hand-scripted scene-recall sequence (bit-identical); stopping restores the pre-battle settings exactly.

## 3. Design
`Source/features/battle/Battle.{h,cpp}` (audio-thread state machine, pure with `QuantClock`), `Gui.*`, `Help.cpp`, `Info.cpp`. The battle does not own scenes; it issues the same internal `SceneRecall(indexA/B, quant = Next boundary of Every)` as F-L07, via the overlay mechanism. On **start**: remember the current base (no scene), immediately activate A on the next boundary (or Immediate if `fm18_startNow`). On each boundary: toggle. On **stop**: release overlay with a "restore base" request (the APVTS never changed, so release = drop overlay). Because the overlay also writes back to APVTS for knob display (F-L07 step 3), Battle mode sets the overlay to **display-only** (no write-back) to avoid a stream of host automation: the knobs show the base, a "Battle: B" badge shows the active side. Requires valid transport; otherwise "Needs host tempo" and no switching. Panic/F-S12 stops the battle.

## 4. Parameters and state
Group `fm18`, file `Source/params/fm18.h`, lock `tools/param_ids/fm18.lock`, appended to `GAPFILL_PARAMS_EXT`.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fm18_on` | bool | off | - |
| `fm18_a` | int 1-8 | 1 | scene |
| `fm18_b` | int 1-8 | 2 | scene |
| `fm18_every` | choice: 1 bar, 2 bars, 4 bars, Phrase | 2 bars | - |
No subtree. Empty scene slots are skipped with a message. `fm18_on` in `performanceParameterIDs()`. Neutral proof: off -> no overlay requests, goldens identical.

## 5. Real-time safety
State machine of two ints and a ppq double; overlay swaps through F-L07's SnapshotBox (no allocation). Reset events: `reset()`/`releaseResources`/bypass stop the battle (state off-runtime but the parameter stays on and restarts at the next transport play); transport jump recomputes the next boundary; sample-rate none.

## 6. UI
Performance tab card "Battle": A/B scene pickers, Every, big On/Off, live badge "A" / "B" plus "next switch: bar 5" text. Accessible names; keyboard; Reduced Motion (no sliding). Help "Beat battles" order 1540.

## 7. MIDI / automation / CLAP / host
`fm18_on` automatable and MIDI-learnable (start/stop from a controller). Needs transport/time signature.

## 8. Tests
`switches_on_each_boundary_all_every_values` | `order_A_B_A_B` | `equals_scripted_scene_recalls_bit_identical` | `stop_restores_base_exactly` | `empty_slot_skipped_with_message` | `no_transport_no_switching` | `loop_wrap_keeps_alternation_consistent` (parity computed from ppq: `index = floor (ppq / everyBeats) mod 2`, so seeks reproduce) | `panic_stops_battle` | `display_only_overlay_no_apvts_writes` | `neutral_golden`.
Use ppq parity (not a running toggle) so seek/loop/offline results are reproducible.

## 9. Dependencies and build order
After F-L07 (all three slices). No other dependents.

## 10. Effort and risks
~2.5 ideal days. Risk: display-only overlay diverges from what knobs show; the badge and a "Battle active" footer message mitigate.

## 11. Acceptance checklist
- [ ] Offline render equals scripted recalls
- [ ] Stop restores base exactly (memcmp of effective settings)
- [ ] UI checklist; unverified: real host loop playback
