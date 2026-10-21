# Troubleshooting and bug reports

Use this guide to turn a report into a reproducible failure. Follow the checks in order and keep the smallest failing case.

## Before changing code

Record the commit SHA, build type, operating system and architecture, plugin format, host and version, sample rate, callback sizes, transport state, and whether the failure reproduces after a clean host restart. Include the exact steps, expected result, actual result, and frequency. Attach relevant logs or a stack trace. Do not attach private sessions, recordings, or licensed content unless the owner has explicitly approved sharing them; a synthetic signal or minimal project is usually enough.

## Build and test failures

1. Start with the repository preflight and its unit tests.
2. Keep the full command, tool versions, exit code, and first meaningful error. A final linker error may be downstream of an earlier compile or configuration failure.
3. If CMake appears to use stale configuration, configure a fresh build directory instead of deleting unrelated workspace data.
4. For quick feedback, build the affected target with tools/dev/build-target.sh. Then run the full test and validation sequence before merging.
5. A filtered test run that selects zero tests is a failure. Confirm the output reports selected, executed, passed, and failed counts.
6. Compare against the same command on the target branch before attributing the failure to a change.

## Audio, transport, and timing

For timing reports, include sample rate, callback-size sequence, tempo, meter, play/stop/seek/loop actions, reported plugin latency, and whether the dry path aligns with the input. Try a compact matrix:

| Case | Callback pattern | Transport |
|---|---|---|
| Small blocks | 32, then 8192 samples | play, stop, restart |
| Large then small | 8192, then 32 samples | play and seek |
| Typical mixed | 512, then 4096 samples | loop and tempo change |
| Internal chunking | 512, 64, 2048, 128 samples | repeated seek and loop |

Issue #10 tracks a reported transport-jump defect. Capture a regression case that exercises both callback chunking and genuine host position changes; a sequence of block sizes alone is not proof of a seek. Check dry alignment, no unexpected panic/false trigger, and deterministic scheduling at the same musical position.

For DSP failures, reduce the case to a deterministic synthetic signal and fixed parameter snapshot. Record finite/NaN counts, peak and RMS levels, alignment/latency, random seed, PPQ, and block pattern. Compare bypassed and wet paths.

## Validator and editor crashes

For pluginval or host crashes, include validator/host version, exact command and flags, plugin format, binary hash, exit code, output, crash dump, and stack trace where available. Issue #13 tracks a Windows editor lifecycle investigation. A GUI-skipping validator option may help isolate the failing phase, but it is diagnostic only; it does not establish that editor lifecycle validation passed. Keep the failure scenario intact when checking a fix.

## CI and runner failures

Record the workflow run URL, job name, commit, event type, queued/start times, and whether any step began. A job that never starts is infrastructure evidence, not a product test result. Historical runner-startup reports should be revisited only if a current run reproduces them; see the dated note in docs/COMPLIANCE.md.

## Flaky test policy

Do not hide a flaky failure with automatic retries, weaker assertions, or regenerated goldens. First identify whether the cause is a race, uninitialized state, timing assumption, or external tool. Add a deterministic reproduction, fix the underlying cause, then run the focused case and full required suite.

## Closeout checklist

- [ ] Reproduction steps work from a clean checkout or state the required setup.
- [ ] The fix has a focused regression test with a nonzero executed count.
- [ ] Relevant full tests, validators, and host checks have evidence recorded.
- [ ] The report describes the user-visible result and any remaining unverified platform.
- [ ] docs/TEST-TRACEABILITY.md is updated when an acceptance row changes.

For the evidence fields and release status terms, use docs/TEST-TRACEABILITY.md and docs/VALIDATION.md.