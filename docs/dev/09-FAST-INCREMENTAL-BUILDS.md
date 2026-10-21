# Focused incremental developer builds

Use `tools/dev/bootstrap.sh` once to fetch pinned JUCE and CLAP extension dependencies and configure `build-dev`. Thereafter avoid recompiling every format during a feature's edit/test loop:

```bash
bash tools/dev/build-target.sh build-dev tests Protection
bash tools/dev/build-target.sh build-dev vst3
bash tools/dev/build-target.sh build-dev clap
bash tools/dev/build-target.sh build-dev standalone
bash tools/dev/build-target.sh build-dev plugin
bash tools/dev/build-target.sh build-dev all
```

The default group is `tests`, with `-Combinatorial` as the test filter. The `plugin` group builds VST3 and Standalone without the test runner; `all` includes tests and CLAP if its configured target exists. A specifically requested CLAP build fails if the target was not configured. Set `JOBS=1` on memory-constrained hosts or `JOBS=4` if capacity permits. For Visual Studio/Xcode multi-config builds, `CONFIG=Release` is the default. Incremental CMake builds reuse existing object files; changed shared source files may still require recompilation in more than one target.

This script does **not** replace `tools/dev/check.sh` or CI. Before merging, run the repository preflight, full unit/combinatorial suite and all supported-platform plugin validators. Target-specific builds provide fast feedback; only the full acceptance pipeline can establish release readiness.

Known limitation: the shell script requires a POSIX-style Bash environment. On Windows, Git Bash is suitable for invoking it; the native Windows CMake build still requires the configured MSVC toolchain.

---

# 09 - Fast, incremental CI and beta releases

Goal: every part of the build is an independent job, no test is run twice
without a reason, and unchanged work is cached. Gates are unchanged: nothing
below weakens a validator (see `07-QA-AND-RELEASE.md` and `docs/VALIDATION.md`).

## 1. Job graph (`.github/workflows/ci.yml`)

```
changes ──┐                                   ┌─> build-linux-x64   (VST3 CLAP Standalone; pluginval, clap-validator, smoke)
preflight ┴─> unit-tests (Linux, FULL suite) ─┼─> build-windows-x64 (VST3 CLAP Standalone; smoke tests, pluginval, clap-validator, smoke)
                                              ├─> build-macos arm64 / x86_64 (VST3 AU CLAP Standalone; smoke tests, pluginval, auval, clap-validator, smoke)
                                              └─> ci-gate (the one check to require in branch protection)
```

| Job | Does | Does NOT |
|---|---|---|
| `changes` | `tools/ci_detect_native_changes.sh`: docs-only diff (`docs/`, `*.md`, `LICENSE`, `.gitignore`) => `native=false` | - |
| `preflight` | `validate_repo.py`, `test_validate_repo.py`, `test_package_release.sh` (Win/Linux/mac fixtures), `test_ci_helpers.sh`, workflow YAML parse, `bash -n` on every script | build anything |
| `unit-tests` | builds `GapFillTests` only; runs the **whole** suite (DSP, golden, state fixtures, Combinatorial) once; uploads `unit-test-results` | build plugin formats |
| `build-*` | build the shipping formats for one platform, validate those exact binaries, upload a named artifact | run the full suite |

Smoke subset on Windows/macOS: `SMOKE_FILTER=Plugin` (`GapFillTests Plugin`;
substring match on test group or name). It covers processor lifecycle,
sample rates, block sizes and layouts on the platform's own compiler/runtime
without repeating the DSP/golden/Combinatorial work, which is platform-neutral
and already ran on Linux. Change the one `env:` value in `ci.yml` to widen it.

Artifacts (all `retention-days: 90`, uploaded only when every validation step passed):
`gapfill-linux-x64`, `gapfill-windows-x64` (includes `icon.ico` for the installer),
`gapfill-macos-arm64`, `gapfill-macos-x86_64` (a `.tar` so executable bits and
symlinks survive). Also `unit-test-results` (30 d) and Windows pluginval logs (14 d).

