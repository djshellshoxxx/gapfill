# Detailed F-X01 opt-in extension contract —2026-10-31

The released `hgAmount/hgVoices/hgMotion/hgTone` path implements doc12's period-
cloud design. It does not implement the detailed ticket's epoch/PSOLA engine,
incremental history-coordinate tracker, selectable interval/scatter/snap controls.
The user's full-completion brief explicitly requires the missing capability.
Add a new opt-in path; never reinterpret or renormalize the four released IDs.

## Parameters and selection

Coordinator appends a new HGEXT group after MACROEXT; this worker does not edit
Parameters/registry/GROUP_ORDER. The seven canonical new IDs, in frozen order:
`fx01_mode` Off/Harmonic(defaultOff), `fx01_intervals` Octaves/OpenFifths/Major/
Minor/Suspended/Scale(defaultOpenFifths), `fx01_voices`1..4(default2),
`fx01_motion`0..100(default30), `fx01_scatter`0..100(default20),
`fx01_tone`-100..100(default0), `fx01_snap`0..100(default0).

ModeOff ignores every new setting and preserves the old hg path exactly. ModeOn
supersedes the old hgAmount decision on Granular voices; unvoiced/invalid-epoch
material falls back to ordinary Granular. Other algorithms and legacy RNG draws
are untouched. New sub-RNG uses the recorded32-bit recipe seed and permanent
HGran salt. Mode is performance state; conditional Random-All includes only
motion/scatter/tone while the new mode is active. Controls latch at fill start.

## Pure modules and owner boundaries

`Source/features/harmonic/psola/Settings.h` defines the small POD contract.
`PitchTracker` owns fixed ring/work/frame arrays. The processor pushes mono ONLY
immediately after actual source-history writes, with the first absolute source
sample position; Capture pauses both together. No audio playhead/PPQ coordinate
is substituted. Every32 input samples computes at most16 lags of512 samples.
Window snapshots and completed PitchFrame histPos refer to the captured source,
not the later completion callback. Query returns median voiced f0/clarity and
voiced fraction within an inclusive source-history window; fraction>=0.6,
clarity>=0.8 and50..1000Hz are required. Inactive push returns without state work.

`PitchMarks` owns one central mono scratch, prepared for at most0.32s/current
sample rate (maximum61440 floats). It filters at4*f0, finds at most512 period-
aligned epochs by bounded local searches, rejects spacing variance>15%, and
keeps marks with a complete2*T source window. Fewer than four usable marks is
fallback. It borrows captured L/R only for the call; no processor pointer escapes.

`PsolaVoice` borrows the voice's stable captured L/R buffers until reset/end,
copies its own epoch table and grain state, and reads each grain at rate1.
New output-epoch spacing changes pitch while preserving the source envelope.
It honors interval tables, key snap, motion/scatter, fixed pans, mono equality,
normalization and tone. FillVoice retains its existing envelope/duck/panic and
planned deadline around the new module. Reset invalidates borrowed pointers.

The detailed six-grain array cannot cover2*T windows at ratio4 (eight overlap).
Use eight per stream, at most32 total, within the old global96 ceiling and the
0.5MiB new-feature memory budget. This resolves the ticket's conflicting bounds;
there is no truncated-window or silently dropped-grain substitution.

Coordinator owns PluginProcessor/FillEngine/FillVoice hooks. The worker owns new
pure modules, parameter builder/lock, tests and self-contained UI/help. No hot
file changes occur before this contract checkpoint is reviewed.

## Recipe, scene and state decisions

FillRecipe stays16bytes and every existing flag bit retains its meaning. New
used/fallback decisions require a bounded versioned sidecar, not flag reuse.
Proposal: HarmonicDecisionMetadata{schema1,outcome,intervalSet,voices,f0Hz},
with distinct optional versioned recipe carriers through event/history/lock/
template codecs. Missing sidecar preserves old recipe replay; unknown/malformed
sidecars fail safely without changing the base recipe. Worker owns the pure
metadata codec; coordinator/runtime owns shared carriers and persistence hooks.
The state worker confirms there is no existing generic recipe-v2 seam.

HGEXT defaults persist as ordinary new tail parameters. No change to the shared
Cloud/Taste/Arrangement model-version tuple is necessary because the new mode
is explicitly opt-in. New scene recall dispatch for these controls must be
specified by the coordinator without expanding historical scene scopes silently.

## Acceptance before completion

Tracker: four-rate voiced/noisy/vibrato accuracy and unvoiced rejection; exact
32/64/480/4096 chunk/frame timestamps; <=8192 MACs/hop; source-coordinate Capture
queries; inactive identity; reset/reprepare; reference cost and AllocGuard.
Epochs: spacing/coverage, short/irregular/nonfinite fallbacks, bounded storage.
PSOLA: target220/275/330/440 peaks within10c, formant preservation/rate1, periodicity
>=0.8 versus ordinary Granular<=0.4, interval/snap tables, mono/stereo, bounded
normalization and grains, deterministic seed, all extremes/fades/deadlines.
Integration: off-path goldens/old enabled hg sessions unchanged; new parameter
normalization/state/scene/metadata/lock/history/template round trips; no flag reuse;
physical host/UI/listening and performance items remain explicitly unverified
until actually run. Do not describe doc12's old engine as detailed F-X01 complete.
