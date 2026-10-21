# Developer setup

This page describes the supported local feedback loop. CI and docs/VALIDATION.md remain the authoritative merge and release gates.

## Toolchain

Use CMake 3.22 or newer and a C++20-capable compiler supported by the platform. The repository pins JUCE and the CLAP extension in its build configuration; use the versions declared there instead of substituting a local copy. Windows plugin builds use the configured MSVC toolchain. The helper scripts require Bash; Git Bash can invoke them on Windows while CMake still uses MSVC.

Install the platform build tools before running the bootstrap. Keep dependency and build directories outside tracked source files where possible.

## First configuration

From the repository root:

1. Run tools/dev/bootstrap.sh with the desired build directory, normally build-dev. This fetches the pinned dependencies and configures CMake.
2. Run tools/dev/check.sh build-dev for preflight and the local test subset.
3. Build the target needed for the current change with tools/dev/build-target.sh build-dev tests, vst3, clap, standalone, plugin, or all. Use its filter argument for a focused test selection.
4. Use JOBS=1 when memory is constrained. Multi-config generators default to Release; set CONFIG when another configuration is required.

The focused target script shortens the edit/test loop. It does not replace a full test run, platform validators, or host validation. Before merge, follow docs/dev/07-QA-AND-RELEASE.md and docs/VALIDATION.md.

## Clean versus incremental builds

Reuse an existing configured build directory for normal edits. Create a new directory when changing generator, compiler, architecture, SDK, or significant CMake options. Do not remove downloaded dependencies to resolve an unrelated compile error. Capture the full configure/build log and tool versions when a clean build differs from an incremental one.

Build outputs are platform and generator specific. Do not commit build folders, plugin binaries, generated user state, or private DAW projects.

## Platform notes

| Platform | Notes |
|---|---|
| Windows | Use MSVC and the Windows SDK expected by CI. Bash scripts may run from Git Bash; ensure CMake resolves the native MSVC environment. |
| macOS | Install the Xcode command-line tools and use the configured Apple SDK. AU validation and signing requirements are covered by release documentation. |
| Linux | Install the compiler, linker, and system libraries required by JUCE and the selected plugin formats. CI is the reference for missing package names. |

## When setup fails

Run the repository preflight first and preserve its output. For dependency, compiler, validator, or test failures, use docs/TROUBLESHOOTING.md. Check docs/TEST-TRACEABILITY.md before marking a feature validated.