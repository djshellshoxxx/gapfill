# F-M23 opt-in extended macro contract

Approved by the integration coordinator on 2026-10-31. This implements the
canonical detailed eight-macro specification additively: its proposed target
indices21..35 conflict with the shipped table, and its v2 `<Macros>` name cannot
safely gate older readers. Neither shipped identity is reinterpreted.

Schemas1/2 keep the existing MacroConfig, four knobs, thirty-six targets, four
legacy LFOs, old evaluation paths, and codecs unchanged. Explicit opt-in creates
`MacrosV3` v3 with eight macros and two global LFOs. The old configuration is
retained in full for return/Undo, including legacy LFO settings. Fresh/default
instances stay schema2 until the user enables the new mode. The V3 tree is
bounded to8KiB of actual escaped XML; no valid admitted configuration may be
silently omitted. A valid V3 wins over valid V2 then legacy; invalid newer data
falls back to the next valid older schema. Only the active schema is written.

The root parameter owner appends MACROEXT after all existing groups:

| IDs | Type/range/default | Count |
|---|---|---:|
| fm23_macro5..8 | float0..100%, default0 |4|
| fm23_src1..8 | choice Knob/LFO A/LFO B/Energy, defaultKnob |8|
| fm23_depth1..8 | float-100..100%, default50 |8|
| fm23_lfoAShape/BShape | Sine/Triangle/Saw/Square/Random step/Random glide, defaultSine |2|
| fm23_lfoADiv/BDiv | sixteen fixed choices below, defaultindex8 |2|
| fm23_lfoAHz/BHz | float0.05..10Hz, skewcentre1, default0.5 |2|

All26 are host-automatable/MIDI-learnable performance parameters, excluded from
Random/Surprise/Undo and included in new scene macro scope. Existing first-four
knob IDs and all existing host indices are unchanged. Mapping tables/names are
non-parameter state. Sources in schemas1/2 remain the released paths regardless
of newly loaded host-parameter values.

Division entries are fixed quarter-note lengths:
0=.25,1=1/3,2=.5,3=.75,4=2/3,5=1,6=1.5,7=2,
8=onebar,9=twobars,10=fourbars,11=eightbars,
12=.125,13=1/6,14=.375,15=4/3.
Bar length is numerator*4/denominator, default4/4. Valid PPQ anchors phase to0;
otherwise exact outputPos/sampleRate chooses free-run phase. Two independent
salted random-access hashes produce RandomStep and smoothstep RandomGlide;
no scheduler/legacy RNG draws occur. Energy reads the exact decision analysis
hop frame and maps0..1 to-1..1. Knob source ignores depth; other sources use
clamp(knob+depth*source,0,1). Inputs sanitize nonfinite values.

Targets0..35 retain shipped meanings. Missing canonical capabilities append:

| Index | Identity | Physical target domain |
|---:|---|---|
|36|mistakes|0..100%|
|37|rcBits|integer2..16|
|38|rcRate|integer1..64, skew midpoint8|
|39|resDecayMs|20..2000ms, skew midpoint200|
|40|resDamp|0..100%|
|41|ratGate|10..100%|
|42|tdMix|0..100% independent wet blend|
|43|fxSend|0..1 multiplier|
|44..46|bandLowAmt/bandMidAmt/bandHighAmt|0..100%|

Existing targets already cover Crush mix24, Resonator mix26, Ratchet mix23 and
Tape depth25. No duplicate target alias ownership is introduced. Target conversion
honors the stated skew and integer snapping. The core21 ownership mask stays
unchanged; advanced runtime ownership widens to32bits for targets21..46.

New inactive Settings fields are `TapeSettings.mixOverride01=-1` and
`ResonatorSettings.decaySeconds=-1`. Negative selects the released exact path;
new mapped targets supply a bounded positive physical value. Tape's detailed
new effect and this independent mix target require one explicit integration
adapter when both modes are present; no disconnected or silent alias is accepted.

New decision order is scene/base -> personality -> Battle -> extended macros
-> Evolve -> Breathing -> Tension -> Phrase, with other existing contextual
stages retained at their defined positions. Width alone is evaluated at chunk
midpoint and uses the existing20ms smoother; Energy-width assignment is refused
by UI/model and engine. Macro FXSend multiplies Tension's send exactly once;
all FX mixes, including per-voice effects, must consume that product. Continuous
bus controls need deterministic processing boundaries, not decision-only fields.

Pure contracts live in `features/modulation/Sources.h` and `ExtendedConfig.h`.
LiveControl owns parameter snapshots/runtime publication; root owns processor
sample/analysis/timing adapters. New modules add no locks, allocation, APVTS access
or I/O on audio. Runtime source values have no mutable phase; reset/release/SR
change/bypass/seek recompute from the next exact decision position. Effective
readout/smoothing caches clear at the existing lifecycle hooks.

