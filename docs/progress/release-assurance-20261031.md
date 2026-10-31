# Q-01 / E-6 release assurance, 2026-10-31

Status: implementation and local tool checks in progress. Native builds,
scheduled executions, real signing and release acceptance are **unverified**.
This work does not publish, tag, close issues or modify existing public assets.

## Scope and contracts

- `tools/dev/build-target.sh`: explicit `au` and auto-detected AU in `all`.
- `tools/qa/nightly_run.py`: executes a real test selection; rejects zero tests,
  reported failures and nonzero exits. Records exact command/filter, counts,
  exit/signal, log, binary SHA-256, observed full source SHA, dirty state,
  dependencies, platform/architecture, CMake flags/config and whether the
  stored performance baseline was enforced. A pass is automated test evidence.
- `Tests/features/TestSoak.cpp`: one continuously living main processor plus a
  cooperative partner; seeded parameter/preset/state/transport churn, scene
  requests, capture/recapture/release, RecordTap start/cancel, wet-export
  requests/cancellation/results. Both callbacks run serially; concurrent
  registry/editor behavior is covered by separate threading tests. Output is
  finite and bounded under fixed safe gains; measured callbacks use AllocGuard.
  Panic/stop plus 30 seconds of silent processing must clear both engines.
  Summary counters distinguish actions requested from results observed.
- `tools/qa/soak_run.py`: starts exactly one test process, observes the
  processor's post-warmup marker and actual measured duration, samples that
  process's RSS, and retains raw samples. Compares median initial/final samples
  against 1 percent growth; reports the peak separately. Missing measurements
  are unverified, never pass. Process restarts cannot accumulate soak duration.
- `nightly.yml`: separate ASan+UBSan full-suite, targeted TSan, Release
  performance/short continuous soak and four-platform native full-QA jobs.
  TSan has no broad JUCE/plugin suppression. A known external-library report
  remains a failure until supported by a reviewed narrow suppression/fix.
- `nightly_native.py`: pins pluginval 1.0.4 and clap-validator 0.4.1 commit
  `152b9823e992d782c5c1fd33bca0295478b919aa`; retains tool fingerprints. Uses
  the Q-01 full matrix and strict automated summary. macOS ad-hoc signatures
  are validation preparation, not Developer ID/notarization evidence. Existing
  user AU installs are preserved; validation needs a clean account.

The existing PR pipeline retains its full Linux suite and validators. Added
tool regressions in preflight, plus an explicitly counted native Soak smoke
selection on Windows and both macOS architectures. No release gate was removed.

All new nightly build/evidence/tool paths are under `RUNNER_TEMP`, outside the
source checkout; otherwise strict clean-source provenance would be invalidated.

## Local verification

- Test-first helper failures were observed before implementation.
- Original Python tool tests: 49 passed on the reviewed integration baseline.
- Release helper tests: 19 passed. They cover real subprocess exit/count behavior, malformed
  soak reports, measured duration, missing RSS, the 1 percent criterion,
  missing identities/validators, checksum tampering, unsafe ZIP paths/symlinks
  and actual detached-signature verification with a **temporary synthetic** key.
- GnuPG needs local Unix sockets. The managed sandbox initially denied its
  agent socket; the isolated fixture test passed with the supported additional
  network permission. No production signing identity was used.
- Python byte compilation, workflow YAML parsing, repository preflight,
  completeness audit (0 failures / 8 pre-existing exemptions), diff whitespace
  checks passed during the slices. Record the final aggregate counts below.
- No C++ build or actual Soak run has completed in this worktree yet. The
  coordinator owns toolchain setup and the first integrated native build.

## Continuous soak commands

Run the same binary/process continuously on a persistent local/self-hosted
machine; the short scheduled job cannot satisfy 8-hour or 7-day acceptance:

```bash
xvfb-run -a python3 tools/qa/soak_run.py \
  --binary /absolute/build/artefacts/GapFillTests --build-dir /absolute/build \
  --seconds 28800 --warmup 60 --seed 20261031 --out /absolute/evidence/soak-8h
```

For seven days use `--seconds 604800` in a persistent session with retained
evidence and no process restart. Elapsed time and RSS must come from the report;
these commands are not evidence that either duration has run. Audio-device
xruns, musical usefulness, hardware, actual concurrent host instances and
listening remain separate manual acceptance rows.

