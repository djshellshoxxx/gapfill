# Architecture and Design Rationale

Sources: `docs/architecture/MODULES.md`, README "Architecture". *(inferred)* marks conclusions from the layout.

- **Processor as host boundary.** `PluginProcessor` coordinates analysis, fill scheduling, voice rendering and UI data. Analysis, fills and UI talk through bounded state and events.
- **Analysis feeds fills from the input's own history.** A bounded recent-input buffer is the material source, so output stays in the track's own sound. *(README)*
- **Deterministic decisions.** Fill eligibility, choices and timing must be reproducible and independent of host block size (`FillEngine`).
- **Protection of transients.** Fill voices must preserve important input transients (`FillVoice`).
- **Stable parameter IDs.** IDs are compatibility data for hosts and presets.
- **UI may drop frames, never sound.** Visualisation reads bounded snapshots; the editor can be destroyed and recreated while audio runs.
- **Explicit build list.** Sources are added to CMake one by one so builds do not depend on directory contents.
- **Optional CLAP.** If `clap-juce-extensions` is absent, CMake skips CLAP and still builds VST3 and Standalone.
