# Developer Guide

## Build

Requires CMake 3.22+ and a C++20 compiler (MSVC 2022 on Windows). JUCE 8.0.15 and optionally
`clap-juce-extensions` are checked out beside the source (not vendored). The README "Building" section has the exact commands;
`docs/DEV-SETUP.md` covers platforms. The build was not run in this docs pass.

## Test

```bash
build/artefacts/Release/GapFillTests.exe            # everything
build/artefacts/Release/GapFillTests.exe Protection # tests whose name matches
```

Plugin validation uses pluginval (README "Testing"). Test sources are in `Tests/`
(`TestDsp.cpp`, `TestPlugin.cpp`, `TestCombinatorial.cpp`, `TestSignals.*`, custom framework in `TestFramework.*`).
Test traceability: `docs/TEST-TRACEABILITY.md`.

## Rules (from `docs/architecture/MODULES.md`)

1. DSP must not depend on GUI classes.
2. Audio callback: no locks, allocations, file I/O or logging; capacity fixed after `prepareToPlay`.
3. Parameter IDs are public compatibility data: append new IDs, never repurpose one.
4. Add every new source and test file explicitly to CMake; no broad globs.
5. Establish behaviour with a focused test before moving code.
