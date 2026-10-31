# GapFill completion ledger and shared contracts — 2026-10-31

This is the execution ledger for the owner's autonomous completion assignment of
2026-10-31. That assignment includes the formerly deferred feature extensions.
The older draft finalization prompt is supporting material, not a feature freeze
or permission to omit requested work. This file preserves earlier progress notes.

## Baseline and evidence vocabulary

- Released baseline: `e89aecd218253141cfcd35ad3d9ddfe76c8891e3`,
  `v0.0.2-beta1`. Keep the release tag, binaries, original fixtures and legacy
  golden data immutable. New 0.0.2 fixtures must be generated from that tag.
- Integration base also retains PR #31's seven commits (head `34e7c35`) and the
  independent M0 acceptance-pack work. Their evidence is historical evidence for
  those commits; it does not establish acceptance of subsequent code.
- Released host parameter order has **216 IDs, indices 0–215**. The final group is
  `FM10` (207–215), ending in `fm10_timeout`. FORMANT is 146–149, HGRAN 163–166,
  MIDIOUT 169–171 and COOP 204–205.
- The historical feature register says 65 yes / eight partial / one no / one out
  of scope. Its `yes` means code/test presence, not native host or release approval.
- `implemented`, `integrated`, `verified` and `accepted` are separate states.
  `blocked` names a resource or decision; `unverified` means no retained result.
  No empty report, skipped step, template, finite-audio assertion or prior branch
  run is a manual, audible, physical-device or current-candidate pass.

Every row below has these fields: requirement/source; starting behavior and target
acceptance; dependencies; owner and file boundary; branch/commit; review; evidence;
external dependencies; disposition. To keep the table readable, the last five
fields inherit these defaults unless a row says otherwise:

| Field | Default for every pending row |
|---|---|
| Branch/commit | Not yet assigned/implemented; coordinator records the exact worker branch and commit here before integration. |
| Review | Pending independent diff/contract review. Worker cannot approve its own row. |
| Evidence | Unverified on the future integrated candidate; add exact SHA/configuration/commands/counts/logs and artifact hashes. |
| External dependencies | None for implementation; platform/device/listening/owner checks are explicitly named in the row. |
| Disposition | In scope, pending; no blanket post-1.0 deferral. |

Worker names are task owners, not permanent file ownership. `root` owns shared
registry/processor integration. `state_compatibility` owns state/scene contracts;
`release_status` owns the initial QA tooling lane; `runtime_safety` owns lifecycle;
`ui_verification` owns UI verification. Unassigned lanes remain visible and are
dispatched as concurrency permits. Only the coordinator transfers ownership.

## Shared contract checkpoint

These choices were adopted by the coordinator from the implementation brief.
Changes require an explicit ledger/contract update before dependent workers edit.

### Host IDs, group order and public identity

1. Freeze **all 216 released indices**, not only the first 66. New IDs go in new
   tail groups after FM10, in one coordinator-assigned order. Appending inside an
   earlier shipped group moves later indices even if its own `.lock` and
   `GROUP_ORDER` remain append-only; it also corrupts old index-based scenes.
2. `tools/param_ids/HOST_ORDER` records the released flattened order from e89aecd.
   Preflight validates the actual macro-chain prefix, independent of file order.
   The release prefix cannot be shortened, regenerated from current headers or
   edited to excuse movement. A later public release may deliberately extend it.
3. The registry owner alone edits `Parameters.*`, `params/Groups.h`, group-order
   and new group registration. Feature workers propose IDs, types/defaults,
   classification, layout hook and scope membership first. Do not reorder choice
   values, command IDs, preset order or existing parameter semantics.
4. Retain bundle/CLAP ID `com.gapfillaudio.gapfill`, manufacturer `Gfau`, plugin
   `Gpfl`, existing VST3 identity and AU wrapper identity. Derive auval's subtype,
   manufacturer and type from the configured target rather than a copied string.
5. Preflight protects declarations. Native tests must also compare the actual
   processor host-parameter list and scene index table against the release prefix;
   matching macros alone do not prove createParameterLayout registers that order.

### State, scenes and model compatibility