Docs-only changes: `preflight` still runs (it validates docs), native jobs are
skipped, `ci-gate` is green. A workflow-level `paths-ignore` is deliberately not
used: it would leave required checks pending forever. `workflow_dispatch` always
builds natively, which is also how you get artifacts for a docs-only commit you
want to release.

Concurrency: PR runs are keyed by PR number and cancel superseded runs. Pushes
and manual runs are keyed by SHA and never cancelled, so a release candidate's
artifacts cannot be lost to a later push.

## 2. Caches

| What | Where | Key |
|---|---|---|
| JUCE 8.0.15 + clap-juce-extensions checkouts | all jobs | `deps-<os>-hashFiles(tools/ci_fetch_deps.sh)`; the pins live in that script, so bumping a pin busts the cache |
| ccache | `unit-tests`, `build-linux-x64`, `build-macos-*` (Ninja generator, `CMAKE_*_COMPILER_LAUNCHER=ccache`) | `ccache-<job>-<sha>` with prefix restore (500 MB, compressed) |
| pluginval download | all native jobs | `pluginval-<os>-<YYYY-MM>` |
| clap-validator binary (`~/.cargo/bin`) | all native jobs | `clap-validator-<os>-<arch>-<YYYY-MM>` |

The validators track upstream "latest"; the month in the key refreshes them
about monthly instead of on every run. To force a refresh, change the key prefix.

Not cached, on purpose: Windows compiles (the Visual Studio generator ignores
compiler launchers; Ninja + MSVC + ccache was not judged reliable enough to gate
a release on - revisit with sccache if Windows becomes the bottleneck), and the
Rust toolchain itself (preinstalled on runners).

## 3. Cutting a beta (`.github/workflows/release-beta.yml`)

1. Bump `project(GapFill VERSION ...)` / `GAPFILL_RELEASE_STAGE` in `CMakeLists.txt` if needed and merge to `master`.
2. Make sure CI is green on the exact commit you will release. If that commit was docs-only (native jobs skipped), run the **CI** workflow manually on it first.
3. Actions > **Release beta** > Run workflow on `master`; inputs: `tag` (e.g. `v0.0.2-beta1`, must equal `v<CMake version>-<stage><N>`) and optionally `source_run_id`.
4. `verify` refuses unless: tag format/CMake match, tag and release do not exist, the CI run concluded `success`, its head SHA equals the dispatched SHA, and all four platform artifacts plus `unit-test-results` are unexpired.
5. `package-desktop` (Linux runner, NSIS) rebuilds build-tree layouts from the artifacts (`tools/release_stage_artifacts.sh`, which also checks PE/ELF x86-64 headers) and runs `packaging/package_release.sh`: Windows installer, portable/VST3/CLAP zips, Linux portable/VST3/CLAP zips. `package-macos` runs `packaging/package_macos.sh` per arch: lipo and `codesign --verify` re-checks, then `GapFill-<ver>-macOS-<arch>.zip` (VST3, AU, CLAP, Standalone, INSTALL.txt).
6. `publish` writes one `GapFill-<ver>-SHA256SUMS.txt`, creates the GitHub release with `prerelease=true`, `draft=false`, `--latest=false` targeting the verified SHA, then downloads it back and re-checks every checksum.

Nothing is rebuilt or re-tested in the release workflow; only verified bytes are repackaged.
The old one-shot, pinned-SHA workflows (`macos-release.yml`, `publish-win-linux.yml`)
were removed because this workflow replaces them; the `v0.0.1-beta1` release and its assets are untouched.

## 4. Known limits (unverified until the first real run)

* The workflows cannot be executed locally; they were checked with `yaml.safe_load` and actionlint, and the packaging/helper scripts have fixture tests. The first CI and release runs are the real verification.
* macOS now builds with Ninja instead of the Xcode generator (needed for ccache). Output paths are resolved with `find`, but this is a behaviour change to confirm on the first run.
* The NSIS installer is compiled at release time from the verified Windows binaries; CI does not execute the installer.
* macOS packages are ad-hoc signed only (not notarized); Windows binaries are not Authenticode-signed.
