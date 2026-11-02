# F-L21 additive Hit Reverb contract —2026-10-31

Functional contract approved by the coordinator before dependent source/registry edits. The owner
has authorized implementation of the detailed ticket; this checkpoint resolves
its differences from the released doc-12 ConvoCard. This document does not claim
the new engine exists or that any numerical acceptance has passed.

## Compatibility and placement

Keep `cvAmount/cvSize/cvDamp`, their normalization, loaded-IR workflow, existing
worker/card and old active sessions unchanged. A new default-Off mode selects the
detailed Hit Reverb. With that mode on, suppress the old ConvoCard, including at
new Mix0, so mode selection cannot accidentally apply both. It is initially a
parallel return from the generated wet signal at the same protected pre-mix tap;
the new BANDS host later supplies its band-selected input. It never processes
dry input, changes dry/PDC latency, writes existing parameters or alters RNG draws.
Capture candidates read incoming MAIN input independently of the generated
reverb input and the fill's frozen Capture/Scan source. The approved dedicated
Convo-owned live tap records only while detailed mode is enabled; its minimum
correct window is4s strongest lookback+.8s IR+bounded block allowance. Main-event
timestamps and this writer share one input coordinate domain. Do not query a
paused fill history or2.25s Capture tap with current main-event timestamps. Root
owns the main-buffer tap hook, hot adapters and group registration.

Approved new tail group `L21EXT`, host indices286–294 after CaptureEXT285, exact IDs:

| ID | Type/range/default | Contract |
| --- | --- | --- |
| fl21_mode | Off / Hit Reverb, Off | Off retains released cv* path. |
| fl21_mix | float0–100,0 | Return level;0 skips processing/new capture work. |
| fl21_source | Last Hit / Strongest / Manual, Last Hit | Automatic/manual candidate selection. |
| fl21_hold | bool,false | Freeze current IR; block automatic and manual Capture while held. |
| fl21_length | float50–800ms,250 | Clamp by prepared partition cap. |
| fl21_preDelay | float0–50ms,0 | Align shaped IR peak, included within bounded prepared IR extent. |
| fl21_damp | float2000–20000Hz,12000,log | Zero-phase shaping cutoff, clamp below Nyquist. |
| fl21_decay | percent0–100,40 | Tail exponent1+3*decay01. |
| fl21_stereo | Stereo / Mono,Stereo | Fold IR explicitly; actual mono input remains mono. |

All IDs are automatable/learnable, outside existing Random/Surprise allowlists,
and added deliberately to the new scene FX scope. No IR audio, worker queue,
capture, pending status, thumbnail or generation is serialized. Mode Off/absent
is the migration default. State root2 remains unchanged. Project load/reprepare
starts the deterministic fallback IR; scene control recalls never restore audio.
There is no current CaptureIr command (22 is MomentaryReverse,33 PresetGoto).
Append the approved CaptureIr=34, without renumbering any
command. Detailed mode owns it while selected; otherwise dispatch can invoke the
existing ConvoCard::requestCapture. Binding/profile/event validation must admit
the new value explicitly; its source/generation semantics are still bounded.

The approved strict Hold interpretation refuses Manual Capture while held, leaves
the IR unchanged, disables the UI button and reports why. No new candidate may
be admitted while Hold is on.

## Narrow modules and audio data

- `CandidateSelector`: fixed1024 accepted/32 pending descriptors
  `{sourcePosition,strength,epoch}` (about17KiB, exact implementation measured).
  The ticket's eight-entry ring cannot retain exact Strongest over4s at the
  actual5ms minimum detection spacing (up to800 hits), or24 pending hits during
  the120ms confirmation period. The coordinator approved this bounded correction;
  the original eight-entry memory criterion is not claimed met. Overflow is an
  explicit rejected/unknown admission, never an unbounded allocation. Selection
  scans at most1024 descriptors, only at selection, never per sample. There is
  no PCM or host pointer. A candidate requires strength≥0.5, nonclipping peak<.99,
  preceding30ms peak≥20dB below its hit and no stronger event in following120ms.
  Admission is delayed until that future evidence exists. Last/Strongest search
  only the last4s; ties choose the newer timestamp. Source-coordinate resets and
  missing/overwritten history invalidate descriptors. No-candidate Manual request
  reports "No suitable hit yet" and leaves the active IR intact.