1. Root `state::kStateVersion` currently equals **2**. Keep it unchanged for new
   optional feature subtrees; each new representation has its own explicit schema
   version and migration. Do not assume doc-12's proposed root version 3 shipped.
   Existing `.gapbank` metadata's `stateVersion=3` is separately historical.
2. Scene v2 budget: **256 KiB**; aggregate optional state: **384 KiB**; outer state
   file bound: **1 MiB**. Preserve legacy scene scope semantics and reader behavior
   through migration, including old defaults and intentionally excluded safety/
   routing/session IDs. Expand recall capacity to include `hg*` and new approved FX.
3. Existing Scene v1 uses native-endian uint16-index/float pairs and does not
   validate its `v` field. New encoding must define byte order, finite-value
   validation and exact schema handling. Do not claim historical readers safely
   reject new data merely because a version number changed; choose and test a
   distinguishable representation/fallback strategy.
4. Existing `SceneBank::toValueTree` stops at 24 KiB, and its caller discards the
   truncation indication. A full bank must never silently lose later scenes.
   Round-trip all 64 scenes at maximum supported scope with nondefault values,
   hostile sizes, malformed payloads and platform exchange. Validate portable
   `.gapbank` size separately: its old 256 KiB XML bound may not hold dense extended
   banks. Coordinate a bounded new representation/file-version limit before use.
5. Resolve/apply state atomically. Bad optional subtrees fail soft alone; invalid
   sound-affecting parameter state leaves current sound and metadata unchanged.
   Project load never automatically recalls a scene. Import never changes live
   parameters until recall. Undo import retains its existing behavior.
6. Preserve legacy Cloud/taste/arrangement behavior for old enabled states through
   **per-model versioned subtrees**, not silently changed permanent defaults.
   A fresh instance may use an improved model. Root approves the model contract
   before any new compatibility parameter is introduced. Cover host state, preset
   files, scene recall, factory initialization, import/reset/Undo and absent metadata.
7. The recipe flags byte is full (bits 0–7 assigned). New harmonic/formant metadata
   needs an additive versioned representation; no stolen flag or changed old bytes.
   Existing immutable release fixtures remain the old-format oracle.

### DSP, transport, MIDI and routing

1. Legacy off/neutral paths perform no added RNG draws or arithmetic that changes
   old samples. Keep salts permanent. New active behavior may change intentionally
   only under the reviewed model/feature selection, with dedicated scenarios.
2. Keep dry/PDC latency unchanged. Existing bus formant `fmt*` remains unchanged.
   Per-voice formant is a new opt-in path with the specified two-processor pool,
   10.7 ms-class onset delay, short-gap fallback and planned end/protection guard.
   Define bus-plus-voice processing explicitly; avoid unintended double application.
3. Audio work is bounded/preallocated. No allocations, GUI access, blocking,
   file/network I/O or unsafe locks; state the reset/release/rate/bypass/jump table.
   GUI stress uses the real message thread/event loop and queued callbacks.
4. Preserve input Main=0/Sidechain=1 and output Main=0/Wet=1. Read sidechain before
   writing potentially aliased Wet channels. Wet bus remains host-owned/disabled
   by default; add mono support explicitly, not by reinterpreting old channels.
5. New main Dry-only mode applies only with Wet enabled. **wetSolo overrides
   Dry-only** as a monitor mode; bypass/panic retain existing precedence; bus off
   forces normal main mixing. Use the specified independent level and 20 ms ramps.
6. Preserve MIDI mode indices Off/Gaps/Fills/Both, old gap/fill note mapping and
   offset semantics. Hit/CC/velocity/early timing are additive. Note-offs outrank
   note-ons on overflow; no input echo; both build configurations get allocation,
   offset, stuck-note and teardown tests. `GAPFILL_MIDI_OUT` stays OFF by default;
   do not change AU metadata or enable it silently in releases.
7. Existing scene Program Change behavior retains precedence on migrated states.
   A new explicit target selection dispatches each PC once (preset or scene), with
   channel/range checks and editor-closed execution. Normalize channel numbering
   at boundaries: BindingTable uses 0=any, 1–16, unlike some spec pseudocode.