Verification must cover legacy byte/sample identity, all47 target ranges,
64-entry bounded parser input and duplicate precedence, maximum escaped names
plus retained legacy tree under8KiB, schema precedence/fallback, scene/import/
Undo, LFO random access/seek/block invariance, deterministic Energy, send
composition, width rejection/smoothing, allocation and actual integrated renders.
This checkpoint contains contracts and neutral fields, not a completion claim.

The first implementation slice adds only pure source functions. A standalone
native probe was RED at link time before implementation, then passed six shape
ranges over120,000 negative/positive positions, meter-aware phase, free sample
clock, huge/nonfinite input, random-channel separation, seek repeatability,
RandomGlide boundary continuity, Knob depth identity and signed Energy/depth.
Five repository tests include allocation guarding. No runtime/host wiring is
claimed by this source-only slice.


### V3 persistence and model slice

The new mapping record is16bytes: u8 target, u8 curve, u8 bipolar,
u8 reserved0, followed by min01/max01/smoothMs as explicit IEEE754float32 LE.
Eight compact M records carry names and up to8 base64 mapping records each.
A nested unchanged MacrosV2 snapshot retains old target-engine selection and
all four legacy LFOs; it is validated against its original2KiB budget. The V3
wrapper uses v3 and enc=m16le, strictly rejects unsupported versions/duplicate
indices/nonfinite/out-of-range/truncated records, and assigns only after complete
validation. New mapping floats roundtrip exactly, without the old permille
quantization. Existing V1/V2 writers keep their serialized form.

A maximum native probe with64records, eight16-quote names and the largest
retained4x8 legacy config/LFOs measured3770escaped XMLbytes, below8KiB. The
actual repository Modulation tests ran in a native focused executable:9passed,
0failed, including allocation guard, exact documented LE bytes, full64record
admission, hostile inputs, precision and opt-in/return snapshot coherence.
Processor project-save/bank/schema-chain integration tests were added and
syntax-checked; they require the coordinator's integrated processor test binary.

SubtreeDesc gains the approved optional alsoSupersedes tail. Valid V3 explicitly
supersedes both V2 and V1; invalid V3 falls back. Only the active schema serializes.
The descriptor count is17. History's input cap grows1KiB→2KiB to admit the
existing16-entry input bound with new optional recipe extras; writer capacity8
and Lock's1KiB/v1 remain unchanged. Invalid extras are independently fail-soft
in the recipe owner's adapter; this does not reinterpret the base recipe.


The coordinator subsequently approved permanent target47=fm20_amount (0..100%),
bringing the new table to48. Shipped25 remains legacy tdAmount. Evaluation must
collect all effective values/ownership first, then apply47 as an active-path
selector before42, then remaining targets in numeric order. Thus one macro can
enable detailed Tape and set its mix, independently of mapping/macro order.
The27 advanced targets21..47 fit the approved32bit runtime ownership mask.

### Typed runtime slice

Runtime and typed48-target projection are implemented separately from live
processor preflight wiring. A focused native repository run now passes17tests,
including9216 first36-field byte comparisons against the repaired V2 arithmetic,
all added physical units, Tape selector-before-mix/lowest ownership, nominal
context restore, pinned-source control grid, negative-position cell midpoints,
all new scene parameter kinds, maximum64-map evaluation and allocation guards.
The pipeline regression was syntax-RED before its new fields existed, then
syntax-GREEN with the new adapters. Actual full processor/frame/host wiring and
that pipeline's integrated execution are still pending.

The new Effective tails are MacroState.extendedRuntime/extendedInputs and
TransportInfo.outputPos/sampleRate/energy01; old aggregate initializers retain
neutral defaults. Runtime effective01/active are audio-owned readout caches;
LiveControl publishes their atomics and resets caches on config/lifecycle changes.
The oscillator itself remains random-access/stateless. ContextState keeps energy
tracking demanded while a V3 mapping33 exists, including instantaneous zero.
Legacy schemas retain the original gate and FX mutation path.

The existing bus Amount path already ramps20ms. New Resonator decay/damping
controls still require their own>=5ms coefficient adapter before continuous
modulation can be accepted; the derived physical field alone is not audio
completion. New PSOLA mix send awaits the shared typed field/bundle. The live
preflight snapshot/grid, exact Energy frame wiring, full runtime integration,
8-macro UI and integrated regressions remain explicit pending work.


### Detailed HG typed scene/send extension

After the shared pitchSync Settings/Voice contracts landed, all seven fx01_*
controls gained actual typed FX references and ScopeFx membership. New-mode send
now multiplies pitchSync.mix01 while preserving mode, intervals, voices and
choice metadata; detailed Tape chance likewise remains unchanged while its mix
scales. The focused native Modulation suite now passes18tests, including the
new seven-field projection and both independent mix/chance assertions. Four
changed units passed syntax. Real processor preflight/voice/capture routing is
owned by the coordinator and remains separate acceptance evidence.
