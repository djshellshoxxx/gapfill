# Manual acceptance evidence

Automated tests and validators do not establish listening quality, real host
behaviour, or physical MIDI operation. Completed reports belong here, using
[the host report template](HOST-REPORT-TEMPLATE.md). An empty template is not
acceptance evidence.

## M0 gate

The development plan defines M0 as green master CI, **0.0.1 beta** passing
M-1 through M-4 in REAPER and a second DAW, and a triaged B-n bug list. M0 is
still **open**: no completed host reports were present at the 2026-10-30 review.
Feature implementation and a later beta do not retroactively satisfy this gate.

The current integration baseline is 0.0.2 beta (`v0.0.2-beta1`). Record its
acceptance separately. To close M0 exactly as written, test the original
`v0.0.1-beta1` artifacts too, or obtain and record an explicit owner-approved
roadmap revision. Never silently substitute one version for another.

| Acceptance item | Required evidence | Current state |
|---|---|---|
| Automated baseline | Exact source SHA, CI run and job conclusions | See [M0 progress](../progress/M0-baseline-20261030.md) |
| REAPER | Completed M-1..M-4 report with host/OS/format and artifact identity | Unverified |
| Second DAW | Independent completed M-1..M-4 report | Unverified |
| Physical MIDI | Controller model, connection/routing and observed M-4 results | Unverified |
| B-n triage | Reproduction, severity, affected version and disposition for each defect | Existing issues listed in M0 progress; manual findings pending |

## Running and recording acceptance

1. Use the actual release packages or a documented build from an exact commit.
   Record the package URL, SHA-256, source SHA and format. Do not identify an
   artifact solely by its displayed version. Keep the two beta versions in
   separate test locations and verify which binary the host loaded.
2. Copy the template into `<version>/<host>-<os>-<format>.md`. Start every
   result as **unverified**. Record tester, date, host version, OS, driver,
   interface, MIDI controller, sample rate and block size.
3. Run the authoritative [manual scripts](../dev/07-QA-AND-RELEASE.md#4-manual-scripts-run-in-each-host-record-passfail--evidence)
   and [hardware validation](../VALIDATION.md). Record each numbered step,
   observed behaviour and evidence, including failed or blocked steps.
   M0 needs M-1..M-4; later milestone requirements remain separate.
4. Retain reproducible projects and renders. Reference evidence paths or durable
   links; do not commit licensed source audio, private projects or credentials.
   Record input and render hashes, comparison commands and numerical results
   for null tests and deterministic bounces. Listening requires a tester's
   observation and cannot be inferred from finite sample values.
5. Log defects in the triage table. A failed check remains failed until a linked
   fix is verified on a specified artifact. Do not erase the earlier result.
6. Review the reports against every exit criterion. Only then update milestone
   status. CI passing, a standalone smoke launch, or an automated matrix with
   open manual rows is insufficient to close M0.

## Status vocabulary

- **Pass**: performed on the recorded artifact with the expected result and evidence.
- **Fail**: performed with an unexpected result; link a defect/reproduction.
- **Blocked**: could not perform; name the missing resource and next action.
- **Unverified**: not performed or no retained evidence.

Do not count blocked or unverified checks as passes. If a script does not apply
to an older release, explain why and obtain a documented acceptance decision;
do not silently drop it.