8. **Explicit Skip is dislike; unrated is neutral.** Correct DECISIONS' misleading
   wording. Keep commands 31/32 stable; extend profile/schema validation and command
   GUI names, routing and persistence without renumbering previous values.
9. Keep co-op's shipped three modes and 32 slots. Append deterministic weighted
   rendezvous/priority/yield/grid behavior, coherent membership snapshots and musical
   activation generations. Add bounded group-scene generation/cancel semantics.
   No pointers to processors in shared data; no cross-process/cross-format IPC.
10. Timeline grows within Creative content; at minimum size use its specified
    overlay drawer. The detailed F-M03 spec forbids resizing the outer editor and
    hiding Mix/bypass/panic; repair conflicting audit prose instead of violating it.

## Requirement ledger

All paths below are repository-relative. Feature shorthand resolves to the exact
ticket file under `docs/dev/features/`; doc-12 is
`docs/dev/12-SPEC-BACKLOG-AND-NEW.md`. Current baseline descriptions are discovery
facts, not completion claims. Recheck each owner's production source before edits.

| ID / requirement and source | Baseline → target and acceptance | Dependencies | Owner / file boundary | External acceptance |
|---|---|---|---|---|
| BASE — active work, `AGENTS.md`, brief §2 | Pin commits, protect ownership; compare #27/#28/#30/#31 and M0 branch; salvage unique changes without duplicate implementations. | None | root; integration history, issue/PR reconciliation | Repository writes only under existing authorization; publication separate. |
| CONTRACT — host identity/order, brief §4, `02-FOUNDATIONS.md` | 216 shipped indices → complete frozen prefix plus runtime registration/scene-index checks; all new groups after FM10. | BASE | spec_review: `tools/validate_repo.py`, `tools/test_validate_repo.py`, `tools/param_ids/HOST_ORDER`; root: actual registry | Native parameter-list/validator evidence. |
| Q01-A — `Q-01-release-qa-matrix-harness.md` | Fast Linux harness exists → derive correct AU identity; run concurrent lifecycle group; intended unmatched-filter failure only; crash/signal/timeout/launch failure never passes. | BASE, #31 | release_status; `tools/qa/*`, QA tests | Native AU/tool identities. |
| Q01-B — same, brief §5 | JSON/skip tracking partial → compiler/arch/config/tool versions/exact exits/logs; explicit strict acceptance, all manual/unavailable rows visible; meaningful command integration tests. | Q01-A | release_status; QA tooling/traceability | Linux/Windows full evidence, macOS AU. |
| CI — `07-QA-AND-RELEASE.md`, `09-FAST-INCREMENTAL-BUILDS.md` | Full suite Linux; Plugin smoke Win/mac → affected native suites; scheduled/manual ASan+UBSan, separate TSan, full QA, perf/static-analysis evidence; logs on failure. | QA contracts | root dispatch; `.github/workflows/*`, CMake/test configuration | Actual hosted executions, supported runners. |
| W01 — `W-01-windows-editor-automation-crash.md` | Historical crash, later green Windows logs; 40 Linux rounds/no pumped loop → evidence-based not-reproduced status and ≥200-cycle real-loop stress with audio, automation, pending callbacks, presets/state; exact repeat/dump/sanitizer criteria. | #31, native test driver | runtime_safety; `Tests/features/TestEditorLifecycle.cpp`, lifecycle adapter, W-01 evidence | Native Windows 10/11, Windows pluginval 3×, symbols/dump for recurrence. |
| RECORD — QA report / brief §6B | Recorded RecordTap allocate-after-cancel window → deterministic interleaving reproduction, safe acknowledgement/ownership, rearm/cancel/shutdown/editor-close tests. | None | runtime_safety; record/export lifecycle modules | Sanitizer evidence; real export/cancel checks. |
| FIXTURE — brief §6C, `02-FOUNDATIONS.md` F0-7 | Only old fixtures → actual-tag 0.0.2 factory/full-session/mapping/scene/taste/groove/routing/recipe fixtures, corrupt variants; correct restored values and deterministic stable resave. | Immutable beta build | state_compatibility; `Tests/fixtures/`, fixture tests | Exact published binary/config provenance. |
| STATE — optional subtree/compatibility contract | Root2/no-op migration → explicit per-subtree schemas, legacy model paths, malformed/unknown/version guards, atomic/fail-soft semantics and old-fixture protection. | CONTRACT, FIXTURE | state_compatibility; `features/state/*`, feature state adapters; root processor hook | Native old-session/host validation. |
| SCENE — `F-L07-performance-scenes.md`, N-VAL, brief §7G | 128 recall cap, hg omitted, v1/truncation → safe v2, full64 bank, all intended fields/scopes, portable encoding, bounded lossless save/export, atomic import/Undo, exact quantized recall. | STATE, CONTRACT | state_compatibility; `features/scenes/*`, `features/bank/*`, overlay/targets | Cross-OS bank/DAW recall. |
| T01 — `T-01-meter-peak-hold-verification.md` | Arithmetic covered → narrow readout, clock-driven peak/fall and per-platform pixel golden with reviewed tolerances. | Rendering setup | ui_verification; meter tests/GUI helper | Linux/Windows/macOS render baselines. |
| T02 — `T-02-top-bar-8px-grid-pass.md` | Ratchet/three100% before images → three sizes×three scales before/after, required grid/hit targets, no clipping/focus loss; ratchet only improves. | UI ownership window | ui_verification; layout/capture tests; root owns editor wiring | Required owner visual approval. |
| FORMANT — `F-L20-formant-transformations.md` | Bus card only → additive per-voice Preserve/Detail/pool; no-claim/off identity; ≤3-cent pitch, ±1.5dB noise RMS, one-sample onset, prescribed pre-echo, safe short-gap/third-request fallback, sample-rate/bounds/CPU/AllocGuard. | CONTRACT, DSP pool/setup seam | root dispatch; `features/formant/*`; root owns FillVoice/Engine hooks | Listening, UI, reference CPU/native validators. |
| COOP — `F-X06-multi-track-cooperative-gapfill.md` |32slot live subset → deterministic appended mode, priorities/yield/grid/fixed slots/coherent activation; four-member64bar no conflicting owner, share±3points/2000slots, shuffled order/1–4threads/64–512blocks bit identity, removal stability, TSan lifecycle. | Transport contract, tail IDs | root dispatch; `features/coop/*`; root shared hooks | Same-module multi-instance host, isolated-host fallback. |
| COOP-SCENE — brief §7B / detailed co-op stretch now requested | No group recall → bounded generation, common boundary, panic/cancel, missing-peer/teardown/duplicate protection without processor pointers. | COOP, SCENE | Same co-op owner plus state owner contract | Multi-instance host timing evidence. |
| TIMELINE — `F-M03-visual-gap-timeline.md` | Overlay subset → canonical lanes/threshold/outcomes/PPQ/Freeze/scrub/inspector/Event list/drop hatching/seams; compact identity, ≤1.5MiB192k, <2ms paint,4Hz Reduced Motion. | UI shell, reason/event contracts | root dispatch UI lane; `features/timeline/*`, explain tests | Screen reader, host/key/size/scale visual checks. |
| TASTE — `F-X05-local-preference-learning.md` | Measured+7.6points/30 ratings, lowered tests → ≥15points with specified80%consistent30rating users; hidden seeds/populations,0.5–2 cap,<0.1step, no zero-weight/forced/locked bias, private deterministic saved profile. | STATE model version | root dispatch; `features/taste/*`, Taste tests/strip | Multi-instance library and learning usefulness in hosts. |
| SKIP — same, command-map spec | GUI31/32 only → MIDI/profile/schema/name/UI/state support, message-thread model changes; explicit dislike/unrated-neutral semantics. | Command protocol, STATE | root dispatch MIDI lane; `features/commands/*`, live/context adapter | Physical controller/rearm and feedback. |
| PC — `F-S11-preset-favorites-next-previous.md`, N-VAL | Scene PC exists, preset responder absent → target precedence, opt-in/channel/range filtering, editor-closed preset/scene dispatch, bank select ignored; existing burst Undo intact. | MIDI/STATE shared decision | MIDI lane; `commands/*`, live control, preset navigator | Host PC forwarding and keyboard conflict matrix. |
| MIDI-OUT — `F-L11-midi-output.md` | Gap/Fill only → Hit/CC/velocity/early timing; exact32–8192block offsets,≤hop early,32events/block,≤66CC/s,1000seeded no-stuck-note sequences, no echo, off-before-on and flush every transition. | Tail IDs, timing/event contracts | MIDI lane; `features/midiout/*`; root processor hook | ON/OFF native wrappers, actual host routing; AU unchanged. |
| ROUTING — `F-L12-wet-only-output-bus.md` | Stereo optional Wet only → main mode, independent level,20ms ramps, mono/stereo layouts, wetSolo precedence, bus-off fallback, aliasing and exact tap/dry equivalence. | Tail IDs, DSP mix contract | MIDI/routing lane; `features/wetbus/*`; root processor buses/mix | Host routing; AU expansion only after auval+Logic evidence. |
| W03 — `W-03-variable-block-transport-prediction.md` | Core fix/tests present,0.25 tolerance → pure tracker seam, previous whole callback/tempo, once/callback; variable blocks/ramps/steps/seek/loop/noPPQ resets and deterministic fills. | Separate characterized refactor | root dispatch; `features/transport/*`, transport tests; root adapter | Jitter in two real hosts before tolerance tightening. |
| ARRANGE — `F-X04-multi-section-arrangement-awareness.md` |±2s prediction and34%Calm → ±one0.5s frame≥80%, Calm≥40%fewer, stated detector/CPU/memory limits, neutral Learning/no forced ineligible fills. | STATE version, W03 | context lane; `features/arrangement/*`, effective stage | Real songs/loop and reference-machine budgets. |
| CLOUD — doc-12 N-FX2 | Default smoothness misses; dense setting passes → ≥20%lower RMS modulation at canonical defaults/fixture, bounded grains/CPU/protection; old enabled state retains old model. | STATE version | context/DSP lane; `features/cloud/*`, Cloud tests | Listening and reference CPU. |
| HARMONIC — `F-X01-harmonic-granular-resynthesis.md`, doc-12 | YIN/grains exist; old VM budget uncertain → actual required precision/fallback/grain/periodicity/determinism/bounds;≤0.3ms pitch step under reference workload or safe bounded analysis distribution; hg scene recall. | STATE, SCENE, timing seam | context lane; `features/harmonic/*`, Harmonic tests | Bass/lead/vocal listening and native CPU/host runs. |
| KEY/CONTEXT — WP7 / relevant feature files | Key CPU105–180us on loaded VM; energy/groove approximations → reference60us budget and canonical energy/groove/call/tension tests, zero-allocation interactions and documented actual limits. | STATE, DSP integration | context lane; `features/{keydetect,context,groove,callresponse,tension}/*` | Real music/key accuracy, listening, host shortcuts. |
| FX-RESIDUAL — WP8/WP9 and detailed FX specs | Tape timing/capture/scrub/card-routing deviations → inspect each current source against governing requirements; implement genuine omissions without duplicating already closed wiring. | Ledger/spec sweep, DSP owner | DSP lane; affected feature modules | Audible every-effect/preset behavior; latency/protection. |
| I18N — doc-12 F-P04 | Not started → English/pseudo catalogs, explicit per-user English default, fallback/UTF8≤256KiB/placeholders/font coverage; all UI/help/a11y and +40%strings at3sizes×3scales; no translated tokens/IDs. | UI string inventory | root dispatch localization; `features/i18n/*`, `Resources/lang/*`, thin UI adapters | Real screen reader/font/localization review. |
| REMAINDER — brief §7K / audit gap register | Status-file totals conceal small omissions → reconcile every audit row: capture/export, command feedback, scene scope, timing, optional choices, controller profiles, semantic exclusions; each gets evidence or an explicit unresolved row. | All source owners | spec_review ledger sweep; owner implements own slice | Audit gap14 host/hardware/listening evidence. |
| SOAK — brief §8, M7/07 | No continuous harness → seeded real processor with automation/transport/scenes/cancel/co-op; warmRSS≤1%growth, bounds/stuck voices/cost, eight-hour continuous-process run plus documented seven-day acceptance. | Stable integrated fingerprint | CI/perf lane; harness/tests/logs | Persistent runner, actual duration and seven consecutive green nightly days. |
| PERF/ANALYSIS — 06/07 | Baseline/test presence → reproducible per-block worst supported workloads, mean/p99.9/memory budgets, coverage/static analysis/sanitizers at required policy level. | Stable integrated fingerprint | CI/perf lane | Comparable reference environment; no VM-noise assertions as soundness proof. |
| PACKAGE/SIGN — brief §8, E-6/07 | Published unsigned/ad-hoc beta → authorized signing/notarization, provenance/checksums, downloaded packaged binary validators/launch, installation/upgrade/uninstall/retention and accurate notices. | Stable verified candidate | release lane; packaging/workflows/docs | Credentials/certificates, clean native machines; no public publish/tag without separate authorization. |
| HOST/M0 — M0/`docs/qa/README.md` | Templates only → retain original0.0.1 acceptance separately from current candidate; complete authoritative scripts on REAPER+secondDAW and expanded OS/format matrix; retain B-n/S1/S2 triage. | Versioned artifact identity | acceptance lane; `docs/qa/<version>/*` | Real DAWs/devices/MIDI/ears/tester; owner revision if old-script applicability conflicts. |
| DOCS/RC — brief §10, D-01 | Historical/inconsistent prose → accurate Help/COMPLIANCE/VALIDATION/TRACEABILITY/MODULES/feature status/ADRs/release notes; independent row reconciliation and concrete candidate/PR/evidence matrix. | All implementation/evidence | root plus independent reviewer | Owner visual/scope/product acceptance and publication step remain explicit. |
| EXCLUDED — explicit user scope | Free/Pro gating, AAX, AUv3 and iOS stay unimplemented/out of scope; no licensing/telemetry/network addition. | None | All workers | No external dependency; deliberate exclusion. |

