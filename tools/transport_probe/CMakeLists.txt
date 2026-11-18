cmake_minimum_required(VERSION 3.24)
project(GapFillTransportProbe VERSION 1.0.0 LANGUAGES C CXX)
option(GAPFILL_BUILD_TRANSPORT_PROBE "Build the optional timing-only VST3 host diagnostic" OFF)
if(NOT GAPFILL_BUILD_TRANSPORT_PROBE)
    message(STATUS "Transport probe is disabled; enable GAPFILL_BUILD_TRANSPORT_PROBE explicitly")
    return()
endif()
set(JUCE_DIR "" CACHE PATH "Existing JUCE source checkout; no dependency download is performed")
if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set JUCE_DIR to an existing JUCE source checkout")
endif()
find_package(Git REQUIRED)
get_filename_component(GAPFILL_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${GAPFILL_REPO}" rev-parse HEAD
    OUTPUT_VARIABLE PROBE_HEAD OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${GAPFILL_REPO}" status --porcelain
    OUTPUT_VARIABLE PROBE_DIRTY_TEXT OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${JUCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE PROBE_JUCE_HEAD OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${JUCE_DIR}" status --porcelain
    OUTPUT_VARIABLE PROBE_JUCE_DIRTY_TEXT OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(PROBE_DIRTY_TEXT)
    set(PROBE_DIRTY 1)
else()
    set(PROBE_DIRTY 0)
endif()
if(PROBE_JUCE_DIRTY_TEXT)
    set(PROBE_JUCE_DIRTY 1)
else()
    set(PROBE_JUCE_DIRTY 0)
endif()
add_subdirectory("${JUCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE")
juce_add_plugin(GapFillTransportProbe
    COMPANY_NAME "Circuit Drift Labs" BUNDLE_ID "com.circuitdriftlabs.gapfill.transportprobe"
    PLUGIN_MANUFACTURER_CODE CdLb PLUGIN_CODE GtPr PRODUCT_NAME "GapFill Transport Probe"
    FORMATS VST3 VST3_CATEGORIES Fx IS_SYNTH FALSE NEEDS_MIDI_INPUT TRUE PRODUCES_MIDI_OUTPUT TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(GapFillTransportProbe PRIVATE Probe.cpp)
target_compile_features(GapFillTransportProbe PRIVATE cxx_std_20)
target_compile_definitions(GapFillTransportProbe PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    GAPFILL_PROBE_HEAD="${PROBE_HEAD}" GAPFILL_PROBE_DIRTY=${PROBE_DIRTY}
    GAPFILL_PROBE_JUCE_HEAD="${PROBE_JUCE_HEAD}"
    GAPFILL_PROBE_JUCE_DIRTY=${PROBE_JUCE_DIRTY}
    GAPFILL_PROBE_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")
target_link_libraries(GapFillTransportProbe PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
