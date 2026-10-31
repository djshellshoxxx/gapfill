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
