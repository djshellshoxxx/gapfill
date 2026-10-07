# GapFill

> **Free/open Circuit Drift Labs plugin.** GapFill is maintained as a useful experimental creative effect and community tool rather than a premium paid product.


**An intelligent creative effect that finds the spaces between musical events and turns them into controlled ear candy — while protecting the original hits.**

GapFill is not a delay, a glitcher, a gate or a random multi-FX. It listens to the
audio, decides where the *usable space between events* is, and generates material
only there. The material is cut from your own recent audio, so what lands in the
gaps still belongs to the track.

```
KICK ——[granular reverse]—— SNARE ——[stutter]—— KICK ——[pitching delay]—— SNARE
```

---

## Download (v0.0.1 beta)

Grab the latest build from the GitHub **Releases** page:

| Asset | What it is |
|---|---|
| `GapFill-0.0.1-beta-Windows-x64-Setup.exe` | Installer: VST3, CLAP and the standalone app (each optional) |
| `GapFill-0.0.1-beta-Windows-x64-Portable.zip` | No-install: standalone `GapFill.exe` plus both plugin formats |
| `GapFill-0.0.1-beta-Windows-x64-VST3.zip` | `GapFill.vst3` only - copy to `C:\Program Files\Common Files\VST3` |
| `GapFill-0.0.1-beta-Windows-x64-CLAP.zip` | `GapFill.clap` only - copy to `C:\Program Files\Common Files\CLAP` |
| `GapFill-0.0.1-beta-Linux-x64-*.zip` | The same three formats for Linux (`~/.vst3`, `~/.clap`) |

## What's new in 0.0.1 beta

- **SURPRISE** (top bar) - one press jumps to one of eight curated, always-coherent creative
  profiles (Drum Edits, Dub Echoes, Glitch Storm, Tape Breakdown...). Detector, protection and
  mix are never touched, and the same profile never comes up twice in a row.
- **UNDO** (top bar) - steps back through the last 32 whole-sound changes: Random, Random All,
  Surprise, Reset, preset changes and file loads.
- **CAPTURE** (Creative panel) - latches the fill source. Fills keep being built from the audio
  heard when you pressed it while detection, protection and the dry signal follow the live track.
  Automatable and MIDI-learnable for performance.
- **Tape Stop** fill algorithm - the hit that opened the gap brakes to a halt, losing speed,
  pitch and top end. Available in AUTO (with its own weight) or as a forced Fill Type.
- Fixes: saved sessions and `.gapfill` presets were rejected on load by an over-strict
  validator; a compile error in preset saving; unnamed weight sliders for screen readers;
  the CI `clap-validator` install step.

## Building

Requirements: CMake 3.22+, a C++20 compiler (MSVC 2022 on Windows), and the JUCE
checkout in `JUCE/` (JUCE 8.0.15 is what this was developed against).

Both dependencies are checked out beside the source rather than vendored, and
both are gitignored:

```bash
git clone --depth 1 --branch 8.0.15 https://github.com/juce-framework/JUCE JUCE
git clone --recurse-submodules --depth 1 \
    https://github.com/free-audio/clap-juce-extensions.git clap-juce-extensions
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

`clap-juce-extensions` is optional. If the directory is absent CMake prints a
note and simply skips the CLAP target; the VST3 and Standalone still build.

Artefacts land in a deterministic location inside the build tree:

| Target      | Path                                                          |
|-------------|---------------------------------------------------------------|
| VST3        | `build/GapFill_artefacts/<Config>/VST3/GapFill.vst3`          |
| CLAP        | `build/GapFill_artefacts/<Config>/CLAP/GapFill.clap`          |
| Standalone  | `build/artefacts/<Config>/GapFill.exe`                        |
| Test runner | `build/artefacts/<Config>/GapFillTests.exe`                   |

The VST3 and CLAP are bundles, so the binary sits at
`GapFill.vst3/Contents/x86_64-win/GapFill.vst3` inside the directory of that
name. Do not set `RUNTIME_OUTPUT_DIRECTORY` on those two targets: JUCE links
them straight into the bundle layout, and redirecting the output drops the
binary beside the bundle and leaves an empty shell no host can load.

Nothing is copied into a system plugin folder automatically, so a read-only
system directory can never fail the build.

### Linux

The same CMake build produces a Linux VST3, CLAP and standalone. Build
dependencies (Debian/Ubuntu; `JUCE_WEB_BROWSER=0` and `JUCE_USE_CURL=0` mean
neither WebKit nor curl is required):

```bash
sudo apt install build-essential cmake ninja-build \
    libasound2-dev libfreetype-dev libfontconfig1-dev \
    libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
    libxinerama-dev libxrandr-dev libxrender-dev
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux
```

Linux artefacts follow the same layout, with `x86_64-linux` as the bundle
architecture directory and no extension on the standalone binary.

### Release packaging

`packaging/package_release.sh <windows-build> [<linux-build>]` turns finished
builds into the release assets in `dist/`: the NSIS installer
(`packaging/GapFill.nsi`), portable/VST3/CLAP zips and a SHA-256 list. The
0.0.1 beta Windows binaries were cross-compiled from Linux with clang-cl 22 and
lld-link against the MSVC static runtime (`CMAKE_CROSSCOMPILING` skips only the
VST3 `moduleinfo.json` helper, which cannot run on the build machine).

On a machine with little free RAM, cap the compiler as well as MSBuild — JUCE's
recommended flags add `/MP`, which starts one `cl.exe` per core:

```bash
cmake --build build --config Release -- -m:1 -p:CL_MPCount=1
```

## Testing

```bash
build/artefacts/Release/GapFillTests.exe            # everything
build/artefacts/Release/GapFillTests.exe Protection # only tests matching "Protection"
```

The suite covers the DSP primitives (ring buffer wraparound, crossover energy
preservation, deterministic RNG, spectral overlap-add), the detector (synthetic
transient patterns, gap confidence, per-band availability, sidechain detection),
every fill algorithm, the protection system, latency/lookahead alignment, state
serialisation, and a large combinatorial sweep of parameter settings.

Plugin validation (scan, creation, editor lifecycle, bus layouts, state, audio
processing) is run with [pluginval](https://github.com/Tracktion/pluginval):

```bash
tools/pluginval/pluginval.exe --strictness-level 8 \
    --validate build/GapFill_artefacts/Release/VST3/GapFill.vst3
