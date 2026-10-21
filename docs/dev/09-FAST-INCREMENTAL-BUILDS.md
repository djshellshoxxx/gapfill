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