- `CaptureJob`: one immutable identity `{generation,sourceEpoch,start,length,
  copied,sourceChannels,settings}`. Audio copies at most4096samples/host callback into a
  preallocated mailbox and verifies the source's oldest/newest bounds before
  every copy, including8192input/4096copy callbacks. An overwritten/changed source
  cancels that generation; it never queues corrupted audio. Internal processChunk
  calls share one beginHostBlock quota; splitting a callback must not multiply it.
  Do not lend a mutable
  SourceHistory pointer to a worker. Only complete jobs enter the worker queue.
- `IrPreparer`: pure shaping plus kernel construction with preallocated FFT plan/
  scratch. Remove DC,2ms fade-in, specified last70% power fade ending exactly0,
  peak-to-predelay alignment, forward/backward2nd-order lowpass, optional mono,
  L2 normalization then L1≤16 cap; reject silence/nonfinite. Deterministic fallback
  uses private seed0x5EED21 and specified exponential room decay, same shaping.
- `PartitionedConvolver`: uniform overlap-save, B512/1024/2048 at≤52/100/200kHz,
  FFT2B, real binsB+1, P=ceil(length/B)≤96. Source extent limit is
  min(.8s,96B/sampleRate); sample-rate support outside the documented table must
  be explicit, not index undefined storage. Stereo input spectra can be shared
  between active/spare kernels because both see exactly the same input timeline.
  The fixed B-sample wet character delay is not reported as plugin PDC. Return
  is Mix-scaled, ducked/protected by the existing wet shaping and bounded with
  the specified1.5 ceiling/sanitise. Mono source/output handling is explicit.
- `HitReverb`: owns the lifecycle, exactly two kernel banks, one shared input
  spectrum history, bounded PCM mailboxes, status/thumbnail publication and worker.
  It exposes prepare/reset/process/clearTail/tailSamples and command Capture.
  The adapter supplies tempo/PPQ, actual source identity/history windows, detection
  candidates, actual source channel count, `nonRealtime`, quality and wet duck.
  It never reads host/GUI state or calls translation/logging from audio.

The contract should use typed settings/context POD, not another generic effect
framework. The later BANDS host can adapt existing stages to a small return-only
interface while retaining the permanent legacy serial chain.

## Ownership, generations and offline behavior

Each kernel bank has one owner: Writable(worker), Ready(published), Active(audio)
or Fading(audio). Release/acquire transfer the bank index plus nonzero64-bit
generation/source epoch. Audio starts a ready kernel's50ms equal-power crossfade
at a block boundary; both banks remain audio-owned for the entire fade. Only
after the fade does it release the old bank for worker writes. A result arriving
from an earlier source/rate/reset generation is ignored. No raw peer/processor
pointer, seqlock over non-atomic shared samples, RT mutex or bank reuse ABA.

New requests while both banks are audio-owned may replace a bounded latest-job
mailbox, not overwrite an in-use kernel. Worker stalls retain the old audible IR.
Panic/bypass cancels pending generations and clears contributions within25ms;
reset hard-clears input histories and selects fallback; release/reprepare signals
stop, joins the worker before reallocating any storage, then prepares fresh plans.
No thread creation/join/destruction or allocation occurs in process.

For `nonRealtime`, use the same completed capture job/settings/generation and run
its preallocated shaping/partition work synchronously at the next block boundary
before adoption. Do not wait on the realtime worker or let worker scheduling pick
the offline adoption point. This work is bounded and only runs on explicit offline
blocks. Consecutive renders with the same audio/callbacks/commands must be exactly
identical. Record the adoption schedule in tests. Realtime asynchronous timing
can be documented, but cannot be used as an excuse for nondeterministic bounce.

## Prepared memory and numerical conflicts to resolve honestly

