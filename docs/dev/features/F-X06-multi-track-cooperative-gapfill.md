# F-X06 Multi-track cooperative GapFill

Status: specified (this pass), not implemented. Size L (MVP) + stretch. Depends on F0-5 (`applyAtDecision`, `TransportInfo`), Gate B, E-2 transport correctness (W-03). In-process only: no networking, no files, no OS IPC.

## 1. Summary and user-visible behaviour
When several GapFill instances run in the same host process (drums, bass, keys, vocals), they normally fill gaps independently and often at the same moment. With **Cooperate** on, instances that share a **Group** (A-H) take turns: the song is divided into small musical slots (default one bar) and, for each slot, exactly one member of the group is the "owner" that may fill at full density; the others reduce their fill probability by the **Yield** amount. Ownership is a deterministic, seeded function of the slot number and the members' priorities, so playback and offline renders are reproducible and no real-time messages flow between tracks. If a host runs each plugin in a separate process, or the transport is unavailable, cooperation pauses with a clear message and each instance behaves exactly as before. Off by default.

## 2. Success / quality metric
| # | Metric | Target |
|---|---|---|
| M1 | 4 instances, group A, equal priority, Yield 100, grid 1 bar, 64 bars of the same drum loop: bars in which more than one instance starts a fill | 0 of 64 owner-eligible bars (a non-owner never starts a fill while an owner exists) |
| M2 | Ownership share vs priority share over 2000 slots | within +-3 percentage points |
| M3 | Determinism: two renders with instances processed in different orders, different block sizes (64 vs 512), on 1 and 4 threads | bit-identical per instance |
| M4 | Minimal disruption: removing one non-owner member | owner of every other slot unchanged |
| M5 | Registry operations (claim/release under 16-thread stress, TSan) | no lost slot, no duplicate live token, no crash |
| M6 | CPU and memory | <= 0.02 % CPU; registry 16 x 16 B static |

## 3. Design
### Modules (`Source/features/coop/`)
`CoopRegistry.h/.cpp` (process-wide static table), `Ownership.h/.cpp` (pure functions), `CoopSettings.h`, `Gather.cpp`, `Gui.*`, `Help.cpp`, `Info.cpp`.

### Registry: cross-instance sharing with lifetime rules
```cpp
struct PeerSlot {                            // trivially destructible, constant-initialised
    std::atomic<std::uint32_t> state {0};    // 0 free, 1 claiming, 2 live
    std::atomic<std::uint32_t> token {0};    // process-unique, never 0
    std::atomic<std::uint8_t>  group {0}, slot {0}, priority {50};
    std::atomic<std::uint32_t> beat {0};     // diagnostic heartbeat (ms counter), never used for decisions
};
struct CoopRegistry {
    static constexpr int kMaxPeers = 16;
    PeerSlot peers[kMaxPeers];
    std::atomic<std::uint32_t> version {0};  // bumped on every membership/setting change
    std::atomic<std::uint32_t> nextToken {1};
    static CoopRegistry& get() noexcept;     // function-local static; constexpr ctor + trivial dtor => no init-order and no unload hazard
};
class PeerHandle {                           // RAII, one per processor, message-thread owned
    bool claim (std::uint8_t group, std::uint8_t slot, std::uint8_t priority) noexcept;   // CAS 0 -> 1, fill, store 2 (release); false if full
    void update (std::uint8_t group, std::uint8_t slot, std::uint8_t priority) noexcept;
    void release() noexcept;                 // store 0; also in the destructor
};
struct MembersSnapshot { int n; struct M { std::uint32_t token; std::uint8_t slot, priority; } m[16]; std::uint32_t version; };
MembersSnapshot snapshot (std::uint8_t group) noexcept;   // acquire loads of live entries only
```
Rules: (1) the registry holds **plain data only**, never pointers to processors, so a destroyed instance cannot leave a dangling reference; (2) an instance claims a slot when `fx06_group` leaves Off (and `prepareToPlay` has run), updates on parameter changes (message thread, `parameterChanged`), and releases on group Off, `releaseResources`, and in the processor destructor; (3) hosts may create many instances for scanning: they never claim until prepared and grouped; (4) a full table (17th member) returns false and the UI shows "Group is full (16)"; (5) `static` lives per loaded module image: VST3, CLAP, AU and Standalone builds each have their own registry, and sandboxed/bridged hosts that load plugins in separate processes see only themselves: the UI shows "Peers: 1 (instances may be sandboxed)"; (6) no destruction-order dependence: the registry is never destroyed in a way that matters because it is trivially destructible.

