# Core plugin sources. A feature adds its own cmake/sources/<feature>.cmake and
# never edits this file or CMakeLists.txt.
list(APPEND GAPFILL_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    Source/Presets.cpp
    Source/MidiLearn.cpp
    Source/dsp/AnalysisEngine.cpp
    Source/dsp/FillVoice.cpp
    Source/dsp/FillEngine.cpp
    Source/dsp/SpectralSmear.cpp
    Source/gui/LookAndFeel.cpp
    Source/gui/Knob.cpp
    Source/gui/WaveformGapView.cpp
    Source/gui/Panels.cpp
    Source/gui/HelpView.cpp
    Source/gui/OptionsView.cpp
)
