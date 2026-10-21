cmake_minimum_required(VERSION 3.22)

project(GapFill VERSION 0.0.1 LANGUAGES C CXX)
set(GAPFILL_RELEASE_STAGE "beta")

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

# CLAP support (unofficial JUCE extensions). Optional: if the checkout is absent
# the VST3/Standalone builds still work, only the CLAP target is skipped.
set(GAPFILL_CLAP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/clap-juce-extensions")
if (EXISTS "${GAPFILL_CLAP_DIR}/CMakeLists.txt")
    add_subdirectory("${GAPFILL_CLAP_DIR}" EXCLUDE_FROM_ALL)
    set(GAPFILL_HAS_CLAP TRUE)
else()
    set(GAPFILL_HAS_CLAP FALSE)
    message(STATUS "clap-juce-extensions not found - CLAP target disabled")
endif()

# ---------------------------------------------------------------------------
# Shared source list (used by both the plugin and the test runner)
# ---------------------------------------------------------------------------
set(GAPFILL_SOURCES
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
    Source/gui/layout/DisclosureTile.cpp
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
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0
    _USE_MATH_DEFINES=1
    GAPFILL_VERSION="${PROJECT_VERSION} ${GAPFILL_RELEASE_STAGE}"
)

# A cross-compiled build cannot run the VST3 manifest helper on the build
# machine. moduleinfo.json is optional for hosts, so it is skipped there only.
if (CMAKE_CROSSCOMPILING)
    set(GAPFILL_VST3_AUTO_MANIFEST FALSE)
else()
    set(GAPFILL_VST3_AUTO_MANIFEST TRUE)
endif()

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
# Audio Unit is Apple-only; preserve Windows/Linux target lists.
set(GAPFILL_PLUGIN_FORMATS VST3 Standalone)
if (APPLE)
    list(APPEND GAPFILL_PLUGIN_FORMATS AU)
endif()

juce_add_plugin(GapFill
    COMPANY_NAME            "GapFill Audio"
    BUNDLE_ID               "com.gapfillaudio.gapfill"
    PLUGIN_MANUFACTURER_CODE Gfau
    PLUGIN_CODE             Gpfl
    FORMATS                 ${GAPFILL_PLUGIN_FORMATS}
    PRODUCT_NAME            "GapFill"
    DESCRIPTION             "Intelligent gap-aware creative fill processor"
    VST3_CATEGORIES         Fx Delay Modulation
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    # The wrapper must allow keyboard events so the persisted opt-in
    # Keyboard Navigation mode can function. The editor and every interactive
    # child remain non-focusable by default until the user enables that mode.
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST      ${GAPFILL_VST3_AUTO_MANIFEST}
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

# Do NOT redirect the VST3 or CLAP output here. JUCE builds those binaries
# directly into their bundle layout (GapFill.vst3/Contents/x86_64-win/...);
# overriding RUNTIME_OUTPUT_DIRECTORY drops the DLL beside the bundle instead
# of inside it and leaves the bundle an empty shell that a host cannot load.
# Their deterministic home is build/GapFill_artefacts/<Config>/<Format>/.
if (TARGET GapFill_Standalone)
    set_target_properties(GapFill_Standalone PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${GAPFILL_ARTEFACT_DIR}")
endif()

if (GAPFILL_HAS_CLAP)
    clap_juce_extensions_plugin(TARGET GapFill
        CLAP_ID "com.gapfillaudio.gapfill"
        CLAP_FEATURES audio-effect glitch delay granular
        CLAP_MANUAL_URL "https://github.com/djshellshoxxx/gapfill"
        CLAP_SUPPORT_URL "https://github.com/djshellshoxxx/gapfill/issues")
endif()

# ---------------------------------------------------------------------------
# Test runner
# ---------------------------------------------------------------------------
juce_add_console_app(GapFillTests PRODUCT_NAME "GapFillTests")

target_sources(GapFillTests PRIVATE
    ${GAPFILL_SOURCES}
    Tests/TestMain.cpp
    Tests/TestFramework.cpp
    Tests/TestFrameworkFilters.cpp
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
    JucePlugin_WantsMidiInput=1
)

target_include_directories(GapFillTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_link_libraries(GapFillTests PRIVATE ${GAPFILL_MODULES} PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

set_target_properties(GapFillTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GAPFILL_ARTEFACT_DIR}")

if (MSVC)
    target_compile_options(GapFillTests PRIVATE /W4 /fp:precise /bigobj)
endif()
