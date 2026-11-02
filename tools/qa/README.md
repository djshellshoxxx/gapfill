# QA matrix evidence

`run_matrix.py` executes the configured tests and validators; `summarise.py` checks their evidence. Both work with Python's isolated mode (`-I`) and only use the standard library. This tooling does not establish host, hardware, listening, signing or release acceptance.

## Run against an immutable build

Use the exact source/dependency checkout that built the binary, with artifacts outside the source tree. Example (supply the actual tool and bundle paths):

```sh
python3 -I tools/qa/run_matrix.py \
  --binary /tmp/gapfill-build/artefacts/GapFillTests --tier full \
  --build-dir /tmp/gapfill-build --build-config Release \
  --out /tmp/gapfill-evidence/full \
  --pluginval /tmp/tools/pluginval --vst3 /tmp/gapfill-build/GapFill_artefacts/Release/VST3/GapFill.vst3 \
  --clap-validator /tmp/tools/clap-validator --clap /tmp/gapfill-build/GapFill_artefacts/Release/CLAP/GapFill.clap
```

On macOS, pass `--au <actual .component bundle>` so `auval` uses its `AudioComponents` identity. Without it, the literal JUCE CMake declarations determine the identity; unresolved declarations fail with a request for the built bundle. On other operating systems, the AU row stays visible as explicitly inapplicable. A missing macOS validator remains unverified.

For a synthetic PR merge checkout, `--source-head <full PR head SHA>` records the source head separately. The tested SHA is always observed from the checkout. Build configuration and compiler ID/version are read from CMake metadata. Missing or ambiguous data stays unverified. Dependencies, source cleanliness and the binary hash are recorded. CI must establish the actual source/build association: observing a checkout and a binary cannot prove how that binary was compiled.

Use a separate `--out` directory for every retained run. The legacy `<sha>-<os>.json` naming is preserved, so reusing an output directory can overwrite earlier reports/logs.

## Strict automated acceptance

```sh
python3 -I tools/qa/summarise.py --strict \
  --summary-out /tmp/gapfill-evidence/strict-summary.json \
  /tmp/gapfill-evidence/full/<sha>-<os>.json
```

The default summary retains its previous behavior: unverified rows are listed but do not cause failure. `--strict` additionally requires complete full-tier automated coverage, clean source/dependencies, exact candidate provenance, successful process results, raw-log hashes and counts matching the report. Old schema-1 reports remain readable normally, but cannot establish strict provenance by themselves. Malformed or duplicate rows fail rather than disappearing from the gate.

An explicitly skipped fast-tier `unit` row can be covered by a full-tier `unit-all` row from another supplied report only when source SHA, OS/architecture, compiler/configuration, dependency revisions and binary hash match. The donor's raw log must be intact and establish at least 100 executed tests with no failures. The original skipped row remains unverified; the summary records the donor evidence/log hashes in `coverage_links`. Separate run directories preserve both reports.

Every other unavailable applicable step blocks strict automated acceptance. Fast-tier-only success is incomplete. Manual requirements remain listed in `manual_rows_open`, and the summary explicitly states that release acceptance is not established. There is no manual-attestation ingestion or signing gate in this module.

## Regression tests

`python3 -m unittest tools/test_qa_matrix.py` exercises real subprocess exits, Python unittest counts/skips, metadata collection, AU identity and strict report reconciliation. Tests include crash/unrelated-error rejection, same-case assertion/cleanup failures, damaged logs, mismatched candidates and malformed evidence. Native DSP, host and validator results require their actual executables and environments.
