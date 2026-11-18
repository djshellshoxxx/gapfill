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

Snapshots begin every128 decimated samples, retaining more than two seconds at
all supported rates while keeping the16-lag ceiling per32-input hop. Call
`setActive` from the current mode, then push only samples actually written by the
analysis source. Non-contiguous source positions or non-finite input reset the
incremental detector; position overflow is rejected. Reset retains the active
selection; prepare returns inactive. Getter/
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

`PitchSourceRouter` is the production Capture adapter: it maintains one live
detector and immutable frame banks for the frozen history and two snapshots.
History freezes before incoming held audio. Recapture banks freeze at copy start,
remap live endpoints to snapshot coordinates0..N-1, and require matching nonzero
generation before activation. Analysis-writer coordinate restarts cannot erase
the held source bank. Release starts a new history segment; a query crossing the
join is unknown until its whole range belongs to the fresh segment. Frames whose
analysis/filter footprint crosses a source boundary are excluded. There is no
catch-up over old PCM. Cold enabling while already held remains unknown until a
new recapture; incoming audio is never substituted for the unseen old source.

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

Natural detailed selection at zero send records `Outcome::Bypassed` with exact
zero F0/clarity, valid requested intervals/voices and idle telemetry. It does not
query pitch or find epochs and never falsely calls the source unvoiced. Unlike
absent legacy metadata, Bypassed selects the detailed plain-Granular path on
Lock/portable replay and suppresses old Harmonic/Cloud dispatch. An incoming Used
decision at zero current send retains its recorded Used metadata. All outcomes
retain the same12-byte schema1 codec and apply only to Granular recipes.