Store actual required partitions, never more96. With P75 at192kHz/B2048, one
stereo complex-float spectrum bank is75*2049*2*8=2,458,800bytes. Two kernels plus
one shared input history are7,376,400bytes before scratch/PCM. Two bounded.85s
stereo PCM mailboxes add2,611,200bytes. FFT/input/output/shaping scratch and fixed
descriptors add a separately measured bounded amount. Target a documented prepared
budget around11MiB at192kHz, calculate exact capacities in a test, and preserve
the existing card's separately accounted buffers. If96partitions are allocated
instead, the three spectrum banks alone use9,441,792bytes. Do not repeat the
ticket's risk paragraph's "under2MB at192kHz": it conflicts with its explicit
two-bank96partition stereo complex-float design. The48kHz two-filter1.2MB estimate
in§5 excludes the input FDL, PCM mailboxes and scratch.

The separate incoming-audio tap adds about4.85*192000*2*4=7,449,600bytes at192kHz
stereo, plus exact rounded ring capacity/block allowance. It is part of the new
feature's prepared memory, even when idle/disarmed, and must be included in the
reported total. The complete functional path is therefore around18MiB rather
than the kernel-only11MiB estimate. The canonical under2MB figure remains an
OPEN numerical requirement/conflict; correct bounded implementation is approved,
not a waiver or a fabricated memory pass. Do not hide the dedicated tap in
Capture's separately specified memory budget.

The ticket also demands192kHz CPU≤2×48kHz while B and bin count grow4× and P stays
roughly75. Preserve that acceptance target and measure it; the partition table
alone does not prove it. Similarly50ms A/B crossfade uses two kernels but the
swap limit remains≤1.6×steady. Share the input transform/history and evaluate
packed/SIMD kernel accumulation before relaxing any threshold. If strict measured
targets still fail, retain the specific unresolved requirement and optimize;
do not move it behind an external CPU signoff. No silent IR truncation or changed
sample-rate bandwidth is authorized by this proposal.

Tail reporting: detailed IR length+B+predelay is below the base2.5s host tail.
Legacy active serial Dub/FsDelay/freezer tails need the separate approved
conservative predictor. A future parallel stage host uses maximum stage tail;
do not claim that a max bound is safe for the old serial cascade.

## Reviewable implementation slices and tests

1. Pure candidate selection/shaping/fallback plus strict positive/negative/source-
   reset/overwrite fixtures. Direct-convolution scalar oracle at lengths1..max,
   B512/1024/2048, delta/predelay/silence/random/extremes; max error≤1e-6.
2. Preallocated partition core with typed kernel banks, exact B-delay and finite
   bounds. Test every host partition1/32/64/512/4096/8192, same PCM bit sequence,
   mono/stereo, maximum partitions/rates, zeroMix call count, AllocGuard and
   ASan/UBSan. Keep FFT numerical tolerance explicit across different platforms.
3. Ownership/generation worker adapter: stalled worker, latest duringfade, stale
   ready afterreset, repeatedprepare/rate/teardown30+cycles, deterministic offline,
   TSan and no reads of borrowed history. Real-time callback never waits.
4. Default-Off tail parameters/gather/actual UI/help/ParamInfo, source command,
   scene/undo/preset default migration and independent production dispatch tests.
   Verify oldcv* active saved audio and neutral goldens unchanged.
5. Actual processor/cross-effect/hold/capture/scene/panic routing matrix and strict
   reference performance:3% steady at48k maximumIR,192k≤2×, swap≤1.6×/step≤-50dBFS,
   output≤1.5 and25msclear. Retain exact binary/source/config/log fingerprints.

Human listening/real host validation follows implemented software and remains
explicitly open. It is not a reason to leave the deterministic fallback, source
controls, worker contract or actual engine unimplemented.

## Parameter/settings checkpoint

`l21ext.cpp` now supplies the exact nine controls in the table, version hint3,
logarithmic Damp normalization, and empty Performance/Random-All allowlists.
`convreverb::Settings` in `HitReverbSettings.h` uses enabled, mix01, source,
hold, lengthMs, preDelayMs, dampHz, decay01 and stereo. Root gathers the two
percentage parameters with*.01 and choice indices into the scoped enums.
Sanitisation clamps malformed optional values before preparing bounded work.
The pure settings probe passes2/2 under strict GCC warnings; the actual builder
passes JUCE8 syntax compilation. Registry/host/scene integration is pending.
