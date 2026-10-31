# Opt-in detailed harmonic path

This directory implements the detailed F-X01 path beside the released hg*
period-cloud engine. `Settings.h` is neutral by default. Root/processor adapters
own the mode selection, captured audio and persistent carriers; these algorithms
own no GUI/framework/host pointer. The complete boundary contract is recorded in
`docs/progress/F-X01-EXTENSION-CONTRACT-20261031.md`.

`PitchTracker` has one audio owner, fixed arrays and no allocation. Prepare derives
a power-of-two decimator, a fourth-order1kHz lowpass and the50..1000Hz lag range.
Each32-input-sample hop processes at most16 independent512-sample lag sums. A
completed frame stores its snapshot's inclusive source-history endpoint; callback
completion time is never substituted. Query uses bounded stack scratch to return
the median voiced pitch and fraction in an inclusive source window.

Call `setActive` from the current mode, then push only samples that were actually
written to source history. Capture hold makes no push call. Non-contiguous source
positions or non-finite input reset learned signal/history; position overflow is
rejected. Reset retains the active selection; prepare returns inactive. Getter/
query/frame methods share the audio owner and must not be called concurrently
from the UI. Publish meter atomics from the adapter instead.

The short512-sample window uses a3dB first/last-third stationarity gate; the old
1024-sample6dB gate accepted steady decaying55Hz kicks and failed M2. A20-cent
endpoint interpolation allowance is clamped back inside50..1000Hz; it prevents a
nominal1000Hz source being rejected by a tiny YIN interpolation bias. These are new
opt-in detector choices, not changes to the released synchronousYin implementation.

`Metadata` owns the pure12-byte LE optional decision codec. Missing metadata keeps
legacy recipe behavior; the outer RecipeExtras carrier is independently versioned.
Its base FillRecipe remains16 bytes with all eight existing flags unchanged.

Epoch finder and pitch-synchronous captured-buffer voice are the next separate
modules; their production wiring is not claimed by this tracker/codec checkpoint.

`PitchMarks` is the single prepared mono scratch owner for sequential fill starts.
It leaves captured L/R untouched, returns owned ascending epoch marks with full
source-window coverage, and rejects short/invalid/irregular captures. `Voice`
copies those marks and borrows its own FillVoice capture until reset/end. Source
read positions advance by exactly one; pitch comes from output-epoch spacing.
Upward windows are2*min(T0,Tout) to avoid the proven fixed-window sine cancellation.
A5Hz DC guard removes the local mean introduced by shortened windows. Per-stream
Hann rotations are prepared once and advanced by a double recurrence, not a cos
call for every sample. Mono source channels explicitly bypass stereo panning.

The internal Settings.mix01 is the derived global-FX-send weight, never a host
parameter. At zero the adapter skips pitch query/epoch/configure. `Voice::render`
returns unweighted PSOLA; `mixWeight` lets the adapter blend ordinary Granular by
1-mix and PSOLA bymix. Existing incoming recipe choice metadata is retained at
zero send. Missing/invalid epoch data falls back before playback; a runtime source
fault stays silent in the selected engine until the original deadline.
