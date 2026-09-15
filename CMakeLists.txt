cmake_minimum_required(VERSION 3.22)

project(GapFill VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    # Static CRT keeps the VST3 free of runtime redistributable dependencies.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Deterministic output location inside the build tree.
set(GAPFILL_ARTEFACT_DIR "${CMAKE_BINARY_DIR}/artefacts")

add_subdirectory(JUCE)

# ---------------------------------------------------------------------------
# Shared source list (used by both the plugin and the test runner)
# ---------------------------------------------------------------------------
set(GAPFILL_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    Source/Presets.cpp
    Source/dsp/AnalysisEngine.cpp
    Source/dsp/FillVoice.cpp
    Source/dsp/FillEngine.cpp
    Source/dsp/SpectralSmear.cpp
    Source/gui/LookAndFeel.cpp
    Source/gui/Knob.cpp
    Source/gui/WaveformGapView.cpp
    Source/gui/Panels.cpp
    Source/gui/HelpView.cpp
)

set(GAPFILL_MODULES
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
)

set(GAPFILL_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0
    _USE_MATH_DEFINES=1
)

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
juce_add_plugin(GapFill
    COMPANY_NAME            "GapFill Audio"
    BUNDLE_ID               "com.gapfillaudio.gapfill"
    PLUGIN_MANUFACTURER_CODE Gfau
    PLUGIN_CODE             Gpfl
    FORMATS                 VST3 Standalone
    PRODUCT_NAME            "GapFill"
    DESCRIPTION             "Intelligent gap-aware creative fill processor"
    VST3_CATEGORIES         Fx Delay Modulation
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    ICON_BIG                "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    ICON_SMALL              "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon_small.png"
)

target_sources(GapFill PRIVATE ${GAPFILL_SOURCES})
target_compile_definitions(GapFill PUBLIC ${GAPFILL_DEFS})
target_include_directories(GapFill PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_link_libraries(GapFill PRIVATE ${GAPFILL_MODULES} PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

if (MSVC)
    target_compile_options(GapFill PRIVATE /W4 /fp:precise)
else()
    target_compile_options(GapFill PRIVATE -Wall -Wextra)
endif()

foreach(fmt VST3 Standalone)
    if (TARGET GapFill_${fmt})
        set_target_properties(GapFill_${fmt} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${GAPFILL_ARTEFACT_DIR}"
            LIBRARY_OUTPUT_DIRECTORY "${GAPFILL_ARTEFACT_DIR}")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Test runner
# ---------------------------------------------------------------------------
juce_add_console_app(GapFillTests PRODUCT_NAME "GapFillTests")

target_sources(GapFillTests PRIVATE
    ${GAPFILL_SOURCES}
    Tests/TestMain.cpp
    Tests/TestFramework.cpp
    Tests/TestSignals.cpp
    Tests/TestDsp.cpp
    Tests/TestPlugin.cpp
    Tests/TestCombinatorial.cpp
)

target_compile_definitions(GapFillTests PUBLIC
    ${GAPFILL_DEFS}
    GAPFILL_UNIT_TESTS=1
    JucePlugin_Name="GapFill"
    JucePlugin_IsSynth=0
    JucePlugin_IsMidiEffect=0
    JucePlugin_ProducesMidiOutput=0
    JucePlugin_WantsMidiInput=0
)

target_include_directories(GapFillTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_link_libraries(GapFillTests PRIVATE ${GAPFILL_MODULES} PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

set_target_properties(GapFillTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GAPFILL_ARTEFACT_DIR}")

if (MSVC)
    target_compile_options(GapFillTests PRIVATE /W4 /fp:precise /bigobj)
endif()
