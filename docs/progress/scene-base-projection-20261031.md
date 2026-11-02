# Complete staged scene projection

The actual released e89aecd scope lists are frozen independently of current
parameter groups (default128/all158). New scope2 adds seven Creative shaping
IDs (Ping Pong, Lock on/off, personality, mistakes, envelope shape/amount,
Evolve) and three band-type IDs to Bands. All safety/session exclusions stay
intact. This explicit separation prevents old-indexed additions from silently
expanding legacy recall.

PerformanceTargets complements FxTargets with startup-resolved, bounded
setters at each owning stage: EngineSettings, ModulationInputs and
AnalysisSettings. All178 current admitted IDs resolve. applyBaseTo runs before
Effective::applyBlock; sceneBaseApplied suppresses a second raw scene overlay
at decision, preserving personality then macro order. Legacy views remain
unchanged. Tempo and post-wet overrides use neutral -1 fields and require root
processor integration (sceneMaxFillSync/Division, sceneProtectLowHz/Center01).

Focused tests cover frozen scope membership, complete destination coverage,
raw scene/personality/macro ordering, musical/shaping real units, opt-in
analysis projection and allocation-free cached projection. Syntax checks pass
production and new/existing focused tests. Whole processor boundary/neutral
renders remain the shared integration build's responsibility; do not infer
sample accuracy for every stage from target-table coverage alone.

Executed actual cached-JUCE probe: default legacy128/new145, all new178 IDs
resolve with no missing destination. Raw base -> personality -> macro order,
decision no-double-apply, musical conversions, detector/crossover projection,
and tempo/post-wet destination fields all PASS. Preflight222 PASS. Integrated
processor still must consume overrides and run boundary/neutral audio tests.
