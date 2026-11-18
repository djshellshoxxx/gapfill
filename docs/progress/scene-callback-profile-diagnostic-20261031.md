# Scene boundary fixture and callback-profile isolation

Native source snapshot:652592c (2026-10-31 integration candidate), exact immutable
headers/app objects from gapfill-baseline and builds/baseline. A separate new
diagnostic test TU was compiled/linked outside that build tree; no production
source, release tag, fixture or golden data was changed. Coordinator granted and
then released this narrow diagnostic lease.

The old SceneProcessor test compared different callback profiles after sample96000:
its manual baseline restarted8192-sample callbacks there, while scene recall
split the original callback and completed its remainder to98304. The corrected
fixture retains the global callback grid after the inserted manual change. Its
original1e-6 sample tolerance remains unchanged. Three stale Creative assertions
now check the deliberate scope2 thirteen-ID whitelist and scope1's frozen six.

Actual diagnostic results (stereo,48kHz,120BPM,2bars,seed37,Fill/Density100):

| Comparison | First sample above1e-6 | Maximum difference | RMS |
|---|---:|---:|---:|
| Old manual callback profile vs scene, FSD70 |120915|0.505164|0.0343726|
| Matched global callbacks vs scene, FSD70 |none|0|0|
| Two manual callback profiles, FSD70 |120915|0.505164|0.0343726|
| Old manual callback profile vs scene, FSD off |129185|0.500758|0.014652|
| Matched global callbacks vs scene, FSD off |none|0|0|
| Two manual callback profiles, FSD off |129185|0.500758|0.014652|
| Isolated FsDelayCard, same two callback profiles |none|0|0|

All processor profiles started11 fills. The diagnostic reports1passed/0failed
because it characterizes and prints comparisons; that count is not an acceptance
pass for callback-profile audio identity. Reproducer and retained output:
`/workspace/work/evidence/state/TestSceneBoundaryDiagnostics.cpp` and
`scene-boundary-diagnostic-652.log`. It uses unchanged baseline TestRig/drumLoop
and FsDelayCard, not a new render oracle.

OPEN product finding: the underlying legacy core audio render depends on host
callback cuts even with FSD disabled. The scene boundary and isolated FSD paths
are exonerated by the matched-profile and card controls. Root cause is not yet
proven; analysis future availability or voice/history horizons are hypotheses.
The coordinator requested causal tracing and preservation of actual beta renders
through a versioned/additive fix if needed. No blanket32-sample cuts, tolerance
relaxation, fixture regeneration or completion waiver is authorized by this note.
