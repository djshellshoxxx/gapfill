# Scene model and state schemas

SceneController owns the bank on the message thread. A scene resolves stable
host indices into bounded Overlay data; SceneEngine alone owns audio-thread
pending/active state. The engine applies decision values at the published PPQ
and the message thread commits the matching request with host notifications.

New scenes can recall up to 256 parameters, including all four harmonic
granular parameters. The overlay uses a fixed-capacity triple buffer and no
audio-thread allocation. EngineTargets maps harmonic controls into the actual
decision settings; unrelated in-flight voices keep their captured setup.

The released Scenes v1 subtree used native-endian six-byte records and a
128-entry limit. It remains a read-only schema. Writers emit ScenesV2 v2 with
little-endian uint16 indices and IEEE float32 values, bounded to 256 KiB.
Released readers ignore the unknown ScenesV2 type; they do not acquire the new
recall capabilities. New readers prefer valid ScenesV2, falling back to a
valid legacy Scenes subtree if the newer subtree is corrupt. A new save removes
the stale legacy representation. Neither schema serializes pending recalls,
audio, running voices, safety state or excluded routing/performance settings.

Each scene records its scope version. Migrated v1 scenes retain the released
scope: harmonic controls and post-beta parameter IDs are left unchanged when
those scenes are recalled. Newly captured scenes use scope version 2. All 216
released host indices must remain stable; new parameter groups belong after
the released tail rather than inside an earlier group.

Serialization is transactional. A capacity/validation error returns an explicit
failure and never saves a successful-looking prefix. The project subtree
budget is 384 KiB and the outer processor-state cap remains 1 MiB. The host
integration must check state::writeSubtrees' WriteResult and retain its previous
valid state if saving fails.

Group recalls use gotoSceneAt(index, absolutePpq). Their fixed boundary is
preserved during manual-edit updates. A missed deadline, absent transport or
pending transport discontinuity cancels the request; Result::missedDeadlineId
must be acknowledged on the message thread with rejectPending. Ordinary scene
recalls preserve their existing quantization and fallback behavior. Group
membership, generation and cancellation broadcasts belong to the cooperative
registry, never to peer processor pointers.

Tests: SceneFormat, Scenes, SceneBank and State. SceneFormat covers the fixed
wire bytes, complete banks, legacy scopes, harmonic overlays, invalid versions,
transactional failures, schema precedence and fixed group deadlines. Continuous
bus-effect scene timing requires separate segmented processor integration; the
harmonic/decision tests do not establish that broader timing claim.

Scene preflight consumes/arms the published request and queries its first
boundary sample before history/analysis side effects. When this API is used,
the engine keeps the generation pending until the first actual boundary sample;
the end of an old-setting segment is not an early commit. The processor must
call preflight for every segment, forward rejection results and split at the
returned sample offset. A zero offset means the scene applies at segment start.
Group cancellation metadata is part of the same Overlay publication as its
request ID, and must be compared after staging and before processing.

New scene dispatch resolves every shipped FX parameter and the six opt-in
per-voice formant controls. Migrated scope-v1 scenes keep their legacy dispatch
as well as their legacy parameter scope. Recapturing/storing a scene creates
the new scope/dispatch version. The legacy shared macro/battle lookup remains
unchanged; enabling previously disconnected advanced macro mappings needs its
own versioned contract and explicit upgrade.

Scenes also carry the Cloud model version as a separate optional `cm` field
(portable bank `cloudModel` attribute), never as a parameter index. Missing
metadata, including released scenes, selects model 1. Capture reads the
canonical APVTS ModelVersions child only when FX scope is selected; that domain
travels coherently with the overlay and both effect-only/full projections
apply it at the boundary. Other model domains are untouched: Taste is session
learning state, and the canonical scene scope lists contain no section-aware
arrangement controls. A later model scope addition must be explicit.

The controller commits only the Cloud domain back to the canonical child on
message, then invokes setModelVersionsCommittedCallback so the processor can
publish its coherent model selection. Root integration must install that
callback to syncModelVersions(). A manual model upgrade while a recall is
pending removes that model domain from the overlay and commit. Other pending
parameters still apply. Model-only overlays are supported even with no raw
parameter entries. Tests live in TestSceneModels.

Released scope membership is now a separate explicit ID table, frozen from
the actual e89aecd sources: default128 IDs, all scopes158. New scope2 adds
Ping Pong, Lock boolean, personality, mistakes, fill envelope and Evolve to
Creative, plus per-band fill types to Bands. It never adds a recipe or held
state. Old scenes keep their exact old ID lists despite these old-indexed
parameters becoming scene eligible for new scenes.

New views project through `applyBaseTo(settings, modulationInputs)` after raw
gather and before personality/macros. Macro values and personality target their
actual ModulationInputs fields; the scene base flag prevents a second raw
projection from erasing those effective outputs at decision. Legacy scope1
keeps its released decision overlay. `applyAnalysisTo` handles opt-in detector
and crossover settings before analysis. `PerformanceTargets` resolves the
remaining musical/shaping destinations with the same units/choice conversions
as their gatherers. Tempo and post-wet values use neutral -1 override fields;
the processor must consume them in tempo resolution and wet post-processing.
All178 currently admitted v2 IDs have explicit destinations across these DSP
stages; that coverage does not replace end-to-end processor timing tests.

The Performance panel exposes eight numbered scenes per page across eight
pages, bank naming, per-scene rename/duplicate/move/delete and wrap/no-transport
options. Reordering goes through controller APIs that cancel stale requests and
preserve last-good identity. Store/delete require confirmation and verify the
scene did not change during the dialog. Every asynchronous callback holds a
component SafePointer. Export offers selected/whole bank and optional mappings;
import presents every skip/unknown/digest detail in a scrollable review before
an explicit scenes-only or scenes-plus-mappings choice. Import never recalls a
scene. Choosing mappings can affect already active macros and says so.

The import undo action can include a message-thread model snapshot, preserving
mapping undo across editor closure. A new import/reset clears that sidecar;
undo invokes it exactly once. No callback captures an editor. User names stay
verbatim through localization; native/modal text uses complete translation
templates with names inserted as data. The panel polls only bank/name/current
identity and language generation; it does not serialize full dense banks on
every UI timer tick.
