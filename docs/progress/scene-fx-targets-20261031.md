# Complete scene FX targets and boundary preflight

Depends on state-core ddb28d3 and the coordinator's append-only voice-formant
contracts/registry (9662c49, c68d19d, bba73e4).

Implemented a complete audio-safe scene FX lookup, including the six new
voice-formant controls, with direct real-unit conversion and bounded runtime
dispatch. The legacy macro/battle lookup remains unchanged so unrelated scene
work cannot silently activate previously disconnected old macro mappings.
New scene scopes include the voice-formant group. Migrated scope-v1 scenes use
the released dispatch semantics; a newly captured scene uses complete targets.

SceneEngine now exposes stagePublished and samplesUntilBoundary. The first
consumes/arms without advancing the timeline; the second is pure and bounded.
Preflight-aware application waits until the first actual boundary sample. Group
ID and cancel epoch are published coherently with the scene request, retained
during manual edits and exposed through audio-thread-only pending getters.

Verification:

- Repository preflight: 222 stable IDs pass.
- g++ C++20 syntax checks against real JUCE headers: FX/engine targets,
  SceneModel, SceneOverlay, SceneController and new target/split tests pass.
- Cached-JUCE numerical FX probe: amount/unit conversion, feedback precision,
  integer targets, per-voice enable/disable, bus classification and invalid
  targets/NaNs pass actual runtime assertions.
- Cached-JUCE scene model probe links the real current SceneModel/Overlay,
  target code and all 222 parameter IDs; identical absolute boundary sample
  96000 passes for 32/64/512/8192-sample callbacks, as do fixed metadata,
  pre-boundary pending state, FX application and late-deadline rejection.
- FxTargets and SceneSplit regression suites cover the model contracts and
  compare each FX target's units against direct parameter gathering.

Pending: full new native suite execution and processor integration. These model
tests do not establish audible sample-exact bus transitions. The processor must
split before history/analysis side effects, preserve W-03's one host-callback
tracker update, and handle plain-band routing and bus-card contexts correctly.
LiveControl must check group cancellation after staging, forward rejected IDs
and apply only appropriate FX values at an actual segment boundary.

Discovered separate F-M23 work: old advanced target indices were present but
disconnected; enabling them requires legacy/new macro state schemas, explicit
upgrade, sufficient ownership bits and the documented target reconciliation.