```

---

## Architecture

```
MAIN INPUT ──┬────────────────────────────────► [lookahead delay] ──► DRY
             │
             ├──► SourceHistory (6 s circular capture)
             │
             └──► AnalysisEngine
                    ├─ LaneDetector  (fast/slow envelopes, adaptive floor,
                    │                 musical reference level, onset score)
                    ├─ FluxAnalyser  (512-pt STFT spectral flux)
                    ├─ BandSplitter  (Linkwitz-Riley 3-way, per-band lanes)
                    └─ AnalysisFrame ring + event queues  ──► lookahead

SIDECHAIN ───────► AnalysisEngine (timing only)

                        FillEngine
                          ├─ gap state machine + weighted algorithm selector
                          ├─ 4 x FillVoice (14 algorithms + stacked modifiers)
                          ├─ SpectralSmear (shared phase-vocoder)
                          └─ protection: transient / next-event / sidechain duck
                                   │
                                   ▼
                        Low Protect ─► Center Protect ─► Width ─► WET

                              DRY + WET ──► Output trim
```

Key points:

* **Analysis runs ahead of output.** The dry path is delayed by the Lookahead
  setting and that exact figure is reported to the host, so the scheduler can
  ask "when is the next important event?" and still be sample-aligned.
* **Gap confidence is continuous**, not a gate. It drives whether a fill
  happens, how loud, how long and how complex it may be.
* **Randomness is deterministic.** The decision for a gap is derived from the
  user Seed and the musical (PPQ) position, so a project plays back identically
  every time and reloads the same way.
* **Real-time safety.** Everything is preallocated in `prepareToPlay`;
  `processBlock` performs no allocation, locking, file or console I/O, and all
  generator output passes through a finite/bounded sanitiser.

## Source layout

```
Source/
  PluginProcessor.*       host integration, buses, latency, mix, protection bus
  PluginEditor.*          top bar, tabs, A/B, presets, FILE menu, overlays
  Parameters.*            stable parameter IDs (X-macro) + cached atomics
  Presets.*               17 factory presets, reset, randomise
  MidiLearn.*             CC learn and mapping, lock-free audio -> UI
  dsp/
    DspUtils.h            SVF, envelopes, RNG, SourceHistory, EventQueue
    AnalysisEngine.*      detection lanes, flux, band splitting, frame ring
    FillVoice.*           the 14 fill algorithms + modifier chain
    FillEngine.*          scheduler, voice pool, protection envelopes
    SpectralSmear.*       streaming phase-vocoder smear
    VisualisationState.h  lock-free audio -> GUI transfer
  gui/                    look and feel, controls, visualiser, panels,
                          help and options overlays
Tests/                    unit, integration and combinatorial tests
```

## Licence

JUCE is used under its own licence terms. This plugin's own source is provided
as-is.


## Required shared plug-in standard

This project follows the [Circuit Drift Labs Shared Audio Plugin Standard](docs/standards/CDL_PLUGIN_BASELINE.md). It is required for the plug-in target; standalone-only requirements apply only when a standalone target is included. The product-specific specification supplements the shared standard and records the applicable profiles, compliance status, and any exceptions.


## Privacy and network behavior

GapFill does not send audio, presets, project data, diagnostics, analytics, or telemetry over the network. The plugin has no licensing server or background update checker. Network access is not part of audio processing. Links shown in documentation or Help are informational and are opened only through normal user interaction outside the audio thread.

## Shared plugin standard

GapFill follows the required Circuit Drift Labs shared plugin baseline in [`docs/standards/CDL_PLUGIN_BASELINE.md`](docs/standards/CDL_PLUGIN_BASELINE.md). Product-specific compliance and exceptions are tracked in [`docs/COMPLIANCE.md`](docs/COMPLIANCE.md).


## Validation

Release validation is defined in [`docs/VALIDATION.md`](docs/VALIDATION.md). CI builds VST3, CLAP and Standalone on Linux and Windows, runs the JUCE regression suite, validates VST3 with pluginval at strictness 10, validates CLAP with clap-validator, and smoke-launches the standalone application.
