# Optional host transport timing probe

This separate diagnostic VST3 captures timing metadata only. It does not alter
the GapFill product, record audio, modify MIDI or write from `processBlock`.
The callback copies POD timing into a preallocated 4095-row SPSC queue; a worker
thread formats locale-independent CSV and writes an atomic JSON sidecar.
Overflow is counted rather than retried. A close handshake drains the final
callback before reporting completion. One plugin instance records one session.
The CSV closes at roughly 64 MiB plus the bounded queued tail.

## Build and capture

The target is outside the default product build and defaults to OFF. Use an
existing, clean JUCE Git checkout; configuration does not download dependencies.
The build directory must be outside both source checkouts:

```bash
cmake -S tools/transport_probe -B /absolute/probe-build -G Ninja \
  -DGAPFILL_BUILD_TRANSPORT_PROBE=ON -DJUCE_DIR=/absolute/deps/JUCE \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /absolute/probe-build --target GapFillTransportProbe_VST3 --config Release -j2
```

Use the VST3 bundle under `GapFillTransportProbe_artefacts/Release/VST3/` in
the host's custom plugin path. Its product name and plugin code are distinct
from GapFill. On Windows, select a 64-bit toolchain; macOS/Linux use their normal
JUCE build prerequisites. The code is covered by the repository's AGPL license.

Before launching the host's actual executable from a terminal, set an absolute
capture directory and explicit host/version/scenario labels, for example:

```bash
export GAPFILL_TRANSPORT_TRACE_DIR=/absolute/evidence/reaper-steady
export GAPFILL_TRANSPORT_TRACE_HOST_ID=reaper
export GAPFILL_TRANSPORT_TRACE_HOST_VERSION=actual-installed-version
export GAPFILL_TRANSPORT_TRACE_SCENARIO=steady-60s
```

On Windows PowerShell use `$env:GAPFILL_TRANSPORT_TRACE_DIR='C:\GapFillTrace'`
and the corresponding `$env:` host/version/scenario variables before starting
the host. On macOS launch the application's actual `Contents/MacOS/` executable
so it inherits these variables. The probe editor says Disabled when no directory
is configured, shows the filename while recording, and exposes Finish capture.

Place it immediately before GapFill on the same track and keep both active.
Record at least 60 seconds of steady playback, then separate captures for tempo
ramps, callback-boundary steps, loop/seek, bypass/PDC and sample-rate changes.
Repeat in REAPER and a different host that supplies variable callback sizes
(for example Bitwig). Retain actual plugin versions, routing/PDC settings,
screenshots of the scenario, candidate/probe binary hashes and source revisions.
Reported block-size histograms establish what the host actually supplied.

Press Finish capture and wait for Finalized, or remove the probe to finish and
drain it. Stopping transport alone does not finalize the session. Keep the
matching `transport-<uuid>.csv` and `.json`; scanner-created empty traces do not
qualify. A capture stopped at the size cap must be reviewed for actual duration.
No host or hardware capture has been executed in this workspace.

## Analyze a finalized capture

```bash
python3 tools/qa/transport_jitter.py /absolute/capture.csv \
  --metadata /absolute/capture.json \
  --probe-binary /absolute/actual-probe-binary-inside-vst3-bundle \
  --gapfill-binary /absolute/actual-gapfill-binary \
  --out /absolute/evidence/jitter-analysis.json
```

CSV schema 1 contains exactly these columns:

```text
sequence,playing,ppq_valid,tempo_valid,ppq,bpm,block_samples,sample_rate,sample_position_valid,sample_position,non_realtime
```

Sequence gaps and sidecar dropped-row counters identify capture loss; they are
not transport jitter. Start/stop, unavailable timing, rate changes, offline
rendering and sample-position discontinuities are reported separately. Operator
annotations can identify seek/loop/PDC/rate events at the destination callback:

```json
{"schema":1,"events":[{"sequence":1234,"kind":"seek"},{"sequence":2200,"kind":"pdc"}]}
```

Pass that file with `--markers`. Hashes retain the exact CSV, sidecar, annotations
and optionally both binaries. The analyzer rejects changed input, invalid
numbers/schema/counters and oversized input (CSV 256 MiB/two million callbacks;
metadata/markers 256 KiB). It reports beat residual median/p95/p99/p99.9/max for
previous-BPM, endpoint-average and nearest-of-the-two explicit hypotheses.
Each uses the unchanged 0.25 beat threshold. Values between the two neighborhoods
are not implicitly accepted. Duration is eligible **audio frames/rate**, not wall
time, and non-realtime intervals are excluded from those statistics.

An exit code 0 means the supplied trace could be analyzed. It does not verify a
real host, musical acceptance or a tolerance change: `real_host_acceptance`
remains explicitly unverified pending operator review. A source-dirty or
incomplete capture cannot establish clean-candidate acceptance. Seek plus tempo
changes can be indistinguishable from a valid timing hypothesis; retain sample
positions and annotations when investigating them.

## Local verification scope

`TestTraceQueue.cpp` checks the bounded queue, close race and 200000 concurrent
attempts with exact received+dropped accounting. Build it without JUCE:

```bash
g++ -std=c++20 -Wall -Wextra -Werror -pthread tools/transport_probe/TestTraceQueue.cpp -o /absolute/trace-queue
/absolute/trace-queue
python3 -m unittest tools.test_transport_jitter
```

Nine Python tests use explicitly synthetic traces. A native synthetic harness
(`TestProbe.cpp`, linked with existing JUCE modules and repository AllocGuard)
processed 501 callbacks with 0 allocations and identical audio/MIDI bytes, finalized
501 rows with 0 drops, and successfully fed the analyzer. Its source-dirty and
synthetic-host provenance is retained. Default-OFF CMake configuration and native
JUCE syntax checks pass. The optional VST3 wrapper build, actual host runs and
supported-platform validation remain separate pending evidence.
