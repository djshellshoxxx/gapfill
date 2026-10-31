# Scene compatibility and persistence work

Base: 14323940431fc3709c6be0d3ec8428039dafbcaf (master e89aecd plus reviewed
PR 31, M0 acceptance documents and PR 30's finalization plan).

Implemented:

- ScenesV2 schema with fixed little-endian records, explicit validation and
  256-entry bounded overlays; old Scenes v1 remains read-only.
- Versioned scope semantics preserve old scenes' behavior while new scenes
  capture and apply all four harmonic granular parameters at decision PPQ.
- Complete 64-scene persistence with 256 KiB scene and 384 KiB total subtree
  budgets, transactional state writes and explicit serialization errors.
- Portable bank format 2, v1 read compatibility, lossless float32 text and
  version-aware integrity digests, with a bounded 1 MiB portable-file cap.
- Fixed-PPQ scene requests for cooperative scheduling, including rejection of
  late or invalid/discontinuous-transport requests and message-thread rejection
  acknowledgement. Regular scene quantization is unchanged.
- SceneFormat regressions plus maintained Scenes/SceneBank/State expectations
  for the explicitly versioned format and admission rules.

Verified so far:

- Repository preflight: pass, 216 locked IDs.
- Completeness audit: zero failures, the same eight existing exemptions.
- Python tooling: 49 tests pass.
- g++ C++20 syntax checks against the actual JUCE 8.0.15 headers: production
  scene/model/controller/overlay/state/bank/target files and the new/existing
  scene test files compile. Existing unrelated warnings remain visible.
- Standalone scene-wire probe with -Wall -Wextra -Wconversion -Werror: fixed
  known byte vectors and 3,856 round-trip cases pass.

Not yet verified: native JUCE test execution, sanitizers, native Windows/macOS,
DAW/Logic acceptance and cross-OS bank exchange. Tests were written first, but
an executed native red run was unavailable while the coordinated baseline
toolchain/build was being prepared; no red-run claim is made.

Integration required: PluginProcessor must check state::writeSubtrees' returned
WriteResult before copying a new blob and surface its error. LiveControl must
route missedDeadlineId to SceneController::rejectPending. The coordinator owns
those shared hooks. Current tests establish harmonic and decision overlays;
complete bus-effect timing is a separate pending integration slice.

The auxiliary 0.0.2-beta1 fixture writer is prepared in a separate worktree at
the immutable e89aecd release commit. Its production-source diff is empty; only
test glue and its fragment were added. It must be built/run there, with output
and provenance retained, before importing any new release fixtures. Fixtures
have not been fabricated from the improved implementation.

## Actual released-state fixtures

The separate exact `e89aecd218253141cfcd35ad3d9ddfe76c8891e3` tag build executed
`ReleaseFixtureWriter`: **1 passed, 0 failed**. Its production source diff was
empty; 41 factory programs, two non-default sessions and two corrupted blobs
were copied byte-for-byte into Tests/fixtures with generated old-reader values,
old-reader audio fingerprints, SHA-256 checksums, production git blob IDs and
the auxiliary writer source. The new StateCompat002 tests cover all 43 valid
states, stable resave, finite rendering, platform/compiler-matched old audio,
and rejection of both corrupted blobs. Their source was syntax-checked, but
execution against the integrated new processor remains pending the shared
native build. Do not conflate the successful **old writer** run with those
new-reader tests.
