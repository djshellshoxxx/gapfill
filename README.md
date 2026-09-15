# GapFill

**An intelligent creative effect that finds the spaces between musical events and turns them into controlled ear candy — while protecting the original hits.**

GapFill is not a delay, a glitcher, a gate or a random multi-FX. It listens to the
audio, decides where the *usable space between events* is, and generates material
only there. The material is cut from your own recent audio, so what lands in the
gaps still belongs to the track.

```
KICK ——[granular reverse]—— SNARE ——[stutter]—— KICK ——[pitching delay]—— SNARE
```

---

## Building

Requirements: CMake 3.22+, a C++20 compiler (MSVC 2022 on Windows), and the JUCE
checkout in `JUCE/` (JUCE 8.0.15 is what this was developed against).

```bash
git clone --depth 1 --branch 8.0.15 https://github.com/juce-framework/JUCE JUCE
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Artefacts land in a deterministic location inside the build tree:

| Target      | Path                                                     |
|-------------|----------------------------------------------------------|
| VST3        | `build/GapFill_artefacts/Release/VST3/GapFill.vst3`      |
| Standalone  | `build/GapFill_artefacts/Release/Standalone/GapFill.exe` |
| Test runner | `build/artefacts/GapFillTests.exe`                       |

Nothing is copied into a system plugin folder automatically, so a read-only
system directory can never fail the build.

## Testing

```bash
build/artefacts/GapFillTests.exe            # everything
build/artefacts/GapFillTests.exe Protection # only tests matching "Protection"
```

The suite covers the DSP primitives (ring buffer wraparound, crossover energy
preservation, deterministic RNG, spectral overlap-add), the detector (synthetic
transient patterns, gap confidence, per-band availability, sidechain detection),
every fill algorithm, the protection system, latency/lookahead alignment, state
serialisation, and a large combinatorial sweep of parameter settings.

Plugin validation (scan, creation, editor lifecycle, bus layouts, state, audio
processing) is run with [pluginval](https://github.com/Tracktion/pluginval):

```bash
tools/pluginval/pluginval.exe --strictness-level 8 --validate build/GapFill_artefacts/Release/VST3/GapFill.vst3
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
  PluginEditor.*          top bar, tabs, A/B, presets, help overlay
  Parameters.*            stable parameter IDs (X-macro) + cached atomics
  Presets.*               17 factory presets, reset, randomise
  dsp/
    DspUtils.h            SVF, envelopes, RNG, SourceHistory, EventQueue
    AnalysisEngine.*      detection lanes, flux, band splitting, frame ring
    FillVoice.*           the 14 fill algorithms + modifier chain
    FillEngine.*          scheduler, voice pool, protection envelopes
    SpectralSmear.*       streaming phase-vocoder smear
    VisualisationState.h  lock-free audio -> GUI transfer
  gui/                    look and feel, controls, visualiser, panels, help
Tests/                    unit, integration and combinatorial tests
```

## Licence

JUCE is used under its own licence terms. This plugin's own source is provided
as-is.