### Slot and owner election (pure, deterministic)
* Slot index `n = floor(ppq / gridBeats)`; `gridBeats` from `fx06_grid` (1 beat, 2 beats, 1 bar, 2 bars, 4 bars; bar length from `TransportInfo` time signature, else 4 beats). Decision at fill time uses the exact PPQ at the decision sample (same value as the scheduler's key), so block size cannot move it. Requires `TransportInfo.valid`; otherwise cooperation is paused (neutral).
* Weighted rendezvous hashing: for each member `i` in the group, `u_i = hash01 (hashCombine (hashCombine (kSalt_Fx06, groupId), n), slot_i)` mapped to (0,1), `w_i = max (0.05, priority_i / 100)`, score `u_i ^ (1 / w_i)`; owner = highest score (ties broken by lower slot). This gives P(owner = i) proportional to `w_i`, and removing any member only reassigns the slots it owned.
* **Membership refresh** happens at slot boundaries from `snapshot()`; the member list is cached in the instance and only read at a boundary, so every instance applies a change at the same musical position.
* **Effect on the decision** (`applyCooperation (EngineSettings& sLocal, double ppq, const TransportInfo&, const CoopState&)`, decision stage after the arrangement modulators): if not owner, `sLocal.density01 *= (1 - yield01)` and `sLocal.fill01 *= (1 - 0.5 * yield01)`; the owner is unchanged. Probability is reduced, never raised, so cooperation cannot create fills the instance would not otherwise make.
* No RNG draws; the election uses a hash only. No data other than group/slot/priority is read from peers, so the order in which a host processes tracks (including multithreaded) cannot influence any decision.

### Slot identity
`fx06_slot` (1-8) must be unique within a group for determinism. When a group is joined and the slot is taken, the processor writes the lowest free slot into `fx06_slot` (message thread, host-notified). If duplicates appear later (merged projects), the UI shows "Slot 2 is used twice" with a one-click "Fix", and the election treats duplicates as one member (the one with the lower token wins, flagged non-reproducible in the UI).

## 4. Parameters and state
Group `fx06`, file `Source/params/fx06.h`, lock `tools/param_ids/fx06.lock`, appended to `GAPFILL_PARAMS_EXT`. `fx06_group`, `fx06_slot` and `fx06_grid` are marked not automatable.
| ID | Type / range | Default | Unit |
|---|---|---|---|
| `fx06_group` | choice: Off, A, B, C, D, E, F, G, H | Off | - |
| `fx06_slot` | int 1-8 | 1 | - |
| `fx06_priority` | float 0-100 | 50 | % |
| `fx06_yield` | float 0-100 | 70 | % |
| `fx06_grid` | choice: 1 beat, 2 beats, 1 bar, 2 bars, 4 bars | 1 bar | - |
* No subtree. `fx06_group` and `fx06_slot` are in `performanceParameterIDs()`. `stateVersion` unchanged; old sessions: Off.
* Neutral proof: group Off -> no registry traffic at all (no claim), `applyCooperation` returns immediately (identity by `memcmp`), goldens A-F unchanged. Group set but alone in the group (peers = 1): the member is always the owner -> identity.

## 5. Real-time safety
The audio thread never touches the registry: it reads only the cached `CoopState { bool active, owner; }` computed at the slot boundary from the cached member list. The cached list is refreshed from `snapshot()` at the boundary: a bounded loop over 16 `PeerSlot`s with acquire loads, no locks or allocation, ~100 ns. Claim/update/release happen on the message thread only. Resets: `reset()` and `releaseResources` clear the cache and release the claim; sample-rate change: slot arithmetic uses PPQ, so unaffected; bypass: the instance stays a member (so others keep their slots) but its own decisions are not made; transport jump: slot recomputed from the new PPQ; stopped/invalid transport: paused. The 16-entry table and function-local static need no destruction.

## 6. UI
Creative tab Advanced, foldable **Cooperate** card (`CreativeExtra`): Group combo, Slot stepper, Priority, Yield, Grid. Status line (text, never colour-only): "Group A: 3 tracks, you are slot 2. This bar: slot 3 fills" / "Cooperation paused: no tempo from host" / "Peers: 1 (your host may run plugins in separate processes)" / "Slot 2 used twice - Fix". Footer shows "Yielding" only while hovering the card. Reduced Motion: the bar-owner indicator changes instantly without animation. Accessible names and keyboard path per control; Help section "Cooperate" order 1750 explains determinism and the sandbox limitation.

## 7. MIDI / automation / CLAP / host
Group/slot/grid are stored in host state but not exposed as automatable; priority and yield are automatable. Works in VST3, CLAP, AU and standalone instances within the same module image; cross-format cooperation is not possible and is documented. Hosts that run plugin instances in separate processes (some sandboxed modes) simply show one peer. Offline bounce: all instances process the same PPQ grid, so results match realtime playback exactly (the point of the pure election). No MIDI.

## 8. Tests (`Tests/features/TestCoop.cpp`, group `Coop`)
`registry_claim_release_roundtrip` | `registry_full_refuses_17th` | `registry_stress_16_threads_tsan` (M5) | `destructor_releases_slot` | `no_pointers_in_slot_layout` (static_assert trivially copyable/destructible) | `snapshot_only_live_entries` | `election_deterministic_same_inputs` | `election_share_matches_priority` (M2, chi-square) | `election_minimal_disruption_on_member_removal` (M4) | `election_ties_lower_slot` | `slot_index_from_ppq_bar_grid_all_time_signatures` | `paused_when_transport_invalid` | `single_member_is_always_owner` | `non_owner_density_reduced_never_raised` | `four_instances_no_simultaneous_fills_owner_bars` (M1, uses TestRig with 4 processors) | `processing_order_independent` (M3: shuffled order, 1 and 4 threads) | `block_size_independent` (64 vs 512) | `membership_change_applies_at_slot_boundary` | `duplicate_slot_detected_and_fix_assigns_lowest_free` | `group_off_has_zero_registry_traffic` | `off_is_bit_identical_golden_A_to_F` | `state_roundtrip_old_state_off` | `no_audio_thread_allocation` | `offline_equals_realtime_chunking`.

## 9. Dependencies and build order
F0-5 (decision-stage hook, `TransportInfo`), E-2/W-03 transport correctness, `Salts.h` (`kSalt_Fx06`). Standalone module, no other feature depends on it. Registry first (with TSan tests), then election, then hook and UI. The Test rig needs a helper that constructs several `GapFillAudioProcessor`s in one process.

## 10. Effort and risks
~6 ideal days (registry 1.5, election 1, hook/params 1, UI 1, tests 1.5). Risks: (a) hosts that sandbox or bridge: documented fallback, no failure mode; (b) users expect real-time "listening" between tracks: this design deliberately avoids it for determinism, stated in Help; (c) an instance on a muted track still counts as a member and "owns" slots in which it is silent: surfaced in the status line and handled by giving the user Priority 0 (weight 0.05) or Group Off; (d) JUCE parameter writes from `parameterChanged` for slot fix: done on the message thread only. Stretch (separate tickets, non-deterministic, opt-in, labelled "live, not reproducible"): per-fill claim messages in a seqlock ring so instances can avoid overlapping tails, cross-track call and response, shared tempo/phrase estimate, group-wide scene recall (F-L07).

## 11. Acceptance checklist
- [ ] Group Off: no registry traffic; goldens A-F bit-identical
- [ ] M1-M6 recorded in `docs/progress/F-X06.md`; TSan green
- [ ] Determinism under shuffled processing order and different block sizes
- [ ] Sandboxed-host fallback message verified in one real host that supports process isolation (unverified item otherwise)
- [ ] UI checklist (tooltips, names, keyboard, Reduced Motion, min size, Help)
- [ ] Registry layout reviewed for ABI: trivially destructible, no pointers