## Signing helper and workflow hook contract

`packaging/sign_release.py` consumes already provisioned identities only:

| Operation | Provisioned selector | Native tools |
|---|---|---|
| `windows --input <binary>... --out <report>` | `GAPFILL_WINDOWS_CERT_SHA1` | Windows certificate store, signtool |
| `macos --input <bundle>... --out <report>` | `GAPFILL_APPLE_SIGN_IDENTITY` | Developer ID Application, codesign |
| `macos-installer --input <stage> --version X.Y.Z --arch <arm64/x86_64> --package <pkg> --out <report>` | `GAPFILL_APPLE_INSTALLER_IDENTITY` | pkgbuild/productbuild/pkgutil/codesign/lipo |
| `notarize --input <signed-archive> --staple <app/pkg> --out <report>` | `GAPFILL_NOTARY_PROFILE` | existing notarytool keychain profile, stapler |
| `checksums --directory <final-assets> --out <report>` | `GAPFILL_RELEASE_GPG_KEY` | provisioned GnuPG signing key/agent |

Missing identities/tools return **2, unverified**. Actual tool failures return
1 with diagnostics; no private key/password/token is accepted in command args.
The checksum operation prepares an unsigned manifest when its identity is
missing, but signed-release acceptance remains unverified.

Before changing `release-beta.yml`, review these required hook points:

1. Sign staged Windows DLL/CLAP/standalone files on Windows **before** archives.
   Sign the NSIS installer after packaging, also on Windows. The existing
   Linux package job cannot consume a Windows certificate-store identity.
2. Sign macOS staged bundles; optionally build/sign the system installer.
   Notarize signed archives/pkg, staple supported app/pkg artifacts and then
   repackage the final distribution bytes. Retain Apple submission IDs/results.
3. Generate final checksums only after all signing/stapling/repackaging. Create
   the detached signature and retain the expected trusted public fingerprint.
   A public key downloaded beside an artifact does not establish its trust.
4. Download final assets into a clean directory. Validate every archive on its
   native OS with the helper below; install/upgrade/uninstall and DAWs still
   need recorded acceptance. Publish only after separate release authorization.

Existing `release-beta.yml` is intentionally unchanged pending this review.

## Downloaded package validation

`packaging/verify_release.py checksums --manifest <downloaded-manifest>` checks
every named asset. `extract` bounds and validates the entire ZIP before writing.
`validate --archive <zip> --manifest <manifest> --platform <linux/windows/macos>
--formats <formats...> --out <fresh-directory>` checks architecture, the AGPL
notice, native validators and standalone smoke on the extracted package.
Pass `--pluginval`, `--clap-validator` as applicable, and use `--arch arm64` for
Apple Silicon. Run under Xvfb on a headless Linux host.

Windows portable ZIP contains VST3/CLAP/standalone; its single-format archives
need `--formats vst3` or `--formats clap`. **Linux portable contains standalone
only**, so use `--formats standalone`; Linux VST3/CLAP archives are separate.
macOS archives contain VST3/CLAP/AU/standalone. Fresh output directories avoid
confusing artifacts from different archives/candidates.

`--require-signing --signature <manifest.asc> --trusted-keyring <trusted-public-keyring>`
adds trusted detached-signature, native code-signature and macOS ticket/
Gatekeeper checks. Never trust the bundled key without verifying its fingerprint.
Missing native tools/validators stay unverified. Build/source provenance must
also be retained from the release build attestation; ZIP contents alone do not
prove a source commit.

## Still required for release acceptance

- Fresh reviewed integrated native builds and all applicable tests, sanitizer
  results and packaged validators on Windows/Linux/macOS.
- Actual 8-hour and seven-day continuous soak results; seven consecutive green
  nightly days per the RC finalization criterion, including recent release QA.
- Provisioned signing identities, real Developer ID/notarization/Authenticode,
  signed final checksum verification and reviewed release-workflow wiring.
- Installer component/upgrade/uninstall/retention/notice checks, host/hardware/
  screen-reader/listening evidence and zero open S1/S2 release blockers.
- Final reference-machine performance enforcement decision; scheduled shared
  runner measurements are explicitly advisory against the stored baseline.
