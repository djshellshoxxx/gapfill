# Opt-in libFuzzer target (clang only): -DGAPFILL_FUZZ=ON. Not part of normal builds or CI
# gates; see docs/qa/FUZZING.md. Instruments every GapFill/JUCE object with ASan+UBSan and
# libFuzzer coverage, and links the harness in Tests/fuzz/FuzzTargets.cpp.
if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "GAPFILL_FUZZ requires clang (libFuzzer)")
endif()

juce_add_console_app(GapFillFuzz PRODUCT_NAME "GapFillFuzz")
target_sources(GapFillFuzz PRIVATE ${GAPFILL_SOURCES} Tests/fuzz/FuzzTargets.cpp)
target_compile_definitions(GapFillFuzz PUBLIC
    ${GAPFILL_DEFS}
    GAPFILL_UNIT_TESTS=1
    JucePlugin_Name="GapFill"
    JucePlugin_IsSynth=0
    JucePlugin_IsMidiEffect=0
    JucePlugin_ProducesMidiOutput=0
    JucePlugin_WantsMidiInput=1
)
target_include_directories(GapFillFuzz PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_link_libraries(GapFillFuzz PRIVATE ${GAPFILL_MODULES} juce::juce_recommended_config_flags)
target_compile_options(GapFillFuzz PRIVATE -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -g)
target_link_options(GapFillFuzz PRIVATE -fsanitize=fuzzer,address,undefined)
set_target_properties(GapFillFuzz PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GAPFILL_ARTEFACT_DIR}")
