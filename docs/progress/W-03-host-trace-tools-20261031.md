# W-03 timing trace tooling

The optional `tools/transport_probe` VST3 and `tools/qa/transport_jitter.py`
provide a concrete capture/analysis path for the two real-host measurements.
See the probe README for exact build, environment, capture and analysis commands.
They do not modify the product processor, publish artifacts or change tolerance.

The processing callback copies bounded POD timing into a preallocated SPSC
queue. CSV formatting, file I/O, JSON/provenance and completion run on a worker.
A CAS close handshake prevents completion racing the last callback. Overflow
and I/O failure stay explicit; incomplete data is never counted as a clean host
measurement. Audio samples, MIDI bytes, project names and credentials are not
recorded. The optional build is disabled by default and uses a distinct identity.
The diagnostic declares MIDI input/output for host passthrough; it records no
MIDI and leaves the supplied events unchanged.

The analyzer preserves raw hashes, declared host/version, source/dirty/compiler/
JUCE provenance, optional binary hashes, block-size/rate/tempo observations,
capture gaps, explicit sample-position discontinuities and operator annotations.
Residual distributions evaluate only eligible intervals, using previous complete
callback duration. It does not widen the region between ramp/step hypotheses or
retune the unchanged 0.25 beat threshold. Parsed data and real-host acceptance are
separate outcomes; no synthetic fixture is labeled as a physical host pass.

Local verification: 9 synthetic Python tests; a warning-clean standalone queue/
close/concurrency test with 200000 attempted rows; default-OFF CMake configure;
actual JUCE source compilation; and a synthetic native 501-callback smoke with
AllocGuard 0, exact audio/MIDI identity, finalized 501/501 rows and 0 drops. The actual
CSV+sidecar was accepted by the analyzer and retains synthetic-host/source-dirty
provenance. No heavy JUCE rebuild or actual VST3 host scan was performed.

Remaining external evidence: build/scan the optional probe on supported systems,
record at least 60 seconds in REAPER and a variable-block host plus the W-03
transport scenarios, review producer overhead and preserve clean source/build
fingerprints. Host PPQ jitter, physical devices, musical results and any proposed
tolerance adjustment remain **unverified** here.
