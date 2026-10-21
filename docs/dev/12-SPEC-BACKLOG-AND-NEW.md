# 12 - Specs for the remaining backlog and six new features

> **Precedence note (PR #25 takeover).** `docs/dev/features/*` and `docs/dev/features/DECISIONS.md` are the source of truth. Where this document differs, they win: in particular DECISIONS 1 (new parameter IDs are `<ticketlower>_<name>`; the unprefixed IDs in the 02 registry table are provisional names) and DECISIONS 3, 4 and 14. The empty parameter groups pre-registered by F0-10 are reserved slots; a ticket uses the group that matches it (or appends a new group at the end).

Status: **authorized for implementation** (owner, 2026-10-21: beta1 shipped on all platforms; every catalogued item may be built). Gates A/B in `08` stay the *quality bar* for each PR (CI green, goldens, neutral defaults) but no longer block feature work. Features that need Phase 0 seams still wait for `02` (F0-1..F0-10).

This document gives every Gate C candidate (`08`), every doc-11 candidate, every `05` "Backlog" item, the `10` effects/live features and `04` C-8 a **full, implementable spec**, plus six new features (N-*). Where `03`/`04`/`05`/`10` already hold a complete spec, §1 only points to it. Code facts used below were checked against the repo at this commit: 15 algorithms already exist (`Reverse, Stutter, Delay, Granular, Freeze, PitchRise, PitchFall, Filter, Texture, Crush, Distort, Scramble, Spectral, Comb, TapeStop`), `FillEngine::panic()` exists (4 ms voice fade, used on bypass/transport stop/jump), `FillVoice` has 16 grains and 5 modifiers (incl. Crush), `audition` is an **existing parameter** (detector monitor; do not reuse that ID), `detSource` already offers Main/Sidechain/Hybrid. **Reimplement none of these; extend them.**

## 0. Conventions and the common contract (C1-C10)

Sizes S/M/L/XL as in README. A spec lists: Goal, Behaviour, Params (IDs/ranges/defaults live in the `02` registry; semantics here), State, RT/threading (deltas to C3), GUI (placement per `11`), Tests, Acceptance. Everything below applies to **every** ticket unless it says otherwise.

* **C1 Neutral.** Every new parameter defaults to a value that leaves output bit-identical (golden tolerance 0 on neutral) to the previous build. An effect/feature with `*Amount = 0` (or Off) MUST return before touching state, buffers, RNG or latency. Test per ticket: `<id>_neutral_identity` (golden + `memcmp` of `EngineSettings`/`FillRecipe`).
* **C2 Determinism.** Random choices use a per-feature sub-RNG `hashCombine(key, kSalt_X)` (`Source/features/Salts.h`, table in §2.4); a feature never draws from the legacy `rng`. Decisions use `ppq*96`-style keys and block-size-independent inputs. UI actions never touch the audio RNG. Test: `block_size_invariant` (32/64/512/4096/8192) for every decision-affecting feature.
* **C3 Real time.** Allocation, locks, file/network IO, logging, GUI access: forbidden in `processBlock`. All buffers sized in `prepareToPlay` for the worst case (192 kHz, max block); every loop bounded by a compile-time constant; queues fixed-capacity with defined overflow (drop newest + counter). NaN/Inf/denormal guard on every feedback path (`sanitise`, DC blocker, flush-to-zero). Sample-rate change / `reset()` re-initialises state without allocation beyond `prepare`. Each ticket states its worst-case memory; PR records measured `sizeof` and CPU delta (`06` budgets).
* **C4 State.** Automatable things are APVTS parameters (IDs append-only, `02` registry). Everything else is a versioned optional subtree (`02` F0-6): validated whole, dropped alone on error, size-capped, never containing audio. This document moves `stateVersion` to **3** (one bump for all doc-12 subtrees; absent subtree = defaults; v2->v3 sets nothing). Total subtree budget becomes **64 KiB** (edit to F0-6). Disk IO only on the message thread or a worker.
* **C5 GUI.** Placement per `11`: no new permanent header row; effects live in the **Effects section** (foldable cards, hidden controls until enabled); live operation in the **Performance tab** (or *More* menu if the tab strip would crowd the minimum size - decided in UI-0 from screenshots); files/exchange in the File menu; short status in the footer. Every control: tooltip + ParamInfo entry (U-9), accessibility title/description, opt-in keyboard focus, Reduced Motion, correct at minimum size, Help section via registry, no colour-only state.
* **C6 Tests.** `Tests/features/Test<Feature>.cpp` + `cmake/tests/<feature>.cmake`; each ticket lists names; minimum set: neutral identity, state round trip, old-state load, malformed-subtree drop, bounds with NaN/extreme inputs, block-size invariance, panic/bypass/sample-rate-change behaviour, no-allocation guard where the audio thread is touched. Update `docs/TEST-TRACEABILITY.md`.
* **C7 Reset contract.** Every stateful DSP object implements `reset()` (hard clear), `fadeOut(n)` (soft, <= 4 ms default) and `isIdle()`. Panic (N-USE-a), bypass, transport stop/jump and sample-rate change call them; no tail may survive a panic.
* **C8 Latency.** A feature MUST NOT change reported latency unless its spec says so; any latency change goes through the existing latency crossfade and is listed in the PR.
* **C9 Randomise set.** All new IDs are *excluded* from Random / Random All / Surprise (they join `performanceParameterIDs()`), as in `02`; Undo/A-B snapshot membership follows the existing `02` rule (list-based, unchanged). Scene scope: §F-L07.
* **C10 Docs per ticket.** Help section, ParamInfo entries, `docs/progress/<ticket>.md`, `COMPLIANCE.md` row if a compliance area changes, TEST-TRACEABILITY row, `11` decision-record entry. Anything not run on a real DAW/OS/device is listed *unverified*.

## 1. Coverage map (every candidate -> canonical spec)

"Doc 12" = section below. IDs are the ticket IDs; `08`'s `F-xNN` IDs are kept as ticket IDs.

| Item (source) | Canonical spec | Size |
|---|---|---|
| F-S01 CPU/latency display (08) | 12 F-S01 | S |
| F-S02 parameter info/status (U-9) | 05 U-9 | M |
| F-S03 UI scale (U-8) | 05 U-8 | M |
| F-S04 MIDI note audition (U-3) | 05 U-3 | S |
| F-S05 effective-value feedback (C-0) | 04 C-0 | S |
| F-S06 A/B level-matched compare, F-L23 recipe A/B (10 §4.2) | 12 F-S06 | M |
| F-S07 Stutter Storm | 12 F-S07 | S |
| F-S08 Glitch Roulette | 12 F-S08 | S |
| F-S09 first-run guide (U-7) | 05 U-7 | M |
| F-S10 genre starter presets | 12 F-S10 (delivered in U-10 passes) | S |
| F-S11 favourites / next-prev | 05 U-4 + 12 X-1 (MIDI) | in U-4 |
| F-S12 panic / safe return, F-L25 safety suite | 12 N-USE-a, F-L25 | M |
| F-M01 Audition (E-1) | 03 E-1 | L |
| F-M02 reasons (E-3) | 03 E-3 (+ new reasons §2.2) | L |
| F-M03 gap timeline (10 §4.1) | 12 F-M03 | M |
| F-M04 Simple/Advanced (U-5) | 05 U-5 | L |
| F-M05 intensity envelope (C-4) | 04 C-4 | M |
| F-M06 personalities (C-1) | 04 C-1 | M |
| F-M07 density breathing (C-6) | 04 C-6 | M |
| F-M08 Capture v2 (C-10) | 04 C-10 | M |
| F-M09 per-band type (C-9) | 04 C-9 | M |
| F-M10 momentary / latch | 12 F-M10 | M |
| F-M11 quantized scene/preset change | 12 F-L07 (§quantised apply) | M |
| F-M12 preset browser (U-4) | 05 U-4 | L |
| F-M13 Mistakes (C-3) | 04 C-3 | L |
| F-M14 rhythmic bitcrush (10 §5.2) | 12 F-M14 | M |
| F-M15 comb resonator (10 §5.4) | 12 F-M15 | M |
| F-M16 rhythmic ratchet (10 §5.1) | 12 F-M16 | M |
| F-M17 wet-only output mode | 12 F-M17 | S |
| F-M18 Beat Battles | 12 F-M18 | M |
| F-M19 Freeze & Scrub | 12 F-M19 | M |
| F-M20 tape destruction (10 §5.3) | 12 F-M20 | M |
| F-M21 tension/release curves (10 §4.4) | 12 F-M21 | L |
| F-M22 smart preset suggestions | 12 F-M22 | M |
| F-M23 advanced modulation/macro targets | 04 C-2 + 12 F-M23 (delta) | M |
| F-L01 Fill Lock (C-5) | 04 C-5 | L |
| F-L02 History (U-1) | 05 U-1 | L |
| F-L03 More Like That (C-11) | 04 C-11 | L |
| F-L04 Export (U-2) | 05 U-2 (+10 §4.5) | L |
| F-L05 Auto-Calibrate (U-6) | 05 U-6 | L |
| F-L06 macros (C-2) | 04 C-2 | XL |
| F-L07 performance scenes | 12 F-L07 | L |
| F-L08 phrase awareness (C-7) | 04 C-7 (+`phraseOffset`) | M |
| F-L09 groove inheritance | 12 F-L09 | L |
| F-L10 call and response | 12 F-L10 | L |
| F-L11 MIDI output from detection | 12 F-L11 | L |
| F-L12 wet-only output bus | 12 F-L12 | L |
| F-L13 sidechain-driven fills | 12 F-L13 | L |
| F-L14 multiband effect routing | 12 F-L14 (builds on C-9) | M |
| F-L15 dynamic dub feedback (10 §5.6) | 12 F-L15 | L |
| F-L16 Evolve (C-8) | 04 C-8 (+§F-L16 addendum below) | L |
| F-L17 Mutation Lab | 12 F-L17 | M |
| F-L18 scale/key awareness - manual | 12 **N-MUS** | M |
| F-L19 frequency-shifted delays (10 §5.5) | 12 F-L19 | L |
| F-L20 formant (10 §5.7) | 12 F-L20 | L |
| F-L21 transient-derived convolution | 12 F-L21 | L |
| F-L22 musical energy tracking | 12 F-L22 | L |
| F-L24 controller integration | 12 X-1 (+feedback) | M+L |
| F-L25 live safety suite | 12 N-USE-a + F-L25 | M |
| F-X01 harmonic granular resynthesis | 12 F-X01 | XL |
| F-X02 spectral freeze/morph (10 §5.8) | 12 F-X02 | XL |
| F-X03 automatic key detection | 12 F-X03 | XL |
| F-X04 multi-section awareness | 12 F-X04 | XL |
| F-X05 preference learning | 12 F-X05 | L |
| F-X06 multi-instance cooperation | 12 F-X06 | XL |
| F-X07 adaptive quality | 12 F-D03 | (in F-D03) |
| 11: transport/sidechain health | 12 **N-USE-b** | M |
| 11: per-bar fill budget / quiet zones | 12 F-D01 | M |
| 11: scene bank next/prev/quantized | 12 F-L07 + **N-VAL** | L+L |
| 11: panic and recover | 12 **N-USE-a** | M |
| 11: bank exchange | 12 **N-VAL** | in N-VAL |
| 11: event capture/replay | 12 F-D02 | L |
| 11: CPU/latency budget mode | 12 F-D03 | M |
| 11: accessibility/contrast presets | 12 F-D04 | M |
| 05 backlog: Free/Pro split | 12 F-P01 (decision) | S |
| 05 backlog: quantised Fill Now | 12 F-P02 | M |
| 05 backlog: fill morphing/rhythm memory | **Rejected** (§2.5), covered by F-L10 | - |
| 05 backlog: sidechain-routed fill source | 12 F-L13 | L |
| 05 backlog: localisation | 12 F-P04 | M |
| 05 backlog: online preset library | **Rejected** (§2.5); offline exchange = N-VAL | - |
| 05 backlog: AAX / AUv3 / iOS | 12 F-P03 (decision + entry criteria) | S |
| 03 E-4 CI, E-5 macOS/AU | **Done** (beta1); E-5c/E-6 remain | see §2.6 |
| 03 E-6 signing, E-7 real-host QA | 03 E-6/E-7 + 12 §2.6 | M, process |
| New: Reverse Swell / Granular Cloud | 12 **N-FX1 / N-FX2** | M / M |
| New: Panic+Health / Scale Lock / Scene Bank / Fix-it | 12 **N-USE / N-MUS / N-VAL / N-EASY** | M+M / M / L / M |

Overlap check done for the six new features: *Reverse* and *Granular* algorithms already exist, so N-FX1/N-FX2 are **render modes of those algorithms** (flag on the recipe), not new algorithms; `FillEngine::panic()` exists, so N-USE-a adds the user-facing hold/recover state and MIDI path, not a new fade; `detSource` already has Sidechain/Hybrid, so health reports on it; no scale/key, scene-bank or suggestion code exists.

## 2. Foundation additions and decisions

### 2.1 F0-10 Pre-registration of everything in this document   (size S-M, part of the Phase 0 PR train)
So that no feature PR edits a hot file, Phase 0 also creates, empty/neutral:
1. **Parameter groups** (permanent order, appended after the `02` list; each `GAPFILL_PARAMS_<G>(X)` defined empty, plus `add<G>Parameters(layout)` stub and `tools/param_ids/<g>.lock`): `swell cloud scale keydet budget ratchet rcrush tapedestr resonator fsdelay dub formant spfreeze convo scrub hgran wetout safety midiout scdrive bandfx groove call energy tension battle section learn coop quality`. (`phrase` already exists; `phraseOffset` is appended to it.)
2. **`Source/features/Salts.h`** with every constant of §2.4. **`FeatureHooks.h`** with one empty `gather(EngineSettings&, const ParamCache&)` call per feature, in the fixed order of the work-package table (§6).
3. **Command enum** (`02` F0-4) extended; value list is permanent (§2.3). **RejectReason** extended (§2.2).
4. **Editor slots** (`02` F0-9) gain `PerformanceTab`, `EffectsSection`, `FileMenuExtra`, `HistoryDrawerExtra`.
5. **Subtree names** reserved: `<Scenes> <Tension> <Groove> <Cmd> <Fx>` (fold state/remembered amounts) `<Health>` (muted health codes) `<Learn>`; `<Perf>` already exists; per-subtree caps in §2.3.
Acceptance: golden bit-identical, `validate_repo.py` passes with 30 empty locks, build time delta recorded.

### 2.2 New reject reasons (append before `Count`; permanent values)
`BudgetFull` (F-D01 per-bar budget reached), `QuietZone` (F-D01), `ScGated` (F-L13), `Panicked` (N-USE-a hold), `CoopYield` (F-X06), `ForcedUnsafe` (F-P02 forced fill would hit a protected region). E-3 gets their labels/advice: BudgetFull "Fill budget for this bar used - raise Fills per bar"; QuietZone "Quiet zone - change Quiet Zone"; ScGated "Sidechain gate closed - lower SC threshold or gate mode"; Panicked "Panic is active - press Recover"; CoopYield "Yielded to another GapFill - change Co-op mode"; ForcedUnsafe "Fill Now skipped - next hit too close". Each is reachable from a synthetic test (F0-2 rule).

### 2.3 Command IDs, subtree caps, state version
`Command` types are append-only and shared by GUI buttons, MIDI (X-1) and tests: 1 Preview, 2 LikeLatest, 3 LikeRecipe, 4 LockLatest, 5 LockRecipe, 6 Unlock, 7 Recapture, 8 ReleaseCapture, 9 Panic, 10 Recover, 11 SceneGoto(n), 12 SceneNext, 13 ScenePrev, 14 SceneCancel, 15 PresetNext, 16 PresetPrev, 17 StutterStorm(on/off), 18 Roulette, 19 FillNow, 20 MomentaryStutter, 21 MomentaryFreeze, 22 MomentaryReverse, 23 MomentaryTape, 24 SpectralCapture, 25 SpectralRelease, 26 GrooveLearn, 27 GrooveReset, 28 AbSwap, 29 EventRecordToggle, 30 PreviewRecipe. Subtree caps (bytes, base64 text): `<Macros>` 2 Ki, `<Lock>` 1 Ki, `<History>` 1 Ki, `<Perf>` 4 Ki, `<Scenes>` 24 Ki, `<Cmd>` 3 Ki, `<Tension>` 3 Ki, `<Groove>` 1 Ki, `<Fx>` 1 Ki, `<Health>` 1 Ki, `<Learn>` 4 Ki; sum <= 64 Ki.

### 2.4 RNG salts (`Salts.h`, never reused)
`kSalt_Swell 0x5E11A001, kSalt_Cloud 0xC10D0002, kSalt_Scale 0x5CA1E003, kSalt_Ratchet 0x4A7C0004, kSalt_TapeDestr 0x7A9E0005, kSalt_Resonator 0x4E500006, kSalt_Groove 0x6200E007, kSalt_Call 0xCA110008, kSalt_Energy 0xE4E60009, kSalt_Tension 0x7E45000A, kSalt_Battle 0xBA77000B, kSalt_Budget 0xB0D6000C, kSalt_Roulette 0x4001000D, kSalt_Storm 0x5702000E, kSalt_HGran 0x46A4000F, kSalt_Learn 0x1EA40010, kSalt_Coop 0xC0090011, kSalt_SpFreeze 0x5F2E0012, kSalt_Dub 0xD0B00013, kSalt_Fsd 0xF5D00014`.

### 2.5 Decisions on parked backlog (recorded; reopen only with new evidence)
* **Rejected - Fill morphing / rhythm memory**: excluded by the creative spec; the useful part (reacting to the recent rhythm) is F-L10.
* **Rejected - online preset library/sharing site**: conflicts with the no-network privacy statement (COMPLIANCE "Privacy"). Replaced by offline exchange of `.gapbank`/`.gfrecipe`/`.gapfill` files (N-VAL). Reconsider only with an opt-in design review and an ADR.
* **F-P01 Free/Pro split** (S, decision ticket, deliverable = ADR in `docs/adr/`): recommendation: **no split in 1.x**; one AGPL codebase, all features in one build. If the owner later wants a Pro build: compile-time `GAPFILL_PRO` only gates *UI entry points* of an enumerated list (Macros editor, History export, Calibrate, Evolve, Scene bank file exchange, effect cards beyond the first 4), never DSP code paths or parameter IDs, so state/preset/automation stay identical across builds; a free build loading a project that uses Pro features MUST keep the stored values and play them (no silent drop). Acceptance: ADR merged, no code.
* **F-P03 AAX / AUv3 / iOS** (S, decision): out of scope for 1.x. Entry criteria to reopen: written licence/SDK terms from Avid (AAX) reviewed by owner, a CI runner able to build and a host to validate (Pro Tools / iOS device), and a user-demand count recorded. Until then the CMake `FORMATS` list stays VST3/AU/CLAP/Standalone. No code.
* **F-P04 Localisation** (M): after `describeRecipe`/ParamInfo string tables exist. Add `Source/features/i18n/Strings.{h,cpp}`: `tr("key")` over a table loaded from `Resources/lang/<code>.json` (UTF-8, <= 256 KiB, validated; missing key -> English). Language selector in Options (stored in global settings, default = English, never auto-detected so tests stay deterministic). Strings must not be concatenated into sentences (use `{0}` placeholders). Parameter IDs, state, file formats stay English/ASCII. Fonts: verify glyph coverage for the shipped font, else fall back to the JUCE default sans. Tests: `missing_key_falls_back`, `json_rejects_oversize_malformed`, `placeholders_balanced`, `all_keys_used_exist`, `layout_no_clip_longest_string` (pseudo-locale with +40% length at minimum size). Ship `en` and a pseudo-locale first; community translations are data-only PRs.
* **Fill-lock and other `exact`/loop options** stay out of scope (`02` F0-3).

### 2.6 Platform/release items still open
* **E-4 CI** and **E-5a/b macOS+AU build** are done (CI green on Windows/Linux/macOS, beta1 assets published). In `03` they are marked *Done*. **E-5c notarization** and **E-6 signing** remain: both are blocked only on credentials. Decision rule: no credentials -> keep publishing clearly labelled *unsigned* packages with install guidance (`03` E-5c/E-6 text); implement everything that does not need secrets now (sign step wired behind `if: secrets.X != ''`, `SHA256SUMS` detached signature with minisign, installer checklist tests) so adding credentials is a configuration change. **E-7 real-host QA** stays a process ticket gating 1.0; evidence in `docs/qa/<release>/<host>.md`. Mac Intel/Rosetta and AU-in-Logic checks stay *unverified* until a human runs them.

---

## 3. Shared infrastructure tickets

### X-1 MIDI command map and controller feedback   (size M; feedback profiles = F-L24, size L; depends F0-4, F0-6)
**Goal.** One reliable way to bind MIDI to *commands* (Panic, Scene next, ...), complementing `MidiLearnManager` (CC -> parameter, unchanged).
**Behaviour.**
* `Source/features/commands/CommandMap.{h,cpp}`: table of up to 48 bindings `{ cmdId (§2.3), arg, msgType (CC | Note | ProgramChange), channel (0 = any), number, mode (Trigger | Momentary | Toggle), threshold 64 }`. Trigger fires on CC rising edge through 64 / NoteOn velocity>0 / PC match; Momentary = on while CC>=64 or note held (note-off or CC<64 releases); Toggle flips on each rising edge. Edge re-arms only after the value falls below `threshold-8` (no chatter).
* Evaluated on the audio thread in the same single `MidiBuffer` scan as U-3 and `MidiLearnManager` (one scan, no extra pass); a message that matches a CC *parameter* mapping AND a command binding is a duplicate: Learn refuses the second binding and shows which one owns it (deterministic; first-bound wins on load, second dropped).
* Commands go to the engine through the existing `commands` SPSC queue (audio-thread-originated MIDI uses a second, audio-thread-local 32-entry ring drained at the top of the *next* block start so order is stable; **Panic bypasses both** and sets the atomic panic flag directly).
* Learn: "Learn" next to a command; waits <= 10 s (same session rules/status countdown as CC learn), Cancel/Esc, remove, list with duplicate warning.
* **Feedback (F-L24, SHOULD):** optional MIDI *output* is NOT used (plugin emits no MIDI, `producesMidi()` stays false unless F-L11 is built); instead feedback = on-screen state plus, in Standalone only, an optional output device chosen in Options that echoes LED states (Panic active, current scene, pending scene) as note/CC values defined by a profile. Profiles are JSON in `Resources/controllers/*.json` (<= 8 KiB, schema-validated: name, bindings[], feedback[]), user profiles in `<Documents>/GapFill/Controllers/`. Disconnect/reconnect never blocks audio and never deletes bindings.
**State.** `<Cmd>` subtree (<= 3 KiB): bindings + feedback device name. Invalid binding dropped alone; > 48 truncated. Not automatable; learn mode never saved.
**RT.** Fixed arrays, no allocation; binding lookup O(48) per MIDI event, <= 128 events/block processed (excess counted in `meters.midiDropped`).
**GUI.** Performance tab "MIDI" section (collapsed list) + Options page profile picker. **Tests.** `trigger_edge_once` | `momentary_press_release` | `toggle_flips` | `chatter_rearm_threshold` | `pc_matches_program` | `duplicate_with_param_mapping_refused` | `learn_timeout_cancel` | `bindings_roundtrip_state` | `bad_binding_dropped_rest_kept` | `midi_flood_bounded` | `panic_direct_path_not_queued` | `profile_json_rejects_oversize_malformed` | `no_alloc`.
**Acceptance.** A hardware button bound to Panic stops the wet path within one block in Standalone and one real DAW (manual).

### FX-0 Effect-card framework   (size L; depends F0-2, F0-5, F0-9, F0-10) - *prerequisite of every effect ticket*
**Goal.** One seam so each effect is a separate module that touches no hot file, is neutral off, and obeys panic/reset/bounds uniformly.
**Design.**
* `Source/features/fx/EffectCard.h`: `class EffectCard { prepare(sr,maxBlock,channels); reset(); fadeOut(n); isIdle(); bool enabled(const EngineSettings&); int latencySamples() const; int tailCapMs() const; }` and two kinds:
  * **VoiceFx** (`processVoice(float* l, float* r, int n, const VoiceFxContext&)`): runs on one voice's output before the voice is summed; state lives per voice slot (4 pool + 1 preview voice = 5 instances, preallocated).
  * **BusFx** (`processBus(AudioBuffer<float>& wet, int start, int n, const BusFxContext&)`): runs once after `engine.process` and **before** "wet post-processing" (low-protect HPF, centre protect, width) in `PluginProcessor::processBlock`, so Low/Centre Protect and Width still shape effect output. `BusFxContext` carries `const FillStartInfo* fillStarts; int numStarts` (recipes + output positions of fills started in this block, <= 4) and `const float* duck` (the engine's protection duck, published per block into a preallocated `float duck[maxBlock]` **only when a BusFx is enabled**; engine writes it as it already computes the duck per sample) so tails are ducked exactly like voices: **protected hits stay protected**. A card with `latencySamples() > 0` (e.g. formant) lags the wet path only; the chain delays `duck` by the same amount for that card's output, so the next hit stays protected (reported host latency is unchanged, C8).
* **Fixed chain order (permanent):** VoiceFx: `swell/cloud (render modes in FillVoice) -> ratchet -> tapedestr -> formant -> rcrush`; BusFx: `resonator -> fsdelay -> dub -> convo -> spfreeze -> (wetSafety limiter)`. Reason: time-domain damage first, tonal resonance next, time-based tails last, so tails never get re-damaged.
* `FeatureHooks.h` lists the cards in this order; each card is one `.cpp` registering itself via an explicit call in `FxChain::build()` (explicit list, reviewable). `FillVoice` gets one member `VoiceFxChain*` and <= 3 hook lines (hot-file rule); `PluginProcessor` gets one `fxChain.processBus(...)` line.
* Every card smooths its *Amount* with a 20 ms linear ramp (no zipper) and treats Amount < 0.001 as off (returns before any work; ramp to 0 then `fadeOut` + `isIdle` before it stops being called, so disabling flushes a tail predictably in <= 50 ms for voice cards and <= the card's own tail cap `tailCapMs()` for bus cards: resonator 2 s, freq-shift delay 4 s, dub 8 s, convolution 1.5 s, spectral freeze `sfzLength`; user-lowerable in Options 0.5-16 s, stored in `<Fx>`).
* Output of each card passes `sanitise` and is hard-limited at +12 dBFS internal (never audible clipping; `wetSafety` adds the soft limiter at the bus end).
* Cost accounting: `meters.fxCpu[card]` (relaxed atomics, sampled per 64 blocks via `Time::getHighResolutionTicks`) feeds F-S01/N-USE-b.
**GUI.** Effects section (slot `EffectsSection`): foldable cards in chain order, per-card header with enable LED (= Amount > 0, but toggle sets Amount to the card's remembered value / 50 on first enable; stored in `<Fx>`), "Solo this effect" (sets Fill Type to the carrier algorithm, undoable). Controls of a card are hidden until it is enabled. Fold state persists in `<Fx>`.
**State.** `<Fx>` subtree: fold state + remembered amounts (<= 1 KiB). No audio.
**Tests.** `chain_empty_is_identity` (bit-exact) | `card_off_never_called` (counter) | `enable_disable_ramp_click_free` | `tail_flushed_within_bound` | `panic_clears_all_cards` | `bus_duck_applied_to_tail` | `voice_fx_state_per_voice` | `chain_order_fixed` | `worst_case_cpu_recorded` | `no_alloc` | `fx_golden_all_off`.
**Acceptance.** Adding a new card = new files + one explicit line in `FxChain::build()`; golden unchanged with all cards off; `sizeof` and preallocated memory recorded.

### SP-0 Shared spectral processor   (size L; depends FX-0) - prerequisite of F-L20, F-X02, F-X01, F-L21
**Goal.** One audited STFT/phase-vocoder core instead of three. `Source/dsp/spectral/Stft.{h,cpp}`: fixed `N in {1024, 2048}` (default 2048 @ <= 96 kHz, 4096 @ > 96 kHz), hop N/4, Hann analysis/synthesis with correct overlap-add gain, `juce::dsp::FFT` plan created in `prepare`, per-channel buffers preallocated; reports an exact fixed latency in samples (= N - hop) and exposes `process(in, out, n, SpectralCallback&)`. Callback gets magnitude/phase arrays; phase handling helpers: unwrap, instantaneous frequency, phase locking (identity phase locking per Laroche-Dolson) to limit transient smearing. Existing `SpectralSmear` is **left untouched**; SP-0 does not replace it.
**Bounds/RT.** No allocation, FFT plan never recreated at runtime (sample-rate change re-`prepare`s off the audio thread via the existing prepare path); denormal flush; NaN guard on bins.
**Tests.** `identity_callback_reconstructs` (error < -90 dB) | `latency_exact` | `block_size_invariant` | `sr_change_reprepare` | `impulse_no_nan` | `throughput_budget` (CPU at 2048/48k recorded) | `no_alloc`.
**Acceptance.** Round-trip null test passes at 44.1/48/96/192 kHz. Reference: phase-vocoder with identity phase locking (Laroche & Dolson 1999), as used in common time/pitch tools.

---

## 4. The six new features

### N-FX1 Reverse Swell   (size M; depends FX-0)
**Goal.** A cinematic "reverse-cymbal" rise: gap-captured audio played backwards that *crescendos and brightens* into a chosen landing point (end of gap / next beat / next bar), then stops short of the next protected hit. Distinct from the existing `Algo::Reverse` (a plain reversed slice with edge fades, no landing logic, no filter motion).
**Behaviour.**
* Render mode of `Algo::Reverse` voices: recipe `flags` bit3 = swell. When `swellAmount > 0`, each time the scheduler selects `Reverse` it flips a **sub-RNG** (`kSalt_Swell`; drawn only when `0 < swellAmount < 100`) with probability `swellAmount/100`. At 100 every Reverse voice swells; the user raises `wReverse` (or "Solo this effect") to hear it more. Recipes from Lock/History/Audition carry the flag, so a swell can be locked and replayed.
* **Landing time** `T` (output samples): `Gap end`: `T = gapEnd - protNextSamples - fadeOut`. `Next beat` / `Next bar`: the first beat/bar boundary `>= now + 60 ms` and `<= T_gapEnd`; needs valid transport, else falls back to `Gap end` and sets `syncClamped`-style flag shown in the status line ("Swell: no transport - landing at gap end"). If none fits, `Gap end`.
* **Material.** Source window `L = min(sinceEventSamples + 15 ms, T - start)` ending at the most recent event, read **backwards** so the reversed material's end coincides with the hit's attack (pre-echo swell). If `L` is shorter than the time to `T`, start is delayed (leading silence) so the peak lands at `T`; if longer, playback rate rises up to x2 (<= +12 semitones, same limit as Reverse's `playRate`), otherwise the oldest part is dropped.
* **Envelope** `g(t)` over the voice, `t in [0,1]`: Linear `t`, Exponential `t^2.5`, S-curve smoothstep, Late `t^5`; peak = voice amp; after `T` a 6-16 ms cosine fade-out (never longer than the room before `protNext`). **Filter** one-pole/SVF low-pass per channel whose cutoff sweeps `lerp(250 Hz, 18 kHz, curve(t))` scaled by `swellFilter` (0 = filter bypassed -> pure level swell; 100 = full sweep). Coefficients updated every 16 samples (bounded), state flushed on `reset`.
* **Protection.** Nothing is emitted after `T + fadeOut`; `T` is always `<= gapEnd - protNext - fadeOut`, so the next hit is never overlapped; the engine's `protTrans` duck still applies on top.
**Params.** `swellAmount, swellCurve, swellFilter, swellTarget` (registry). **State.** none beyond params (flag in recipe -> history/lock subtrees already carry `flags`).
**RT.** No allocation; two filters per voice (preallocated), constant work per sample.
**GUI.** Effects section card "Reverse Swell" (4 controls + landing label showing the last landing "bar 17.1"). Help: explains it is a mode of Reverse.
**Tests.** `swell_off_identity` | `rng_untouched_when_off_or_100` | `peak_lands_at_target_within_1_sample` (Gap end, beat, bar at 60/120/174 BPM) | `no_output_after_target_plus_fade` | `never_overlaps_protNext_window` (property test over random gaps) | `envelope_monotonic_until_peak` per curve | `hf_energy_rises_with_filter` | `no_transport_falls_back_gap_end` | `rate_clamped_x2` | `locked_recipe_replays_swell` | `block_size_invariant` | `panic_fades` | `finite_all_extremes`.
**Acceptance.** On the drum-loop fixture, with Swell 100 and Reverse forced, every swell fill peaks within 1 sample of the computed target and the region after it is silent until the next hit; golden unchanged at Amount 0.

### N-FX2 Granular Cloud   (size M; depends FX-0; benefits from N-MUS)
**Goal.** A dense, directly controllable grain cloud scattered over recent audio. Distinct from the existing `Algo::Granular`, whose grain size/density/spread are *derived* from Intensity/Complexity/Chaos and capped at 16 grains.
**Behaviour.**
* Render mode of `Algo::Granular` voices (recipe `flags` bit4). Same selection rule as N-FX1 (`cloudAmount` = probability, sub-RNG `kSalt_Cloud`).
* Source: the latched Capture buffer if capture is active, else the gap-recent `SourceHistory` window `[anchor - cloudSpan, anchor]` (never beyond `anchor`, never before history start).
* Grain scheduler: inter-onset time `= 1/cloudDensity` with +-50% uniform jitter (sub-RNG); grain length `cloudSize` ms (+-20% random), Hann window (Tukey if size < 20 ms to avoid thinning), equal-power random pan scaled by `cloudSpread`. **Position jitter**: start offset (samples before `anchor`) `back = span*(0.5 + (cloudJitter/100)*(u - 0.5))` with `u` uniform [0,1): jitter 0 = all grains from the middle of the span (a stutter-freeze), 100 = uniform over the whole span. **Pitch spread**: per-grain shift triangular-distributed in `+-cloudPitch` cents (0 = none); with Scale Lock on, the shift is replaced by the nearest in-scale interval (N-MUS). Read with the existing interpolated `SourceHistory::read`; reverse grains: probability `Chaos` x 0.3 as today.
* **Bounds.** `kCloudMaxGrains = 48` per voice (preallocated); concurrency estimate `density*size/1000` clamped to <= 40 by lowering the effective density (shown as "density limited" in the card); a grain that cannot be allocated is dropped and counted. Output normalised by `1/sqrt(max(1, estimatedOverlap))` then the voice's amp/fades; voice length, fades and protection are the host algorithm's, so the cloud always ends before the next protected hit.
**Params.** `cloudAmount, cloudDensity, cloudSize, cloudSpan, cloudJitter, cloudPitch, cloudSpread`. **State.** params only.
**RT.** 48 grains x constant per-sample work; positions are doubles; no allocation.
**GUI.** Effects section card "Granular Cloud" (7 controls; Density/Size/Span on the first row, the rest under "More"). Read-only readout "~N grains, limited" when clamped.
**Tests.** `cloud_off_identity` | `rng_untouched_when_off` | `grain_count_never_exceeds_48` (density 80, size 250) | `density_limit_reported` | `positions_within_span_and_history` | `jitter0_all_from_centre` | `pitch_spread_bounds_cents` (histogram) | `scale_lock_shifts_in_scale` | `output_finite_and_bounded` (RMS vs amp within 3 dB across settings) | `same_seed_same_output` | `capture_source_frozen_vs_live` | `ends_before_protNext` | `block_size_invariant` | `no_alloc`.
**Acceptance.** Cloud at default settings sounds denser/smoother than plain Granular on the drum-loop fixture (RMS-modulation depth lower by >= 20% measured), golden unchanged at Amount 0, CPU delta recorded per voice.

### N-USE Panic & Recover + Transport/Sidechain Health   (two slices)

#### N-USE-a Panic and Recover   (size M; depends F0-4, X-1)
**Goal.** One always-available action that silences GapFill's *wet* contribution immediately and a clear way back, never delayed by quantisation, scenes, smoothing or automation, and never touching the dry signal.
**Behaviour.**
* **Panic** (button, keyboard `Ctrl/Cmd+.` when the editor has focus, MIDI via X-1, host-visible nowhere): sets `std::atomic<uint32_t> panicSeq` (increment) read at the *top* of `processBlock` before the command queue. Effect, within that block: (1) wet bus gain ramps to 0 over a **fixed 5 ms** linear ramp that is independent of the Wet knob smoother and of `Lookahead`; (2) `FillEngine::panic()` (existing 4 ms voice stop) + preview voice + all FX cards `fadeOut(≈5 ms)` then `reset()` (C7); (3) pending quantised scene/preset/fill-now and queued commands (except Recover) are cancelled; momentary/latch states released; (4) engine enters `Panicked` (new `RejectReason::Panicked`): no new natural fills, detection/analysis keeps running (visualiser live), capture buffer and settings untouched. Dry path, bypass, latency, parameter values: **unchanged**. A panic that arrives mid-block takes effect at the next block start (<= one host block); MIDI-originated panic uses the event's sample offset to start the ramp inside the block.
* **Recover** (button, MIDI, or auto): wet gain ramps up over a fixed 20 ms, fills resume at the next *natural gap* (not retroactively); never re-fires cancelled actions. Options (stored in `<Perf>`, not automatable): **Recovery = Manual (default) | Auto after 1 / 2 / 4 s**. Pressing Panic while Panicked = Recover (toggle), configurable off in Options for stage safety ("Panic never toggles").
* Panic is **idempotent and stateless across projects**: `Panicked` is never saved in state (a project always opens un-panicked); it survives editor close and is shown in the footer (N-USE-b) while active.
* Last-good-scene recall (F-L25) is a separate action (`SceneGoto(lastGood)`), never implied by Recover.
* Panic does not call `engine.noteTransportJump()` and does not reset RNG/gap counters, so after Recover the sequence of *decisions* continues deterministically from the current position.
**State.** `<Perf>`: recovery mode, toggle-behaviour. **RT.** Only atomics, ramps, existing fades; zero allocation; worst-case work = one extra loop over <= 5 voices + cards. **GUI.** Fixed high-visibility PANIC button at the left of the Performance tab and (to satisfy `11` "reachable without a popup") a compact red Panic/Recover chip pinned in the footer status area of every tab; accessible name "Panic: silence GapFill effects now"; shows Recover when active; large hit target (>= 32 px).
**Tests (`TestPanic.cpp`).** `panic_wet_zero_within_5ms` (sample-exact ramp) | `panic_dry_bit_identical` | `panic_during_each_algo_and_each_fx_card` | `panic_cancels_pending_scene_and_commands` | `panic_not_delayed_by_quantisation_or_smoothing` (wet knob smoother at slow state) | `panic_independent_of_block_size` (32..8192: wet silent by block end of the request block + 5 ms) | `recover_resumes_at_next_gap` | `auto_recover_timing` | `panic_toggle_option` | `panicked_not_saved` | `panic_does_not_advance_rng_or_gap_index` | `panic_midi_direct_path` | `panic_with_bypass_and_transport_stop` | `no_alloc`.
**Acceptance.** Manual: stuck Dub/Cloud tail stopped by one hardware button in a real DAW without any click on the dry track; listed *unverified* until run.

#### N-USE-b Transport & Sidechain Health footer   (size M; depends F-S01, E-3, U-9 status slot)
**Goal.** Tell the player *before* a set goes wrong that sync/source/CPU are degraded, in one compact line with a reason and a next action.
**Data (audio thread -> one `std::atomic<uint32_t> healthMask` + floats, written once per block).** `HealthCode`: `NoTransport` (host reports no valid tempo/play state for > 1 s while a tempo-dependent feature is on), `TransportJump` (jump detected in last 10 s, informational), `NoSidechain` (detSource != Main but sidechain bus not enabled/connected), `SidechainSilent` (bus connected, level < -80 dBFS for >= 2 s while detSource != Main), `FreeTimingFallback` (E-2 fallback BPM in use), `OverloadWarn` (EMA CPU > 70% of block time for >= 2 s), `OverloadCritical` (> 90% or an xrun-like deadline overrun counted), `QueueOverflow` (fillEvents/commands dropped in last 10 s), `BypassActive`, `PanicActive`. Conditions use hysteresis: set after the hold time, cleared after 2 s of absence, so the footer never flickers.
**Display.** FooterStatus shows **only the highest-priority** message (priority: PanicActive > OverloadCritical > NoSidechain/SidechainSilent > NoTransport/FreeTimingFallback > OverloadWarn > QueueOverflow > BypassActive > TransportJump), text e.g. "No sidechain - using input", "Transport unavailable - free timing at 120 BPM", "CPU high (82%) - try Quality: Balanced". Clicking the chip expands the visualiser detail (list of all active codes with explanations, their onset time, and the suggested action; this is the data N-EASY uses). Interaction with `U-9` priority order: learn > export > preview > capture > **health (this) > fix-it (N-EASY) > last gap outcome > hover**. Never green "all OK" ticks: nothing is shown when healthy.
**State.** `<Health>`: user can mute individual codes (e.g. mute NoSidechain). **RT.** Detection = a few comparisons per block; level meters already exist; no allocation.
**Tests.** `each_code_set_and_cleared_with_hysteresis` | `priority_order` | `sidechain_absent_vs_silent` | `no_transport_only_when_feature_needs_it` | `message_text_matches_code` | `muted_code_hidden_but_listed` | `footer_rate_limited` | `healthy_shows_nothing` | `mask_publish_no_alloc`.
**Acceptance.** Reproduce each code in Standalone/DAW with the E-7 script; zero audible/CPU impact with editor closed (< 0.1%).

### F-S01 CPU and latency display   (size S; depends FX-0 meters)
Show reported latency (ms and samples, "PDC" label) and a **sampled** CPU estimate in the Options page and the N-USE-b detail view. CPU = EMA of `processBlock` wall time / block duration, measured with two high-resolution tick reads per block (no syscalls beyond the clock; no allocation), labelled "estimated", never presented as host CPU. Per-card cost from `meters.fxCpu`. **Tests:** `cpu_estimate_monotonic_with_load` | `latency_matches_reported` | `labels_say_estimated` | `disabled_when_editor_closed_zero_cost`. **Acceptance:** shows latency equal to the host-reported value in a real DAW (manual).

### N-MUS Scale Lock   (size M; depends F0-3, F0-5; hooks into N-FX2, F-M15, F-L20)
**Goal.** Make pitched fills land on notes that belong to the song's key.
**Parameters.** `scaleRoot` (C..B), `scaleType` (Off + 12 scales), `scaleAmount` (0-100, probability each pitch decision is snapped), later `scaleFollow` (F-X03). **Off = neutral.**
**Scale tables (semitones above root):** Major 0 2 4 5 7 9 11; Natural Minor 0 2 3 5 7 8 10; Dorian 0 2 3 5 7 9 10; Phrygian 0 1 3 5 7 8 10; Lydian 0 2 4 6 7 9 11; Mixolydian 0 2 4 5 7 9 10; Harmonic Minor 0 2 3 5 7 8 11; Melodic Minor 0 2 3 5 7 9 11; Major Pentatonic 0 2 4 7 9; Minor Pentatonic 0 3 5 7 10; Blues 0 3 5 6 7 10; Whole Tone 0 2 4 6 8 10.
**Behaviour.** `Source/features/scale/ScaleLock.{h,cpp}` (pure): `int snapInterval(int semis, ScaleSet, PitchContext)` and `float snapFrequency(float hz, ScaleSet)`.
* **Relative shifts** (PitchRise/PitchFall semitones, Reverse/Stutter/Granular pitch choices, Cloud `cloudPitch`, formant shift): the fill is a shift of the source by `i` semitones. With `PitchContext{sourcePc, conf}` (default `sourcePc = scaleRoot`, i.e. the user states the song's tonic), the resulting pitch class `(sourcePc + i) mod 12` must be in the scale; snap `i` to the nearest `i'` with that property, ties resolved toward smaller `|i|` then upward; octaves are preserved (`i' ≡ snapped mod 12`, `i' - i` in [-6,6]). 0 and +-12 are always allowed. When F-X03 later provides a confident per-hit `sourcePc`, it replaces the default (SHOULD hook, no v1 dependency). **Honest limit (Help text):** with the default context the lock assumes the hit is the tonic or a chord tone; drums and atonal material are not retuned, they are merely constrained to in-key *intervals*.
* **Absolute pitches** (Comb algorithm frequency, resonator `resNote` in "Scale degrees" mode): snap the pitch class to the nearest scale degree (absolute, root fixed), frequency exact to < 1 cent after snapping.
* **Order and RNG.** Snapping is a pure mapping applied in `adjustRecipe` to the already-drawn value, so the legacy RNG draw order is untouched; the only extra draw is the `scaleAmount` coin (`kSalt_Scale`) and only when `0 < scaleAmount < 100` and the scale is not Off. Result is stored in `FillRecipe.pitchSemis` (so Lock/History/Audition replay the snapped value) and passed via `VoiceSetup::pitchOverride` (F0-3).
* Existing Granular ratio table `kRatios` is used unchanged when Scale is Off; when On the grain ratio set becomes `2^(k/12)` for in-scale `k` within the old min/max (0.25..2.0).
* Display: root + scale chips in the Creative tab "Musical" row (collapsed in Simple) and a tooltip listing the active pitch classes; fills snapped this fill show a small "key" glyph on the History chip.
**State.** params only. **RT.** table lookups, no allocation. **GUI.** Creative tab, Advanced section (Simple view shows only a "Key: D minor" read-only chip when set).
**Tests (`TestScale.cpp`).** `scale_off_identity` | `rng_untouched_when_off` | for each of 12 scales x 12 roots: `snapped_interval_in_scale` | `tie_rule_deterministic` | `octaves_preserved` | `zero_and_octave_always_allowed` | `amount_0_never_snaps_100_always` | `amount_50_ratio_within_tolerance_over_10k_draws` | `recipe_pitch_matches_snapped` | `granular_ratios_in_scale` | `comb_frequency_in_scale_within_1_cent` | `block_size_invariant` | `state_roundtrip_and_old_state_off` | `pitch_context_override`.
**Acceptance.** With D minor set and PitchRise forced for 200 fills, 100% of rise intervals are in `{0,2,3,5,7,8,10,12}`; neutral golden unchanged.

### N-VAL Scene Bank & Exchange   (size L; depends F-L07, X-1, F0-6)
**Goal.** A setlist of scenes you can step through (next/prev incl. MIDI, Program Change, quantised) and share as a file.
**Model (extends F-L07).** A **Bank** = ordered list of up to **64** scenes (`<Scenes>` <= 24 KiB), each with name (<= 32 chars), scope flags, quantise mode override (Inherit | Immediate | Beat | Bar | Phrase), and sparse params. The bank has a name (<= 48) and a *current index*. Quick slots: the Performance tab shows 8 slots per page (8 pages); each slot = bank index.
**Navigation.** `SceneNext`/`ScenePrev` move the *pending* target (clamped at ends by default; Options: Wrap); `SceneGoto(n)` jumps; MIDI Program Change 0-63 -> scene n (when enabled, channel selectable); all go through the quantiser of F-L07. Repeated Next presses before the boundary advance the pending target (up to the bank length) and the display shows the final target; `SceneCancel` (and Esc) discards pending; Panic cancels pending.
**File format `.gapbank`** (UTF-8 XML, <= 256 KiB, <= 64 scenes):
```
<GapBank formatVersion="1" appVersion="0.x.y" stateVersion="3" name="Gig 2026-11" created="2026-10-21T12:00:00Z">
  <Scene name="Verse" quant="bar" scope="creative,weights,fx,musical">
    <P id="density" v="0.55"/> ...        <!-- permanent string IDs, normalised 0..1 -->
  </Scene> ...
  <Macros> ... </Macros>                    <!-- optional, C-2 rules -->
  <Digest sha256="..."/>                    <!-- over canonical payload: integrity, not security -->
</GapBank>
```
* **Import** (File menu; modes Replace | Append | Merge-by-name): size cap check -> parse -> `formatVersion` (newer than supported: reject "made by a newer GapFill"; older: `migrateBank(from)`) -> structural validation -> per-scene validation (every `id` known & not in `sceneNeverIDs`, `v` finite and in 0..1, <= 128 params, unknown IDs ignored and counted) -> digest check (mismatch = warning dialog, user may continue) -> apply **atomically**: either the whole accepted set replaces/extends the bank or nothing changes; a scene that fails validation is skipped and listed in the result dialog; a one-step "Undo import" restores the previous bank. Import never changes live parameters; only recalling a scene does.
* **Export** (File menu): whole bank or selected scenes; checkbox "Include macro mappings"; refuses to overwrite without confirm; writes via temp file + rename; error text shown, state untouched. Path/host info never written.
* Bank in project state: `<Scenes>` (binary-compact base64 of `(paramIndex uint16, float)` pairs; permanent index = position in `GAPFILL_PARAMS`, stable because append-only; ID-string form only in files).
**RT.** Audio thread only sees the active **overlay** (F-L07); file work on the message thread/worker.
**GUI.** Performance tab: bank name, 8-slot page, Prev/Next, quantise selector, pending indicator with Cancel; File menu: Import/Export bank; "Bank..." drawer (rename, reorder by drag or Move up/down buttons for keyboard users, delete with confirm, duplicate).
**Tests (`TestSceneBank.cpp`).** `bank_roundtrip_state` | `bank_roundtrip_file` | `import_rejects_oversize_malformed_newer_version` | `import_skips_bad_scene_keeps_rest` | `import_atomic_replace` | `undo_import_restores` | `unknown_ids_ignored_counted` | `never_ids_rejected` (dry/wet/output/bypass/lookahead) | `digest_mismatch_warns` | `next_prev_clamp_and_wrap` | `rapid_next_presses_advance_pending` | `program_change_maps` | `goto_out_of_range_ignored` | `export_excludes_paths` | `bank_64_scene_state_under_cap` | `old_state_without_scenes_opens_empty_bank`.
**Acceptance.** Export on one machine, import on another OS: identical recalled parameter set (diff of APVTS values == 0); a hostile/garbage `.gapbank` never changes state or crashes.

### N-EASY Fix-it Suggestions   (size M; depends E-3, N-USE-b, U-9, existing Undo)
**Goal.** When the plugin is "doing nothing" or something is degraded, offer **one** concrete, safe fix in the status line that can be applied with one click and undone with one click.
**Inputs.** `GapOutcomeLog` (last 64 outcomes, E-3), `reasonCount`, health mask (N-USE-b), current parameter values. Runs on the message thread (timer, 2 Hz); never on the audio thread; never affects audio decisions by itself.
**Rule table (permanent rule IDs; evidence window = last 8 evaluated gaps unless stated; "k of 8" counts skips with that reason):**
| Rule | Trigger | Proposed change | Class |
|---|---|---|---|
| R1 `fill_off` | >= 3 FillOff in window | `fill` -> 50 | param |
| R2 `too_short` | >= 5 TooShort | `minGap` -> `max(20 ms, floor5(median availMs))` if lower than current | param |
| R3 `too_soon` | >= 5 TooSoon | `retrigGuard` -> 70% of current (>= param min) | param |
| R4 `density_roll` | >= 6 DensityRoll | `density` +20 (max 100) | param |
| R5 `no_voice` | >= 5 NoVoice | `maxFill` -> 80% of current (>= 20 ms) | param |
| R6 `no_algo` | >= 4 NoAlgoFits and `fillType != Auto` | `fillType` -> Auto | param |
| R7 `no_band` | >= 4 NoBand | enable the first disabled of `bandLowOn/MidOn/HighOn` | param |
| R8 `lock_skipped` | >= 3 LockSkipped | `fillLock` -> off | param |
| R9 `no_source` | >= 4 NoSource and detSource=Sidechain and NoSidechain/Silent health | `detSource` -> Main | param |
| R10 `guard_eaten` | >= 5 GuardEaten | `protNext` -20% of current | param, **weakens protection: popover only, needs confirm click** |
| R11 `overload` | OverloadWarn >= 5 s and qualityMode=Full | `qualityMode` -> Balanced | param |
| R12 `no_transport` | NoTransport and sync/quant features on | open Options at "Tempo when host provides none" | action (not undoable) |
| R13 `panicked` | PanicActive > 10 s | Recover | action |
Rules never touch dry/wet/output/bypass/lookahead/seed/latency; R10 is the only protection-weakening rule. Parameter bounds always clamp to the parameter's range.
**Behaviour.** The highest-evidence rule not on cooldown is shown as `Suggestion: Min Gap 120 -> 85 ms  [Apply] [x]`. A suggestion needs its evidence to persist for >= 4 s before showing, is shown >= 6 s, then rotates; after Apply or dismiss the same rule has a 5-minute cooldown; "Don't suggest this" (in the popover) stores the rule ID in global settings; Options toggle "Show suggestions" (default on). **Apply** = push one Undo snapshot (existing Undo action) then, per param, `beginChangeGesture -> setValueNotifyingHost -> endChangeGesture` on the message thread; the footer then shows `Applied: Min Gap 120 -> 85 ms  [Undo]` for 10 s. **Undo** restores the exact previous raw values (bit-identical) unless a later user edit touched those params (then the chip disappears; global Undo still works). Applying while automation is writing is allowed (host decides), no special handling.
**State.** `<Health>`/global settings: muted rule IDs. **Accessibility.** Chip is a focusable button with a spoken description of the change; Apply/Undo reachable by keyboard.
**Tests (`TestFixit.cpp`).** one synthetic log per rule -> exact proposal and values | `below_threshold_no_suggestion` | `cooldown_and_dismiss` | `evidence_hold_time` | `apply_pushes_undo_and_sets_values` | `undo_restores_bit_identical` | `clamped_to_param_range` | `never_touches_forbidden_ids` (R-table whitelist) | `r10_requires_confirm` | `priority_vs_health_and_other_status` | `disabled_option_hides` | `deterministic_for_same_log` | `no_audio_thread_calls` (static check: module has no processBlock includes).
**Acceptance.** On a loop that triggers TooShort repeatedly, one click raises the fill rate and one click on Undo returns the exact earlier values; manual check in a DAW with automation lane present.

---

## 5. Live control, assist and workflow tickets

### UI-0 Performance tab, Effects section and status shell   (size M; depends F0-9; first ticket of the UI stream)
Capture the editor at min/default/large (`11` sequence). Add a container for slots `PerformanceTab`, `EffectsSection`, `FooterStatus`. If a fourth tab fits the minimum width without shrinking the existing three tabs, add **Performance**; otherwise add a *More* menu entry that opens it as a panel. Effects section sits below the Creative row, collapsed by default, scroll confined to that panel. Adds no controls itself. **Tests:** screenshot at min/default/large; `tabs_do_not_clip_at_min`; `keyboard_reaches_every_slot`; `slots_empty_layout_identical_to_baseline` (pixel tolerance). **Acceptance:** existing screens unchanged (diff within tolerance) when no feature registers a component.

### F-L07 Performance scenes with quantised recall   (size L; absorbs F-M11; depends F0-5, F0-6, F0-4)
**Goal.** Save/recall musical configurations that change *on the beat*, click-free, without host automation surprises.
**Scene** = `{ name, scope, quant, sparse params }` (bank/setlist/files: N-VAL). **Scope flags** (per scene): `creative` (fill, intensity, complexity, chaos, density, fillType), `weights` (15 `w*`), `fx` (all effect-card params), `musical` (scale*, phrase*, breath*, tension*, groove*, call*, energy*, battle*, budget*), `macros` (macro values + `macrosOn`), `detector` (opt-in), `protection` (opt-in), `bands`. **Never stored/recalled** (`sceneNeverIDs()`): `dry, wet, output, bypass, lookahead, seed, wetSolo, wetSafety, qualityMode, capture, audition, coop*, midiOut*` and panic state. Default scope = `creative, weights, fx, musical, macros`.
**Recall pipeline (audio-thread correct, block-size independent).**
1. Message thread resolves the scene into an **overlay** `Overlay{ n, idx[], val[], boundaryPpq, boundarySample, mode, id }` (<= 128 entries, preallocated triple buffer) and publishes it with one atomic pointer swap; audio thread never allocates.
2. **Quantiser** (audio thread): Immediate -> boundary = now. Beat/Bar/Phrase -> next boundary strictly after `now + 1 ms`, computed from host PPQ, time signature and `phraseBars`/`phraseOffset`; if tempo/transport invalid -> Options fallback (default **Immediate**, shown as "No transport - switching now"); transport **stopped** -> Immediate. Loop/seek (PPQ moves backwards or jumps > 1 beat) with a pending change -> re-arm from the new position (never dropped, never applied twice). Pending is cancellable (`SceneCancel`, Esc, Panic); a newer request replaces it.
3. **Application:** decision-stage parameters take the overlay when `decisionPpq >= boundaryPpq` (so the result does not depend on where the host cut the block); block-stage/continuous ones (effect amounts, width) switch at the block containing the boundary and are smoothed by their existing 20 ms ramps. Voices in flight keep their captured `VoiceSetup`; no retroactive change; no CPU spike (constant-time swap).
4. **Commit:** audio thread posts `SceneApplied{id, pos}`; the message thread writes the values into the APVTS with host notifications (gestures) within ~100 ms, then clears the overlay. Touching a controlled parameter by hand/automation while the overlay is active removes only that overlay entry. `lastGoodScene` = id of the last committed scene (validated before applying; an invalid scene is never applied, `SceneApplied` carries a failure flag and a status message).
**State.** `<Scenes>` (N-VAL owns the bank format). **GUI.** Performance tab: 8 slots, current/pending highlight (text + shape), countdown "Bar 17 in 1.2 beats", Save (overwrite confirm)/Recall/Clear, quantise selector (Immediate/Beat/Bar/Phrase). **RT.** Overlay <= 128 x 8 B; swap lock-free.
**Tests (`TestScenes.cpp`).** `recall_immediate_applies_next_decision` | `quantised_bar_boundary_exact_ppq` | `block_size_invariant_switch_point` | `tempo_change_before_boundary` | `loop_wrap_rearms` | `seek_rearms_not_drops` | `stopped_transport_immediate` | `no_transport_fallback_immediate` | `newer_request_replaces_pending` | `cancel_and_panic_cancel` | `scope_flags_respected` | `never_ids_untouched` | `invalid_scene_not_applied_last_good_kept` | `commit_matches_overlay` | `manual_edit_clears_entry` | `overlay_no_alloc` | `scene_state_roundtrip_and_old_state` | `click_free_fx_amount_ramp`.
**Acceptance.** In a looping 8-bar DAW project the scene changes land on the bar line (visualiser marker) with no dropout on the dry track; unverified until run in a real host.

### F-M10 Momentary and latch performance controls   (size M; depends F0-4, X-1, E-1's preview voice)
Commands 20-23 (Stutter, Freeze, Reverse, Tape) in **Momentary** (hold) or **Latch** (toggle) mode per control. **Hold controller** (`Source/features/live/Hold.{h,cpp}`): on press, start the dedicated performance voice (shares the E-1 preview voice slot; priority performance > preview) with a fixed recipe per effect: Stutter = Stutter at 1/16 (fallback `stutterSeconds`), Freeze = Freeze of the last 280 ms, Reverse = Reverse of the last `min(maxFill, 1 s)`, Tape = TapeStop curve; while held the controller restarts the voice at its end with an 8 ms crossfade and seed `hash(seed, iteration)`; on release `requestStop` with an 8 ms fade (Tape: fade). **Quantise**: press may be delayed to the next beat/bar by the F-L07 quantiser when "Quantise performance FX" is on (default Off). **Safety:** lost release (no note-off/CC<64) auto-releases after `holdTimeout` (Options 2-30 s, default 8 s); Latch has `latchTimeout` Off/30 s/2 min (default Off) and is cleared by Panic, bypass and transport stop (Options: "Keep latch across stop" default off). Output passes the normal wet post-processing, Wet/Output trims and Low/Centre protect; engine transient-ducking applies like Audition (user can allow override in Options, default off). Not natural fills: not in History/Lock, no RNG from the legacy stream (`kSalt_Storm` family sub-RNG).
**State.** modes + timeouts in `<Perf>`; nothing saved while held. **GUI.** Performance tab: 4 pads (press-and-hold with mouse/touch, keyboard Space/Enter hold), each shows armed/held/latched with text+shape; bindings via X-1.
**Tests.** `hold_starts_and_restarts_seamlessly` | `release_fades_8ms` | `lost_release_times_out` | `latch_toggle_and_timeout` | `latch_cleared_by_panic_bypass_stop` | `priority_over_preview` | `not_in_history_or_rng` | `quantised_press` | `ducked_by_protection_by_default` | `all_pads_finite` | `no_alloc`. **Acceptance:** four pads usable from keyboard and mapped MIDI; no stuck sound after cable pull (manual with hardware).

### F-S07 One-click Stutter Storm   (size S; depends F-M10)
Command 17 (toggle or hold). While active the hold controller plays the curated 8-step pattern `[1/16,1/16,1/32,1/32,1/16,1/32,1/32,1/32]` (per step: Stutter slice of the last 200 ms, gate 80%, +1.5 dB accent on step 1), advancing on the 1/32 grid when transport is valid (else at fallback-BPM grid), cycling until released or timed out; ends with a 1/8 tail. Limited to Stutter within existing voice limits (preview voice only). **Tests:** `pattern_steps_exact_on_grid` (60/120/174 BPM) | `deterministic_seed` | `ends_cleanly` | `timeout` | `respects_panic` | `neutral_when_unused`. **GUI:** a "STORM" pad next to the four pads.

### F-S08 Glitch Roulette   (size S; depends none, extends Surprise)
"Spin" button (and command 18) in the Creative row (Advanced) / Performance tab. Picks one of the existing Surprise profiles (`surpriseProfiles()`, unchanged) via an **injected** message-thread RNG (production: `juce::Random::getSystemRandom()`, tests: fixed seed) with the constraint "not the same profile twice in a row", applies it through the **existing Surprise code path** (so Undo snapshot and randomisable-set rules are inherited), and shows the profile name; "Restore" = the existing Undo. Distinct from Surprise only by the 3-slot animation (skipped under Reduced Motion) and the no-repeat rule. **Tests:** `no_immediate_repeat` | `uses_surprise_path_and_undo` | `only_randomisable_params_change` | `reduced_motion_no_animation` | `deterministic_with_injected_rng`.

### F-P02 Quantised Fill Now   (size M; depends F-L07 quantiser, E-3)
Command 19 + Performance-tab button. Options: quantise Immediate / Next beat (default when transport valid) / Next bar. At the boundary sample the engine runs `trySchedule` with `forced = true`: skips `DensityRoll` and the `TooSoon` half-gap guard, **keeps** voice availability, source presence, `protectHold` (no new fill within `protectHold` of a detected event unless Options "Allow over sustained audio"), and the room to the next *known* event (lookahead) minus `protNext`; if that room < `max(60 ms, minGap)` -> `ForcedUnsafe` (status shows it). Length = min(room, maxFill). Counts as a natural fill (History, Lock, reasons; key = boundary ppq tick). **Tests:** `fires_on_boundary_exact` | `skips_density_roll` | `unsafe_when_next_hit_close` | `respects_protect_hold` | `no_transport_immediate_fallback` | `block_size_invariant` | `appears_in_history_not_preview` | `forced_flag_in_recipe`. **Acceptance:** pressing it 4 times on a drum loop produces 4 fills that never cover a hit.

### F-L25 Live safety suite   (size S-M; builds on N-USE-a)
Adds the pieces N-USE-a does not cover: (1) `wetSafety` (bool, default off): soft limiter on the **wet bus only**, last BusFx stage: `y = x / (1 + (|x|/c)^4)^(1/4)` with `c = 0.89` (-1 dBFS), zero latency, stateless (no overshoot memory), documented "not a mastering limiter"; (2) overload fallback: with `qualityMode = Auto` (F-D03) the engine sheds load automatically; otherwise N-EASY R11 suggests it; (3) last-good-scene action; (4) "Safety" group in the Performance tab: Panic, Recover, Last good scene, Wet safety toggle. Existing click-free bypass is unchanged and covered by tests. **Tests:** `wetsafety_off_identity` | `wetsafety_bounds_output` (inputs to +24 dBFS) | `wetsafety_transparent_below_-6dB` (< 0.1 dB deviation) | `last_good_scene_recall` | `finite_all`.

### F-M23 Advanced modulation and macro targets   (size M; delta to C-2; depends C-2)
Append to `macroTargets()` (persisted order, append-only): the Amount parameters `swellAmount, cloudAmount, ratAmount, rcAmount, tdAmount, resAmount, fsdAmount, dubAmount, fmtAmount, sfzAmount, cvAmount, tensionAmount, energyAdapt, scaleAmount, grooveAmount` (indices 21-35). Mapping gains optional fields `smoothMs` (0-500, default 0 = as C-2) and `bipolar` (maps -1..1 around the macro centre); a macro may select **source = LFO**: shape (sine, triangle, saw, square, seeded sample-and-hold), rate in Hz or tempo division (PPQ-locked when transport valid), depth 0-100%, phase 0-360; LFO output replaces the knob value for the effective calculation at block rate (continuous targets are smoothed; decision-stage targets sample it at the decision PPQ, so S&H uses `hash(seed, ppqTick)`). Old `<Macros>` (without new fields) load unchanged. Bounds: effective values clamp to the target's range; at most 4 LFOs. **Tests:** `new_targets_in_table_order` | `old_macros_subtree_loads` | `smoothing_bounded_no_overshoot` | `lfo_phase_locked_to_ppq` | `lfo_block_size_invariant_decision` | `sh_deterministic` | `bipolar_range`.

### F-S06 A/B compare with level matching (and recipe A/B, F-L23)   (size M; depends E-1, U-1)
Header A/B already exists. Add Options toggle **Match levels** (default Off, `<Perf>`): each side accumulates a K-weighting-lite loudness of the *wet contribution* (100 Hz high-pass, 400 ms mean-square, 10 s EMA, ignoring blocks below -60 dBFS) only while active; once both sides have >= 3 s of data, switching applies an output-trim `trim = clamp(L_other/L_active, -6..+6 dB)` smoothed over 50 ms and shows "+1.4 dB match"; the trim is never saved, never an APVTS value, and is removed when the toggle is turned off. Recipe compare: History chips can be set as slot **A** and **B**; "A/B" alternates the audition (E-1 preview voice) between the two recipes with the same trim logic applied to the preview gain from each recipe's measured preview RMS. Undo/redo: swapping sides pushes no undo entry; "Copy A to B" does. **Tests:** `match_off_identity` | `trim_bounded_6dB` | `silent_side_no_trim` | `trim_smoothed_click_free` | `not_saved_in_state` | `recipe_ab_alternates_and_matches` | `swap_does_not_mutate_automation`.

### F-M17 Wet-only output mode   (size S) and F-L12 wet output bus   (size L)
**F-M17** `wetSolo` (bool, off). When on, the processor drives `dryGain -> 0` and `wetGain -> 1.0` (existing smoothers, 20 ms) and ignores the Wet/Dry knobs; `output` trim and host bypass still apply; the signal is the wet bus after protection/width/`wetSafety`, i.e. exactly the signal U-2 exports and F-L12 carries. Excluded from scenes/random. **Tests:** `off_identity` | `solo_equals_dry0_wet100_out0dB_render` (bit-exact) | `switch_click_free` | `bypass_wins` | `panic_silences`.
**F-L12** Optional second **stereo output bus "Wet"**, disabled by default (`BusesProperties().withOutput("Wet", stereo, false)`), carrying the same wet signal as F-M17 at unity, aligned to the main output (no extra latency). Main bus behaviour unchanged. `isBusesLayoutSupported` accepts main stereo (as today) with the wet bus disabled or stereo. CMake option `GAPFILL_WET_BUS` (default ON for VST3/CLAP, OFF for AU until `auval` + Logic pass). Risks (record in the PR): CLAP wrapper multi-output behaviour, host auto-enabling all buses, AU validation. **Tests:** `bus_disabled_identity` | `wet_bus_equals_wetSolo_signal` | `latency_aligned` | `layout_matrix` | `pluginval_s10_and_clap_validator` | `auval` (macOS) | `state_unaffected_by_bus_enable`.

### F-L11 MIDI output from gap/fill events   (size L; build option)
Params `midiOutMode` (Off/Gaps/Fills/Both), `midiOutNote`, `midiOutChan`. `GAPFILL_MIDI_OUT` CMake option, **default OFF** (changes `producesMidi()` and therefore host routing; enabling is a release decision, VST3/CLAP only, never AU `aufx`). Events: Gaps = NoteOn at gap start (velocity = detector confidence x 127, min 1) and NoteOff at gap end on `midiOutNote`; Fills = NoteOn/Off for each natural fill on `midiOutNote + 1` (velocity = voice amp); Both = both. Time-stamps are **output-domain** sample offsets (aligned with the delayed audio); Options `midiOutOffsetMs` (-100..+100) compensates hosts that do not delay MIDI by PDC. Bounded: <= 16 events/block (excess counted), stuck-note guard (all NoteOffs on panic/bypass/stop/transport jump/release). Output MIDI buffer is pre-reserved in `prepare` (verify with the allocation guard; if JUCE would grow the host buffer, write via a preallocated scratch buffer and swap). Update `COMPLIANCE.md` ("emits no MIDI") in the same PR. **Tests:** `off_emits_nothing` | `gap_notes_pair` | `velocity_mapping` | `max_events_per_block` | `noteoff_on_panic_stop_bypass` | `offset_applied` | `build_option_off_producesMidi_false` | `no_alloc`. **Acceptance:** notes drive a synth in REAPER (manual); unverified until run.

### F-M18 Beat Battles (alternating styles)   (size M; depends F-L07 overlay, existing A/B)
`battleEvery` (Off, 1, 2, 4, 8 bars). Style A and B = the header A/B snapshots restricted to scope `creative, weights, fx, musical` (same whitelist as scenes). Each bar-window index `w = floor(barIndex / N)`; even w -> A, odd -> B, where `barIndex` derives from PPQ and time signature (seek/loop safe; PPQ 0-anchored), applied through the F-L07 **overlay** (no APVTS writes, so host automation values are never touched). Needs valid transport and time signature, else inactive with health note; inactive if A or B was never stored or they are identical. Live indicator "Battle: A" (text+shape). **Tests:** `off_identity` | `parity_by_bar_index` | `seek_loop_reproducible` | `block_size_invariant` | `overlay_only_no_apvts_write` | `inactive_without_transport` | `a_equals_b_inactive` | `panic_suspends` .

### F-M03 Gap timeline (expanded visualiser)   (size M; depends E-3, F0-4, UI-0)
Implements `10` §4.1. Component `Source/features/timeline/Gui.*` in `VisualiserOverlay`; expands in place (visualiser height x2 max, min-size safe), collapsed by default. **Event record** (extends `FillEvent`, 32 B, trivially copyable): `{type: Onset|GapOpen|GapEval|FillStart|FillEnd|Preview|SceneApplied|Panic|Recover, reason, confidence8, pos, ppq?, lenMs, algo, generation}`. Ring: 512 events in the UI-owned `TimelineModel` (message thread) fed by the processor timer from `fillEvents`; the queue stays 64 deep and overflow sets `incomplete` (shown as a striped end-cap "view incomplete"). Layers: events, confidence line + threshold (`kEnterGapConf`), protected zones (from `protectHold`/`protNext`/lookahead values carried in the events), gaps, fills (bars, colour+pattern per family), skipped (hollow glyph with reason letter), preview (hatched), scene/panic markers. Window 8 s fixed (zoom 4/8/16 s); Pause freezes only the display. Reduced Motion: no scrolling animation, redraw 4 Hz. Accessibility: companion list (ListBox, last 20 events as text) reachable by keyboard; every glyph state has text in the legend. Reasons shown are the engine's (never recomputed). **Tests:** `reason_enum_to_text_total` | `record_roundtrip_from_engine_events` | `ring_wrap_512` | `overflow_sets_incomplete` | `pause_does_not_drop_events` | `stopped_transport_ok` | `variable_block_sizes` | `screenshot_min_default_large` | `reduced_motion_static` | `a11y_list_matches_events`. **Acceptance:** for each reason in the E-3 scripted session the timeline text equals the engine record.

### F-D01 Per-bar fill budget and quiet zones   (size M; depends F0-5, phrase/ts infrastructure from C-6/C-7)
Params `fillBudget` (Off,1,2,3,4,6,8) and `quietZone` (Off, Beat 1, Last beat, Last 2 beats, Last bar of phrase). Decision stage (`checkEligibility`, evaluated **before** any RNG draw; per-gap RNG is re-seeded from the key so skipping draws affects no other gap). Needs valid PPQ and time signature, else neutral and health note "Budget needs transport". Fills started this bar = count of entries in the recent-fills ring (last 16 `FillStarted` ppq ticks) whose bar index equals the current bar index -> seek/loop safe, no incremental counter. `fillBudget`: count >= budget -> `BudgetFull`. `quietZone`: a fill may not **start** inside the zone -> `QuietZone` (a fill already running continues); "Last bar of phrase" needs `phraseBars != Off`. Preview/Fill-Now interplay: Fill Now ignores the budget (explicit user action) but counts toward it. GUI: Creative tab, Advanced section: two combos + a thin bar-position strip with the zone shaded and fill ticks. **Tests:** `off_identity` | `budget_n_blocks_n_plus_1` | `new_bar_resets` | `seek_back_recounts` | `quiet_zone_blocks_start_not_running` | `no_transport_neutral` | `reasons_reachable` | `block_size_invariant` | `rng_state_unchanged_when_off`.

### F-D02 Event capture and replay (no audio)   (size L; depends F0-4, F0-6)
Opt-in diagnostics for bug reports and regression: records **parameter changes, commands, transport snapshots (on change), seed, sample rate, block-size sequence, plugin/state version, initial APVTS state, and per-gap outcomes** - never audio, never paths. Recorder: fixed 24-byte records in two SPSC rings (audio-thread producer: commands/transport/outcomes; message-thread producer: parameter edits), capacity 65,536 records total; when full it stops and says so. Zero cost when off (one relaxed atomic load). File `.gfevents` (XML header + base64 records, <= 2 MB). **Replay** (Standalone "Open replay..." and the test harness `ReplayHarness`): re-injects the events into `processBlock` at recorded block boundaries with **supplied** audio (synthetic or user file); reports first divergence of the RejectReason/outcome sequence. Determinism claim (tested): same audio + same events -> identical outcome sequence. Command 29 toggles recording; GUI: Help/diagnostics drawer (Record, Save, event count, size, privacy note listing exactly what is stored). **Tests:** `record_off_zero_cost` | `ring_full_stops_with_flag` | `file_roundtrip` | `replay_reproduces_outcomes` | `rejects_oversize_malformed` | `contains_no_audio_or_paths` | `records_ordered_across_threads`.

### F-D03 Quality modes and adaptive quality (absorbs F-X07)   (size M for modes, L for Auto)
`qualityMode`: **Full** (default, bit-identical to baseline), **Balanced**, **Lite**, **Auto**.
| Aspect | Full | Balanced | Lite |
|---|---|---|---|
| Effective voice pool | 4 | 3 | 2 |
| `SpectralSmear` / SP-0 FFT size | as today | one order lower | one order lower; F-L20, F-X02, F-X01, F-L21 cards disabled ("off in Lite") |
| Cloud max grains/voice | 48 | 32 | 16 |
| Bus-effect tail caps | per card | x0.5 | x0.25 |
| Reported latency | unchanged | unchanged | unchanged (C8) |
`Balanced/Lite` change sound (documented). Switching happens only at a point where **no voice and no FX tail is active** (or via the card `fadeOut`), never mid-fill. **Auto:** starts Full; steps down one level when CPU EMA > 85% for >= 3 s or two deadline overruns within 5 s; steps up when < 55% for >= 20 s; minimum dwell 10 s; current level shown in the footer ("Quality: Balanced (auto)"); in non-real-time host rendering (`isNonRealtime()`) Auto behaves as Full so bounces are reproducible. Measurements from F-S01. **Tests:** `full_golden_identity` | `balanced_lite_voice_caps` | `switch_only_when_idle` | `auto_steps_with_hysteresis_and_dwell` (simulated CPU series) | `auto_full_when_offline` | `latency_unchanged` | `cards_disabled_in_lite_report` | `state_roundtrip`.

### F-D04 Accessibility and contrast presets   (size M; depends U-8)
Options: **Contrast**: Standard / High / Dim-venue; **Control size**: Standard / Large (hit targets >= 32 px, fonts +2 px, via the U-8 layout path); **Focus ring**: 2 px / 3 px; all in the global settings file (per user, not per project). Palettes are token sets from `docs/theme.md`; High meets WCAG 2.2 AA (text >= 4.5:1, UI components/focus >= 3:1), Dim-venue caps peak luminance at ~60% and still meets 4.5:1 for text. Keyboard-only audit of every control incl. all new cards. **Tests:** `contrast_ratios_meet_AA_all_token_pairs` (computed) | `tab_order_complete_and_visible_focus` | `large_controls_min_target` | `settings_persist_global_not_project` | `no_clipping_at_min_with_large` (screenshot) | `reduced_motion_unaffected`.

### F-M22 Smart preset suggestions   (size M; depends U-4, U-6 analyser)
Opt-in button "Suggest presets" (never automatic). Listens 8 s with the U-6 analyser and computes `f1` onsets/s, `f2` median gap (ms), `f3` spectral centroid (Hz), `f4` low/high energy ratio; fixed decision table: `f1 < 2` -> Ambient, Subtle; `2 <= f1 < 6` and `f2 >= 120` -> Rhythmic, Dub, Subtle; `f1 >= 6` or `f2 < 80` -> Subtle, Utility, Glitch; `f3 > 3000` adds Destructive; `f4 > 3` adds Dub; categories are ranked by rule order, top 3 shown at the top of the preset browser with the measured numbers ("6.2 hits/s, median gap 74 ms"). No audio stored, no network, result discarded on close; deterministic for the same input. **Tests:** `table_rules_each_branch` | `deterministic` | `no_audio_retained` | `cancel_safe` | `silence_gives_no_suggestion`.

### F-S10 Genre starter presets   (size S; delivered inside U-10 passes)
Eight additional factory presets in a "Starting Points" group, only through the preset pass (the sole `Presets.cpp` editor), each using **existing** algorithms/parameters (no new DSP) plus the new effect params only after those ship: *Breakbeat Chops* (Stutter/Scramble weights high, density 70), *Dub Space* (Delay/Spectral, width up, Dub card later), *Trance Lift* (PitchRise/Filter, phrase 8), *Ambient Wash* (Granular/Freeze/Texture, low density), *Techno Rolls* (Stutter 1/16, complexity 60), *Hip-Hop Dust* (Crush/TapeStop, subtle), *DnB Tape Stops* (TapeStop/Reverse), *House Filter Rides* (Filter/Comb). Values are starting points tuned by ear in the pass; the U-10 gate tests (`preset_loads_valid`, `preset_produces_fills_on_drum_loop`, peak/finite, documented params, unique <= 32-char names) are the acceptance.

### F-L17 Mutation Lab   (size M; depends U-1/Undo, F-L07 scope lists)
Advanced Creative panel "Lab". **Mutate** creates generation 0..N: 8 candidates by perturbing the current settings: each target gets Gaussian noise `sigma = strength x range` (strength 10-50%, default 25%) from a message-thread RNG with a visible seed (same seed + same start state -> same candidates); targets = scene scope `creative, weights, fx, musical` minus `sceneNeverIDs()`, minus detector/protection. **Hear** applies a candidate as live parameters (one Undo snapshot per Hear session; **Back** restores the pre-lab snapshot exactly). **Pin** (<= 4), **Breed** (uniform crossover of pinned sets + new mutation -> next generation), **Save pinned as scenes/preset**. Session-only: nothing saved in project state; generations kept <= 16. Disabled while Panicked or recording. **Tests:** `same_seed_same_candidates` | `values_in_range_and_forbidden_ids_untouched` | `back_restores_bit_exact` | `pin_limit_and_breed_deterministic` | `save_pinned_as_scene` | `generation_cap` | `no_audio_thread_interaction`.

### F-X05 Local preference learning for variations   (size L; depends U-1, C-11)
History chips get Like/Dislike (thumb) buttons when **Options -> Learn my taste** is on (default Off; nothing recorded otherwise). Traits per fill: algorithm (15), division (12), band (4), length class (4), modifiers (5), stereo mode (4). Score per trait `s = (likes - 2*dislikes) / (likes + dislikes + 4)` in [-1,1]. `learnBias` (0-100) multiplies existing candidate weights in `buildRecipe`/`chooseAlgorithm` by `clamp(1 + 0.8*bias01*s, 0.2, 1.8)` - draw counts and order unchanged, so only the probabilities move and only when bias > 0. Table (<= 4 KiB) lives in the per-user global settings; **Freeze into project** copies it into `<Learn>` so the project renders identically elsewhere (effective table = project's if present, else global). Explainable: panel shows top 3 liked / avoided traits and counts; **Reset** clears. No network, no telemetry, no hidden shifts. **Tests:** `off_identity_weights` | `score_formula_table` | `weights_bounded` | `draw_order_unchanged` | `frozen_table_reproducible_across_machines` | `reset_clears` | `corrupt_settings_ignored` | `bias0_neutral`.

---

## 6. Musical-context tickets

### F-L08 note - phrase offset   (delta to C-7)
Add `phraseOffset` (int 0-15 bars, default 0, group `phrase`): `p = frac((ppq - offset*beatsPerBar) / phraseLenBeats)` (offset taken modulo the phrase length). Tests added to C-7: `offset0_identical_to_baseline` | `offset_shifts_ramp_by_bars` | `offset_modulo_phrase`.

### F-L09 Groove inheritance   (size L; depends E-2 tempo source, F0-5; may change `FillVoice` slice placement, listed hot-file exception like E-2)
Parameters `grooveAmount` (0-100, 0 = off), `grooveBars` (1/2/4). **Analysis** (audio thread, bounded): each detected onset (existing `AnalysisEngine` events, sample-accurate) is mapped to the 1/16 grid via the host PPQ; offset `o = onsetTime - nearestGridTime` in ms; ignore `|o| > 0.5 step` (not groove) and ignore events while transport is invalid or after a jump. 16 slots per bar (time-signature-aware: `tsNum*4/tsDen*4` slots, max 32), each holds an EMA over the last `grooveBars` bars (weights by count; slot confidence `n/(n+2)`); global confidence = mean confidence of occupied slots. **Application:** at fill start the 16/32-entry offset table (ms, clamped +-40) is copied into `VoiceSetup::grooveMs[]`; slice-based algorithms (Stutter slice starts, Scramble slices, Ratchet repeats, Delay taps whose division is 1/16 or 1/8) shift each sub-event by `amount x offset(slot)`. **Never** shifts the fill start, the dry signal or host timing. Jitter from variable host block sizes is irrelevant (offsets come from sample positions, not block edges); transport jumps clear the table. Commands: `GrooveLearn` freezes the table (stored in `<Groove>`, 32 floats), `GrooveReset`. UI: 16-step mini graph + confidence in the Creative Advanced "Musical" row. **Tests:** `amount0_identity` | `straight_input_gives_zero_offsets` | `swung_input_estimated_within_2ms` (synthetic 56% swing at 120 BPM) | `outlier_rejected` | `block_size_invariant_estimates` | `jump_clears` | `fill_start_unchanged` | `only_wet_subdivisions_move` | `learn_freeze_roundtrip_state` | `low_confidence_scales_amount` (effective amount x confidence). 

### F-L10 Call and response   (size L; depends U-1 recipe ring, C-5 adjust stage)
Params `callMode` (Off, Echo, Answer), `callLength` (1-4 fills), `callStrength`. A bounded descriptor ring (8) of accepted recipes `{family, algo, division, lengthClass}`; families: Rhythmic {Stutter, Scramble, Delay, Comb}, Texture {Granular, Texture, Freeze, Spectral}, Pitch {PitchRise, PitchFall, TapeStop, Reverse}, Damage {Crush, Distort, Filter}. Motif found when >= 50% of the last 4 recipes share a family (confidence = share). If not found: ordinary selection and status "No motif - no response". **Echo**: for the next `callLength` fills, restrict the algorithm choice to the motif family (different algorithm, same division class); **Answer**: restrict to a different family (the one with the least recent share) with opposite length class and a division one step finer/coarser. Applied in `adjustRecipe` as a weight mask (zero weights for excluded algorithms) per fill with probability `callStrength`, via sub-RNG `kSalt_Call`; if the mask leaves no fitting algorithm -> ordinary selection. Locked fills win over Call; the response fills are marked in History (flag) and inspectable. **Tests:** `off_identity` | `motif_detection_table` | `echo_same_family` | `answer_different_family` | `no_motif_falls_back` | `mask_empty_falls_back` | `lock_wins` | `bounded_ring` | `deterministic_per_seed` | `strength0_neutral`.

### F-M21 Tension / release curves   (size L; depends C-6/C-7 infrastructure, F0-5)
Params `tensionAmount` (0 = off), `tensionBars` (4/8/16), `tensionShape`. `p = frac(ppq / (bars*beatsPerBar))` (PPQ-0 anchored, valid transport + ts). Shapes `y(p)`: Build `p`; Exp build `p^2`; Saw `p` (hard reset at wrap = the resolution); Swell `sin(pi p)`; Drop `1 - p^2` (all in [0,1]); User A/B/C = piecewise-linear from `<Tension>` (<= 16 breakpoints, x strictly increasing 0..1, y 0..1). Per-target depth `d_t in [-1,1]` (targets: `density, intensity, complexity, chaos` (decision stage); `width, dubAmount, fsdAmount, tdAmount` (block stage)): `value' = clamp01(base01 + amount01 * d_t * y(p))`, applied with the effective-settings rules (base untouched, C-0 ring shows effective). Resolution: at wrap, decision-stage values return to base immediately; block-stage values return via their 20-100 ms smoothing. Cancel: amount 0, transport stop or invalid PPQ -> neutral. Editor: shape combo, target list with depth sliders, mini breakpoint editor for User curves (drag + keyboard add/remove/move), phase marker. **Tests:** `off_identity` | `shape_values_at_quarter_points` | `user_curve_validation` (bad curve drops subtree only) | `loop_wrap_resolution` | `seek_reproducible` | `bounds_0_1` | `block_size_invariant_decisions` | `target_depth_sign` | `transport_stop_neutral` | `state_roundtrip_old_state`.

### F-L22 Musical energy tracking   (size L; depends F0-5; shares `BarStats` with F-X04)
Param `energyAdapt` (0 = off), `energyTarget` (Density/Intensity/Both). Shared `Source/features/context/BarStats.{h,cpp}`: per-window accumulators (onsets, mean-square, spectral-centroid proxy) with windows fixed by **absolute sample count** (0.5 s) so results are block-size independent and transport-free. `e = 0.6*norm(onsets/s over 4 windows) + 0.4*norm(rmsDb)` (norm via running 5th/95th percentile estimates over the last 60 s, clamped); asymmetric EMA (attack 1 s, release 3 s); quantised to levels Low/Mid/High with thresholds 0.33/0.66, hysteresis +-0.05 and minimum dwell 2 s. Effect (decision stage, scaled by `adapt01`): density offset `{+0.15, 0, -0.15}`, intensity offset `{-0.10, 0, +0.15}` for Low/Mid/High respectively (so sparse sections get more fills, busy ones fewer but bolder). Level shown as a small L/M/H chip with text. **Tests:** `off_identity` | `level_hysteresis_no_flapping` | `dwell_enforced` | `block_size_invariant_level` | `silence_stays_low` | `offsets_table` | `target_filter` (Density/Intensity/Both) | `percentile_warmup_neutral` (first 10 s neutral).

### F-X04 Multi-section arrangement awareness   (size XL; depends F-L22 BarStats, C-7, valid transport)
Param `sectionAware` (0 = off). Per-bar feature vector `{onsets/bar, rms dB, centroid proxy, low/high ratio}` computed at exact bar boundaries (split accumulators at the bar's sample position inside the block) into a 64-bar ring. Novelty at bar `b`: normalised Euclidean distance between the mean of bars `b-1..b` and `b-5..b-2` using running per-feature std; **boundary** when novelty > 2.0 and >= 4 bars since the last boundary. Predict the next boundary as `last + phraseBars` if C-7 is set else the median of the last 3 boundary gaps snapped to {4, 8, 16} (needs >= 2 boundaries, else no prediction -> neutral). Effect: with `r` ramping 0..1 over the bar before the predicted boundary, fill probability x `(1 + sect01*1.5*r)` (as C-7, never overriding eligibility) and, mid-section (> 2 bars after a boundary and > 2 bars before the next), density `-0.1*sect01`. Display: section-bar marker on the phrase strip. Validity as C-6; seek/loop clears history. **Tests (synthetic arranged songs):** `off_identity` | `boundary_detected_within_1_bar` (verse/chorus synthetic) | `no_false_boundary_on_steady_loop` | `prediction_from_phrase_bars` | `prediction_from_history` | `never_fills_ineligible_gap` | `seek_clears` | `bar_split_block_size_invariant`. Accuracy is heuristic: acceptance = >= 80% boundaries within 1 bar on 10 synthetic arrangements, 0 false boundaries on 3 steady loops.

### F-X03 Automatic key detection (assist only)   (size XL; depends N-MUS)
Analysis thread (never the audio thread): the audio thread writes a mono stream decimated by `round(sr/12000)` (about 12 kHz, anti-aliased) into an **8 s** preallocated SPSC ring; the thread wakes every 1 s, takes the last 8 s, Hann-windowed 4096-pt FFT frames (hop 1024), peak-picks 65-2000 Hz, folds to a 12-bin chroma (peak-magnitude-weighted, log-compressed), correlates with the 24 Krumhansl-Schmuckler major/minor profiles (Krumhansl, *Cognitive Foundations of Musical Pitch*, 1990) using Pearson r; key = argmax, `confidence = clamp((r1 - r2)/0.25, 0, 1)` x tonal-ness (0 when mean spectral flatness > 0.5 or tonal-peak energy ratio < 0.2, i.e. drums/noise -> "No clear key"). Publishes `{key, mode, conf, seq}` atomically. UI chip: "Detected: D minor (72%)  [Apply to Scale Lock]" (Apply sets `scaleRoot/scaleType` with an Undo snapshot; minor -> Natural Minor). `scaleFollow` (bool): when on and conf >= 0.6 for 4 s, the **effective** root/scale follow (switch only if a new key beats the current by 0.1 for 4 s), base params untouched, shown via the C-0 ring. Runs only while the editor shows the chip or `scaleFollow` is on; <= 2% of one core; stops on `releaseResources`. Optional later: per-hit `PitchContext.sourcePc` (YIN, see F-X01). **Tests:** `synthetic_24_keys_detected_90pct` | `transposition_invariance` | `drums_noise_no_clear_key` | `hysteresis_4s` | `apply_pushes_undo` | `thread_stops_cleanly` | `no_audio_thread_alloc` | `follow_effective_not_base`.

---

## 7. Effect tickets (all depend FX-0; bus/spectral ones on SP-0). Neutral = Amount 0.

Per-card **Amount semantics** are stated; every card also obeys C1-C10, the 20 ms Amount ramp, fadeOut/reset, `sanitise`, per-card tail cap, and runs only on generated wet material.

### F-M16 Rhythmic ratchet   (size M; VoiceFx)
Amount = probability a fill is ratcheted (`kSalt_Ratchet`, 100 = always). Per ratcheted voice a ring (<= 500 ms/ch, preallocated) records the first slice `S = clampToDivision(ratDiv)` of the voice output; repeat `k = 0..ratRepeats-1` re-emits it at `t_k = sum_{j<k} I_j`, `I_0 = S`, `I_{j+1} = max(8 ms, I_j * (1 - 0.5*accel/100))` (accel +100 halves each time, -100 x1.5), gate `ratGate`% of `I_k` with 1.5 ms cosine edges. Tempo: division from valid transport/Sync, else fallback BPM (E-2 memory) with health note. Clipped to the voice end (which already ends before `protNext`); no repeat starts when fewer than 8 ms remain. Max 16 repeats compile-time. **Tests:** `off_identity` | `repeat_positions_exact` (60/120/174 BPM, accel -100/0/+100) | `clipped_before_protNext` | `variable_block_sizes` | `loop_seek_new_fill_only` | `deterministic` | `min_interval_8ms` | `fallback_bpm_flagged` | `no_alloc` | `golden_neutral`.

### F-M14 Rhythmic bit/rate crush   (size M; VoiceFx)
Distinct from `Algo::Crush` and the Crush modifier (those stay). Amount = dry/wet blend of the voice (equal-gain linear). Quantiser `q = 2^(bits-1)`, `y = floor(x*q + 0.5)/q` (platform-independent), sample-hold factor `rcRate` or, with `rcHold` != Free, hold length = division in samples (clamped 1..4096) following the fixed 8-step pattern of multipliers `[1,1,2,1,4,1,2,1]` stepping each division; aliasing is the intended character (documented); `rcTone` -100..0 one-pole low-pass (18 kHz -> 1.5 kHz), 0..+100 one-pole high-pass (20 Hz -> 800 Hz); 20 Hz DC blocker after the quantiser; hard clamp +-1.5. **Tests:** `off_identity` | `quantiser_levels_exact` | `hold_pattern_exact` | `impulse_sine_silence_dc_nyquist` | `nan_recovery` | `channel_separation` | `golden_hash_all_platforms` | `cpu_ceiling` | `tone_filters_corner_freq`.

### F-M20 Tape destruction   (size M; VoiceFx)
Amount = master depth scaling all sub-controls (and the wet blend). Variable-delay line (<= 25 ms, interpolated) modulated by wow (0.4-2 Hz sine + slow seeded noise, depth up to +-6 ms x `tdWow`), flutter (6-14 Hz, up to +-0.4 ms x `tdFlutter`); saturation `tanh(drive x)` with level-matched makeup (`tdSat` 0 = linear); dropouts: rate up to 8/s x `tdDropout`, depth to -40 dB, length 5-80 ms, raised-cosine edges 2-8 ms; splice: up to 4/s x `tdSplice`, read-pointer jump +-(5-60) ms with a 2 ms crossfade. All random events from the voice seed (sub-RNG `kSalt_TapeDestr`) so they repeat per seed; modulation always keeps the read index within the line; 10 Hz DC blocker. Works with or without `Algo::TapeStop` (independent). **Tests:** `off_identity` | `deterministic_dropouts_per_seed` | `read_index_always_valid` | `extreme_settings_finite` | `no_clicks_at_edges` (max sample step) | `dc_after_saturation` | `state_roundtrip` | `cpu_ceiling`.

### F-M15 Comb resonator   (size M; BusFx; benefits from N-MUS)
Amount = wet blend (out = in + amount x resonated). Feedback comb per channel, `f0 = 440 x 2^((note-69)/12)`, fractional delay (Lagrange-3), decay (RT60 0.05-2 s -> `g = 10^(-3*D/(sr*RT60))`, hard cap 0.97), in-loop one-pole damping (`resDamp`), loop DC blocker, 40 Hz input high-pass, gain compensation `sqrt(1-g)`, stereo spread = +-`resSpread`x15 cents detune L/R. `resMode = Scale degrees` (needs Scale Lock on, else behaves Manual): at each fill start (from `BusFxContext::fillStarts`) pick a note among in-scale pitches within +-12 semitones of `resNote` via sub-RNG `kSalt_Resonator`; retune by 10 ms crossfade between two combs. Tail cap 2 s then forced decay; ducked by the engine duck; reset on sample-rate change. **Tests:** `off_identity` | `tuning_error_under_5_cents` (24..96) | `feedback_stable_extremes` | `scale_mode_notes_in_scale` | `retune_click_free` | `silence_denormal_clean` | `stereo_spread_range` | `tail_cap` | `duck_applied`.

### F-M19 Freeze & Scrub   (size M; depends E/C-10 Capture v2)
`scrubOn`, `scrubPos` (0-100% of the captured/last-`captureLength` buffer), `scrubWidth` (10-500 ms). When `scrubOn` and a capture is latched (or a recent-history window of `captureLength` otherwise), the performance voice (F-M10 slot) loops a `scrubWidth` window at `scrubPos` with 8 ms crossfades and equal-power loop smoothing; moving `scrubPos` is smoothed (`posSmoother` 30 ms, bounded speed so scrubbing does not alias: max 8 buffer-widths/s) - it is a granular scan, not a varispeed. Automatable `scrubPos` (host or MIDI-learn). Capture content is snapshotted by the audio thread (no cross-thread reads). Off/Panic: 8 ms fade. **Tests:** `off_identity` | `window_position_exact` | `smoothing_bounded` | `loop_xfade_click_free` | `capture_vs_live_source` | `automation_block_size_invariant` | `panic_stops` | `no_alloc`.

### F-L14 Multiband effect routing   (size M; depends C-9, FX-0)
`bandLowFx/MidFx/HighFx` (bool, default on). The existing LR crossover (`xoverLM/MH`) and per-band amount/intensity/probability already exist and are untouched; this ticket decides which **band voices** feed effect cards: a band voice whose flag is off skips VoiceFx and is excluded from BusFx input. When any flag is off **and** any BusFx is enabled, the engine mixes two wet buses (`wetFx`, `wetPlain`; one extra preallocated stereo block), BusFx processes only `wetFx`, then the buses sum; otherwise a single bus as today. Broadband voices (band = -1) always use effects. **Tests:** `all_on_identity` | `flag_off_skips_voicefx` | `two_bus_sum_equals_single_when_no_busfx` | `bus_input_only_selected_bands` | `lr_crossover_unchanged` | `memory_only_when_needed`.

### F-L19 Frequency-shifted delay   (size L; BusFx)
Amount = wet blend. Stereo feedback delay (<= 2 s/ch, preallocated) with a single-sideband **frequency shifter inside the loop** (every repeat shifts by `fsdShift` Hz, sign = direction): 63-tap windowed (Kaiser) FIR Hilbert pair + quadrature phasor (renormalised every 256 samples, no drift); the Hilbert delay (31 samples) is subtracted from the loop delay (minimum loop delay 64 samples). Time = `fsdTime` division (E-2 clamp rules, fallback BPM) with 50 ms slew on change; feedback 0-85%; damping = 4-pole low-pass in loop (cutoff `lerp(16 kHz, 2.5 kHz, damp)`, never above 0.45 sr); R delay x(1 + 0.25 x stereo) and shift sign inverted at stereo 100%; DC blocker; hard loop gain cap 0.85; tail cap 4 s. `fsdShift = 0` bypasses the Hilbert (plain damped delay, documented). Disable = fade then idle. **Tests:** `off_identity` | `zero_shift_equals_plain_delay_reference` | `shift_accuracy_within_0.5Hz` (sine in, FFT peak) | `negative_shift_direction` | `alias_30dB_below_at_8k_plus2k` | `feedback_stable_max` | `tempo_change_slew` | `sr_change_reset` | `tail_cap_and_panic_clear` | `buffer_limits_192k` | `duck_applied`.

### F-L15 Dynamic dub feedback network   (size L; BusFx)
Amount = wet blend. Stereo ping-pong (dubPing) or parallel delay, `dubDiv` (synced; fallback BPM; 2 s max), loop = high-pass (`dubLow`) -> low-pass (`dubHigh`) 12 dB/oct state-variable filters -> saturator `tanh(1 + 3*dubSat)` with makeup -> feedback `min(dubFeedback, 90%)`; **bounded movement** `dubMove`: low-pass cutoff modulated +-1.5 octave by a tempo-locked LFO (2 bars; 4 s when no transport), updated every 16 samples and smoothed, plus repeat darkening (each pass x0.9 cutoff via the in-loop one-pole). Loop amplitude is bounded by the saturator (<= 1.2) so no runaway; DC blocker; per-card tail cap 8 s (Options 2-16 s) with forced exponential decay after input stops; output soft clamp. Panic and scene transition `fadeOut`. **Tests:** `off_identity` | `impulse_decay_monotonic` | `max_feedback_bounded` | `ping_pong_routing` | `no_transport_lfo_fallback` | `automation_ramps_smooth` | `tail_cap_forced_decay` | `panic_clear_no_discontinuity` | `worst_case_cpu` | `duck_applied`.

### F-L21 Transient-derived convolution   (size L; BusFx; uses `juce::dsp::Convolution`, not SP-0)
Amount = wet blend. **IR capture** (command `IrCapture`/button "Capture IR from last hit"): the audio thread snapshots the strongest event of the last 4 s from `SourceHistory`: segment `[event-2 ms, event+cvSize]`, half-Hann fade-out, high-pass 80 Hz, mono-or-stereo kept, RMS-normalised to -24 dBFS, `cvDamp` applies an exponential high-frequency damping shaped into the IR; the snapshot is handed to the message thread through the SPSC queue and loaded with `Convolution::loadImpulseResponse` (JUCE performs the background work and crossfades; **nothing** allocates on the audio thread). `Convolution` uses `NonUniform{256}` head -> zero reported latency. Max IR 500 ms (<= 192k x 0.5 s x 2 ch); oversize/NaN rejected. **IR audio is never saved in project state**: after reload the card shows "No IR - capture one" and passes dry (neutral); options: *Save IR...* / *Load IR...* (WAV <= 1 s, <= 1 MB, validated) and "Auto-capture on first strong hit" (default off, deterministic for the same input). Disabled in `Lite`. **Tests:** `off_and_noir_identity` | `ir_extraction_known_signal` | `normalisation_level` | `swap_crossfade_no_click` | `oversize_nan_rejected` | `zero_latency` | `state_has_no_audio` | `cpu_budget_500ms` | `tail_cap_1.5s` | `duck_applied`.

### F-L20 Formant shifting   (size L; BusFx; depends SP-0)
Amount = wet blend. STFT (N=1024 @ <= 48 kHz, 2048 above; hop N/4) via SP-0: spectral envelope by cepstral smoothing (lifter `Q = round(sr/1000)`), formant shift warps the envelope on the frequency axis by `2^(fmtShift/12)` and re-imposes `env_warped/env` (gain clamped +-24 dB) onto the **unchanged** fine structure and phases, so fundamental pitch is preserved; `fmtTilt` applies a spectral tilt of `6 x fmtTilt/100` dB per octave about 1 kHz (+-6 dB/oct at +-100); `fmtVowel` A/E/I/O/U applies a fixed three-peak envelope (A 800/1200/2800 Hz, E 400/2000/2600, I 300/2200/3000, O 450/800/2800, U 325/700/2700, Q ~5, strength = Amount) - no speech recognition is implied. **Latency:** the card delays the wet path by `N - hop` samples (declared via `latencySamples()`; FX-0 delays the duck by the same amount so the next hit stays protected; reported host latency unchanged; documented: wet lags gap start by <= 16 ms at 48 kHz). Transient guard: frames with a large spectral flux bypass the envelope warp (identity gain) to limit smearing. Scale Lock (SHOULD): `fmtShift` snapped to in-scale semitones when Scale is on. **Tests:** `off_identity` | `pitch_preserved_within_5_cents` | `formant_peak_moves_by_ratio` | `stereo_phase_coherent` | `transient_timing_within_latency` | `silence_noise_stable` | `extreme_settings_finite` | `latency_declared_and_duck_aligned` | `cpu_memory_budget` | `lite_disabled`.

### F-X02 Spectral freeze and morph   (size XL; BusFx; depends SP-0)
Amount = wet blend. Capture: command `SpectralCapture` (or auto at each natural fill start when Auto-capture is on, default on): the audio thread computes the average magnitude of the last 4 STFT frames of the `SourceHistory` window at the anchor into a preallocated double buffer `frozenMag[2][N/2+1]` (cost: 4 FFTs, bounded; the UI only sees sequence counters, never the buffers). Synthesis sustains the magnitude with propagated (deterministic sub-RNG-initialised) phases for `sfzLength` ms with exponential decay (`sfzDecay`: RT60 0.1-4 s), smear = ±k-bin box blur (k <= 16 x smear), pitch = bin remap by `2^(sfzPitch/12)` (±12 st, interpolated, bounded), width = decorrelation of R phases, morph = linear interpolation of magnitudes between the frozen frame A and frame B (B = second capture, or live source when no second capture). `SpectralRelease` and Panic fade over 20 ms. Memory ceiling: 4 frame buffers x channels x N; FFT plans created in `prepare`. **Tests:** `off_identity` | `capture_release_stress` (10k cycles) | `thread_race_tsan` | `phase_continuity` | `memory_ceiling_recorded` | `latency_declared_duck_aligned` | `panic_fade` | `morph_endpoints_exact` | `sr_change_reprepare` | `golden_tolerance_cross_platform`.

### F-X01 Harmonic granular resynthesis   (size XL; depends N-FX2, N-MUS, SP-0 optional)
Amount = probability a Granular voice becomes harmonic (`kSalt_HGran`). At fill start (audio thread, bounded) run YIN (de Cheveigne & Kawahara 2002) on the last 1024 samples of the 12 kHz-decimated mono source (tau <= 400, threshold 0.15): CPU budget <= 0.3 ms measured on the reference build (if exceeded, move to the analysis thread with one-gap latency and record the change). If `f0` is found with confidence >= 0.7, build `hgVoices` grain streams at intervals from the ordered set {unison, +octave, +fifth, third (major/minor from Scale Lock if on, else major), -octave, +2 octaves}, each stream a N-FX2 cloud whose grain length is an integer number (3-6) of detected periods (20-120 ms) so grains overlap coherently, with `hgMotion` slowly moving the read position (LFO 0.05-0.5 Hz) and detuning (+-15 cents x motion), `hgTone` tilting brightness (one-pole shelf). Total grain cap 96 across streams. No pitch found (drums/noise) -> falls back to ordinary N-FX2/Granular behaviour and the recipe records `harmonic = false`. **Tests:** `off_identity` | `yin_f0_within_0.5_percent` (synthetic harmonic) | `unpitched_falls_back` | `period_coherent_grains` | `voices_and_grains_bounded` | `scale_aware_third` | `cpu_budget_pitch_step` | `deterministic` | `ends_before_protNext` | `no_alloc`.

---

## 8. Routing and system tickets

### F-L13 Sidechain-driven fills   (size L; depends E-3, existing `detSource`)
Params `scSnap`, `scGate`, `scThresh`. Light `SidechainOnsetTracker` (RMS 5 ms, onset when level rises > 6 dB over the 40 ms floor and above `scThresh`; last 8 onsets kept) running **only** when `scSnap > 0` or `scGate != Off` and the sidechain bus is connected (zero cost otherwise); works with any `detSource` (it does not replace detection). `scGate`: *Fill when SC quiet* qualifies a gap only if the SC level < `scThresh` at the decision sample (ducking-style: fill while the vocal rests); *Fill when SC active* requires SC > `scThresh` (fills ride an external rhythm); a closed gate -> `ScGated`. `scSnap`: delays a fill's start (never earlier than the gap start) to the nearest SC onset within `30 ms x scSnap/100` that still leaves `>= minGap`, bounded by available lookahead. With no sidechain connected both are inert and N-USE-b shows `NoSidechain`. **Tests:** `off_identity` | `inert_without_sidechain` | `gate_quiet_blocks_when_active` | `gate_active_requires_signal` | `snap_never_before_gap_start` | `snap_respects_minGap_and_lookahead` | `zero_cost_when_off` | `reasons_reachable` | `block_size_invariant` | `works_with_each_detSource`.

### F-X06 Multi-instance cooperation   (size XL; experimental, depends F-L13 reasons, N-USE-b)
Params `coopGroup` (Off, A-D), `coopMode` (Avoid overlap, Alternate, Follow). Process-wide `CoopRegistry` (`juce::SharedResourcePointer`, 32 fixed slots of atomics: instance id, group, `activeUntilPpq/Sample`, `lastFillStart`, `lastSeenMs`) - no locks, no allocation, no network, no files; a slot not refreshed for 2 s is ignored; slot freed in the destructor/`releaseResources`. *Avoid overlap*: skip a fill (`CoopYield`) if another member of the group has a fill active. *Alternate*: members take turns by group-rank order of instance id. *Follow*: may start a fill only within 50 ms after another member started one. Reads use the previous block's published values. **Honest limits:** hosts that sandbox plugins per process (some Bitwig/Reaper settings) cannot see each other (footer: "Co-op: no other GapFill found"); cross-instance decisions depend on host thread timing, so **results are not bit-reproducible** and are documented as such (default Off keeps everything deterministic). Tests: `off_identity` | `slot_lifecycle_and_stale_expiry` | `avoid_overlap_two_instances` | `alternate_order` | `follow_window` | `lock_free_no_alloc` | `tsan_stress_two_threads` | `single_instance_inert` | `reasons_reachable`.

### F-L24 Controller profiles and feedback   (size L; depends X-1)
Implements the feedback half of X-1 (profiles, Standalone LED echo, disconnect/reconnect recovery); size and tests as in X-1 plus `profile_schema_validation`, `reconnect_does_not_drop_bindings`, `feedback_rate_limited` (<= 50 msgs/s), `no_feedback_loop` (echoed messages are ignored when they match the output device). Real controllers: manual, listed unverified.

---

## 9. Addendum to C-8 Evolve (F-L16)
`04` marks C-8 "stretch"; it is now a normal ticket. Additions: Evolve changes only `fill, intensity, complexity, chaos, density` and the core algorithm-weight pair already specified (never effect cards, scale, scenes, or protection); `evolve` stays in `performanceParameterIDs()`; the waypoint label ("-> Tape Breakdown") is read from an atomic of the last decision; placement: Creative tab Advanced, beside Personality; extra tests `evolve_off_golden` | `waypoint_label_matches_profile` | `block_size_invariant_decisions` | `evolve_with_scene_overlay_precedence` (scene overlay wins for the same target). Dependencies: C-1 (profiles), C-6/C-7 (ts infrastructure), the allowed `Presets.cpp` table-sharing edit.

---

## 10. Parameter ID index (canonical rows: `02-FOUNDATIONS.md` "Permanent parameter registry")

102 new IDs in 30 new groups, plus `phraseOffset` appended to the existing `phrase` group (included in the 102). All append-only; none collides with the 66 shipped IDs (checked against `Source/Parameters.cpp`) or with the existing `02` registry (checked by script before this commit); `validate_repo.py` enforces it from F0-1 on. The group order below is the permanent order after the `02` groups.

| Group (`Source/params/<g>`) | IDs | Ticket |
|---|---|---|
| `swell` | `swellAmount`, `swellCurve`, `swellFilter`, `swellTarget` | N-FX1 |
| `cloud` | `cloudAmount`, `cloudDensity`, `cloudSize`, `cloudSpan`, `cloudJitter`, `cloudPitch`, `cloudSpread` | N-FX2 |
| `scale` | `scaleRoot`, `scaleType`, `scaleAmount` | N-MUS |
| `keydet` | `scaleFollow` | F-X03 |
| `budget` | `fillBudget`, `quietZone` | F-D01 |
| `ratchet` | `ratAmount`, `ratDiv`, `ratRepeats`, `ratAccel`, `ratGate` | F-M16 |
| `rcrush` | `rcAmount`, `rcBits`, `rcRate`, `rcHold`, `rcTone` | F-M14 |
| `tapedestr` | `tdAmount`, `tdWow`, `tdFlutter`, `tdSat`, `tdDropout`, `tdSplice` | F-M20 |
| `resonator` | `resAmount`, `resNote`, `resMode`, `resDecay`, `resDamp`, `resSpread` | F-M15 |
| `fsdelay` | `fsdAmount`, `fsdShift`, `fsdTime`, `fsdFeedback`, `fsdDamp`, `fsdStereo` | F-L19 |
| `dub` | `dubAmount`, `dubDiv`, `dubFeedback`, `dubLow`, `dubHigh`, `dubMove`, `dubSat`, `dubPing` | F-L15 |
| `formant` | `fmtAmount`, `fmtShift`, `fmtTilt`, `fmtVowel` | F-L20 |
| `spfreeze` | `sfzAmount`, `sfzLength`, `sfzDecay`, `sfzSmear`, `sfzMorph`, `sfzPitch`, `sfzWidth` | F-X02 |
| `convo` | `cvAmount`, `cvSize`, `cvDamp` | F-L21 |
| `scrub` | `scrubOn`, `scrubPos`, `scrubWidth` | F-M19 |
| `hgran` | `hgAmount`, `hgVoices`, `hgMotion`, `hgTone` | F-X01 |
| `wetout` | `wetSolo` | F-M17 |
| `safety` | `wetSafety` | F-L25 |
| `midiout` | `midiOutMode`, `midiOutNote`, `midiOutChan` | F-L11 |
| `scdrive` | `scSnap`, `scGate`, `scThresh` | F-L13 |
| `bandfx` | `bandLowFx`, `bandMidFx`, `bandHighFx` | F-L14 |
| `groove` | `grooveAmount`, `grooveBars` | F-L09 |
| `call` | `callMode`, `callLength`, `callStrength` | F-L10 |
| `energy` | `energyAdapt`, `energyTarget` | F-L22 |
| `tension` | `tensionAmount`, `tensionBars`, `tensionShape` | F-M21 |
| `battle` | `battleEvery` | F-M18 |
| `section` | `sectionAware` | F-X04 |
| `learn` | `learnBias` | F-X05 |
| `coop` | `coopGroup`, `coopMode` | F-X06 |
| `quality` | `qualityMode` | F-D03 |
| `phrase` (existing group) | `phraseOffset` | C-7 fix |

Not parameters (state, commands or session-only): panic/recover, scenes/bank, macro LFO fields, commands (§2.3), groove table (`<Groove>`), tension user curves/target depths (`<Tension>`), controller bindings (`<Cmd>`), Fix-it muted rules, preference tables, IR audio (never saved), Options values (hold/latch/recovery timeouts, fallback BPM, contrast, tail caps).

## 11. Hot-file and merge rules for this document's tickets

Extends `01` §3. After F0-10, **no ticket here edits**: `Parameters.h/.cpp`, `CMakeLists.txt`, `Presets.cpp` (except U-10 passes and C-8's table sharing), `HelpView.cpp`, `Tests/TestPlugin.cpp`, or goldens (except in a `[golden]` PR). Single-owner exceptions: `FillVoice.cpp` hook lines (FX-0 adds, up front, the three hooks for VoiceFx and the Swell and Cloud render modes; F-L09 adds the groove slice hook), `PluginProcessor.cpp` bus layout / MIDI-out (F-L12, F-L11), `FillEngine.cpp` stage functions (WP5 order; F-D01, F-L13, F-P02 and N-USE-a each add one early return through hook points reserved in F0-2/F0-10), `PluginEditor.cpp` / `gui/Panels.cpp` (WP11 only). Rule 7 of `01` §2 ("at most 3 open feature PRs") is relaxed to **one open PR per work package** (at most 8 in total), because packages own disjoint files; two PRs touching the same stage function still stack.

## 12. Dependency and merge order

```
F0-1..F0-10 ──► everything below (WP1 merges first, one developer)
UI-0 ──► every ticket with a Performance-tab / Effects-section GUI
E-3 ──► F-S01 ──► N-USE-b ──► N-EASY ; E-3 ──► F-M03 ; F0-4 ──► F-D02
E-1 ──► U-3, F-M10, F-S06 ; E-1 + C-5 + U-1 ──► C-11, U-2, F-L10, F-X05
X-1 ──► N-USE-a ──► F-L25 ; X-1 + F-L07 ──► N-VAL ; F-L07 ──► F-M18, F-P02
FX-0 ──► N-FX1, N-FX2, F-M16, F-M14, F-M20, F-M19, F-L14 ; FX-0 ──► F-M15, F-L19, F-L15, F-L21
FX-0 ──► SP-0 ──► F-L20, F-X02 ; N-FX2 + N-MUS ──► F-X01
N-MUS ──► F-X03 ; N-MUS (soft hooks) ──► N-FX2, F-M15, F-L20
C-6/C-7 infrastructure ──► F-D01, F-M21, F-X04 ; F-L22 ──► F-X04
F-S01 ──► F-D03 ; F-L13 ──► F-X06 ; U-8 ──► F-D04 ; U-4 + U-6 ──► F-M22 ; Undo ──► F-L17
```

**Waves.** 0: WP1 (WP2 in parallel; no source files). 1 (parallel): WP3 (E-3, F-S01), WP4 (E-1, U-3), WP5 (E-2, C-0, C-4), WP6 (X-1, N-USE-a, F-L07), WP7 (N-MUS), WP8 (FX-0, N-FX1, N-FX2), WP11 (UI-0, U-9). 2: WP3 (N-USE-b, N-EASY, F-M03), WP4 (C-5, U-1, C-11, U-2), WP5 (C-1, C-6, C-7, F-D01), WP6 (N-VAL, F-M10, macros), WP7 (F-L22, F-L09), WP8 (remaining voice effects), WP9 (SP-0 and BusFx cards), WP10 (F-M17, F-L13), WP11 (U-4, U-5). 3: F-L10, F-M21, F-X05, F-L21, F-L11, F-L12, F-D03, F-D02, F-M18, F-L17, F-M22, F-D04, remaining UI. 4 (XL, last): F-X01, F-X02, F-X03, F-X04, F-X06. Preset passes U-10a-e close waves 1-4 (sole `Presets.cpp` editor). Each wave ends with a beta tag per `01` §6.

## 13. Work packages (parallel streams, minimal shared-file edits)

Inside a package tickets are serial in the listed order; packages run in parallel. "Owns" = files only that package's PRs may touch (new files unless stated).

| WP | Stream | Tickets (in order) | Owns | Waits for |
|---|---|---|---|---|
| **WP1** | Foundations | F0-1..F0-9, **F0-10** | `Parameters.*`, `CMakeLists.txt`, `FeatureHooks.h`, `Salts.h`, `Effective.*`, `FillEngine.cpp` stage split, `StateSubtrees.*`, `SlotHost.*` | - |
| **WP2** | Platform & decisions | E-5c, E-6, E-7, F-P01, F-P03, F-P04 | `.github/**`, `packaging/**`, `docs/adr/**`, `features/i18n` | credentials; F-P04 after U-9 |
| **WP3** | Explain & assist | E-3, F-S01, N-USE-b, N-EASY, F-M03, F-D02 | `features/{reasons,telemetry,health,fixit,timeline,events}` | WP1 |
| **WP4** | Audition, recipes, export | E-1, U-3, C-5, U-1, C-11, U-2, F-S06 | `features/{audition,lock,history,export,abcompare}` | WP1 |
| **WP5** | Shaping & decisions | E-2, C-0, C-4, C-1, C-6, C-7, F-D01, C-9, C-10, C-3, C-8 (F-L16) | `features/{sync,envelope,personality,breathing,phrase,budget,bandtype,capture2,mistakes,evolve}`; stage functions in `FillEngine.cpp` (serial) | WP1 |
| **WP6** | Live control | X-1, N-USE-a, F-L07, N-VAL, F-M10, F-S07, F-S08, F-P02, F-L25, C-2, F-M23, F-M18, F-L24 | `features/{commands,panic,scenes,bank,live,macros,battle}` | WP1 (F-M10, F-P02 need E-1; F-M18 needs F-L07) |
| **WP7** | Musical context | N-MUS, F-L22, F-L09, F-M21, F-L10, F-X05, F-X03, F-X04 | `features/{scale,context,groove,tension,call,learn,keydetect,sections}`; groove hook in `FillVoice.cpp` | WP1; WP5 (C-6/C-7) for F-M21, F-X04; WP4 (U-1) for F-L10, F-X05 |
| **WP8** | Voice effects | FX-0, N-FX1, N-FX2, F-M16, F-M14, F-M20, F-M19, F-L14 | `features/fx/*`, `features/{swell,cloud}`; the 3 hook lines in `FillVoice.cpp` | WP1; WP5 (C-10) for F-M19 |
| **WP9** | Bus & spectral effects | SP-0, F-M15, F-L19, F-L15, F-L21, F-L20, F-X02, F-X01 | `dsp/spectral/*`, `features/fx/bus/*` | WP8 (FX-0); N-MUS for F-X01 |
| **WP10** | Routing & system | F-M17, F-L12, F-L13, F-L11, F-D03, F-X06 | `PluginProcessor.cpp` (bus layout / MIDI-out lines), `features/{wetout,wetbus,scdrive,midiout,quality,coop}` | WP1; F-D03 after F-S01 |
| **WP11** | UI window & presets | UI-0, U-9, U-4, U-5, U-8, U-7, F-D04, F-M22, F-L17, U-6, U-10a-e (+F-S10) | `PluginEditor.cpp`, `gui/Panels.cpp`, `Presets.cpp`, `features/{info,presetbrowser,viewmode,uiscale,firstrun,access,presetsuggest,lab,calibrate}` | WP1 (UI-0 first); feature GUIs arrive as slot components from WP3-10 |

Effort (my estimate; S 0.5 / M 1.5 / L 4 / XL 8 ideal days): the tickets introduced or completed by this document sum to about **160 ideal developer-days** (8 S, 27 M, 19 L, 5 XL), in addition to the ~112 in `01`. Re-estimate at the end of Phase 0 with measured build/test times.

## 14. Final ticket list (new or newly specified here)

Bold = the six requested new features.

| Ticket | Size | WP | Ticket | Size | WP |
|---|---|---|---|---|---|
| F0-10 pre-registration | M | 1 | F-M16 ratchet | M | 8 |
| X-1 MIDI command map | M | 6 | F-M14 rhythmic crush | M | 8 |
| FX-0 effect-card framework | L | 8 | F-M20 tape destruction | M | 8 |
| SP-0 spectral core | L | 9 | F-M15 comb resonator | M | 9 |
| UI-0 Performance tab / Effects shell | M | 11 | F-M19 freeze & scrub | M | 8 |
| **N-FX1 Reverse Swell** | M | 8 | F-L14 multiband FX routing | M | 8 |
| **N-FX2 Granular Cloud** | M | 8 | F-L19 freq-shift delay | L | 9 |
| **N-USE-a Panic & Recover** | M | 6 | F-L15 dub feedback | L | 9 |
| **N-USE-b Health footer** | M | 3 | F-L21 convolution | L | 9 |
| F-S01 CPU/latency | S | 3 | F-L20 formant | L | 9 |
| **N-MUS Scale Lock** | M | 7 | F-X02 spectral freeze/morph | XL | 9 |
| **N-VAL Scene Bank & Exchange** | L | 6 | F-X01 harmonic granular | XL | 9 |
| **N-EASY Fix-it Suggestions** | M | 3 | F-X03 key detection | XL | 7 |
| F-L07 scenes + quantised recall | L | 6 | F-X04 section awareness | XL | 7 |
| F-M10 momentary/latch | M | 6 | F-X05 preference learning | L | 7 |
| F-S07 Stutter Storm | S | 6 | F-X06 multi-instance co-op | XL | 10 |
| F-S08 Glitch Roulette | S | 6 | F-L22 energy tracking | L | 7 |
| F-P02 Quantised Fill Now | M | 6 | F-L09 groove inheritance | L | 7 |
| F-L25 live safety suite | S | 6 | F-L10 call & response | L | 7 |
| F-M23 modulation / macro targets | M | 6 | F-M21 tension curves | L | 7 |
| F-M18 Beat Battles | M | 6 | F-D01 fill budget / quiet zones | M | 5 |
| F-L24 controller profiles/feedback | L | 6 | F-D02 event capture/replay | L | 3 |
| F-S06 A/B level match | M | 4 | F-D03 quality modes (M) + Auto (L) | M+L | 10 |
| F-M17 wet-only mode | S | 10 | F-D04 accessibility/contrast | M | 11 |
| F-L12 wet output bus | L | 10 | F-M22 smart preset suggestions | M | 11 |
| F-L11 MIDI output | L | 10 | F-L17 Mutation Lab | M | 11 |
| F-L13 sidechain-driven fills | L | 10 | F-S10 genre starter presets | S | 11 |
| F-M03 gap timeline | M | 3 | F-P01 / F-P03 decision ADRs | S / S | 2 |
| F-P04 localisation | M | 2 | C-7 `phraseOffset`, C-8 promotion | (in 04) | 5 |

## 15. Verification and reporting rules for these tickets
Every PR reports, per `01` §5 and `docs/TEST-TRACEABILITY.md`: selected/executed/passed/failed test counts (a zero-test run fails), neutral-golden evidence, state fixtures (pre-feature state loads unchanged), measured CPU/memory, and the list of checks **not** run (real DAW, hardware MIDI/controllers, macOS Intel/Rosetta, AU in Logic, Windows host automation). No ticket is "verified" from CI alone for host-, hardware- or audibility-dependent acceptance.
