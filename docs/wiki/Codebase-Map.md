# Codebase Map: where to find what

| I want to... | Look in |
|---|---|
| Host processor, state, buses, latency | `Source/PluginProcessor.*` |
| Parameter IDs and ranges | `Source/Parameters.*` |
| Factory presets | `Source/Presets.*` |
| MIDI learn | `Source/MidiLearn.*` |
| Analysis | `Source/dsp/AnalysisEngine.*`, `Source/dsp/DspUtils.h` |
| Fill scheduling and recipes | `Source/dsp/FillEngine.*` |
| Fill voices | `Source/dsp/FillVoice.*` |
| Spectral smear | `Source/dsp/SpectralSmear.*` |
| Editor, panels, knobs, theme | `Source/PluginEditor.*`, `Source/gui/` |
| Waveform/gap visualiser | `Source/gui/WaveformGapView.*`, `Source/dsp/VisualisationState.h` |
| Help and options | `Source/gui/HelpView.*`, `Source/gui/OptionsView.*` |
| Tests | `Tests/` |
| Build | `CMakeLists.txt`, `.github/workflows/ci.yml`, `publish-win-linux.yml`, `macos-release.yml` |
| Module ownership rules | `docs/architecture/MODULES.md` |