## Waves and verification invalidation

1. Freeze global host order, integrate useful existing work and repair QA evidence.
   In parallel: immutable-release fixture build, lifecycle diagnosis, localization
   inventory and UI screenshot baseline. Do not build from a mutable checkout.
2. Review/register shared state, ID, model, bus, event and latency interfaces.
   Dispatch pure models/DSP tests in independent modules. Serialize hot-file hooks.
3. Integrate feature slices in dependency order, review independent diffs and run
   focused plus interaction/native gates on exact integration commits.
4. Start full sanitizers/perf/soak and packaged validation on a suitable immutable
   candidate; retain elapsed-duration limitations honestly. Prepare host acceptance
   while software work continues. Track unresolved work instead of ending early.
5. Independent reviewer closes each ledger row only from production wiring and
   current evidence. Separate implementation completion from external acceptance.

DSP/state/routing/lifecycle changes invalidate affected determinism, fixtures,
sanitizers, performance and soak evidence. Packaging/signing changes invalidate
packaged-artifact evidence. Documentation-only changes preserve binary evidence
only if source/config/dependency/binary hashes relevant to the result match.
Each owner records narrower justified invalidation rather than borrowing an
unrelated branch's green job. No worker modifies a checkout under verification.

## Current slice evidence: CONTRACT host-order guard

- Owner: `spec_review`; branch `codex/spec-contracts-20261031`; commit recorded in
  the coordinator's integration handoff (avoid a self-referential commit hash).
- Reproduced five missing protections against the old validator: earlier-group
  append shifting an old index, missing/empty/duplicate manifest, and missing last
  released parameter. Each was an assertion failure with the validator returning
  no errors; no mock/parser-only success substituted for `validate(root)`.
- After implementation: `python3 -m unittest tools.test_validate_repo`: **25 pass**;
  `python3 tools/validate_repo.py`: **216 IDs, OK**;
  `python3 -m unittest discover -s tools -p 'test_*.py'`: **56 pass**.
- Static declaration guard implemented. Independent review and integration pending.
  Native processor-index comparison/full JUCE checks are not established by this
  Python-only slice; runtime baseline preservation remains a CONTRACT dependency.
- Released canonical216-ID SHA-256:
  `ad0beec298ca9513f7fca2ca5d43eb3b185e610f01a2cfb8f9e7b518e347d26d`.
