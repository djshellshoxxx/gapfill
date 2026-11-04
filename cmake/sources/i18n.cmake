# Data-only catalogs are embedded without adding include flags to legacy/JUCE
# sources. A new locale JSON is sufficient; its filename is the persisted code.
list(APPEND GAPFILL_SOURCES
    Source/features/i18n/Catalog.cpp
    Source/features/i18n/Strings.cpp
    Source/features/i18n/ComponentTranslator.cpp
    Source/features/i18n/Options.cpp
    Source/features/i18n/RecipePresentation.cpp
)
file(GLOB GF_I18N_CATALOGS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/lang/*.json")
set(GF_I18N_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated/i18n")
file(MAKE_DIRECTORY "${GF_I18N_GENERATED}")
set(GF_I18N_HEADER "#pragma once\n#include <cstddef>\nnamespace gapfill::i18n::data {\nstruct Language { const char* code; const unsigned char* bytes; std::size_t size; };\n")
set(GF_I18N_ROWS "")
set(GF_I18N_INDEX 0)
foreach(GF_I18N_FILE IN LISTS GF_I18N_CATALOGS)
    get_filename_component(GF_I18N_CODE "${GF_I18N_FILE}" NAME_WE)
    string(LENGTH "${GF_I18N_CODE}" GF_I18N_CODE_LENGTH)
    if(NOT GF_I18N_CODE MATCHES "^[a-zA-Z][a-zA-Z0-9_-]*$" OR GF_I18N_CODE_LENGTH GREATER 32)
        message(FATAL_ERROR "Invalid language catalog filename")
    endif()
    file(SIZE "${GF_I18N_FILE}" GF_I18N_SIZE)
    if(GF_I18N_SIZE GREATER 1048576)   # mirrors Catalog.h maxCatalogBytes (1 MiB, owner decision 2026-11-04)
        message(FATAL_ERROR "Language catalog exceeds 1 MiB")
    endif()
    file(READ "${GF_I18N_FILE}" GF_I18N_HEX HEX)
    string(REGEX REPLACE "(..)" "0x\\1,\n" GF_I18N_BYTES "${GF_I18N_HEX}")
    string(APPEND GF_I18N_HEADER "inline constexpr unsigned char c${GF_I18N_INDEX}[] = {\n${GF_I18N_BYTES}0};\n")
    string(APPEND GF_I18N_ROWS "{\"${GF_I18N_CODE}\",c${GF_I18N_INDEX},${GF_I18N_SIZE}},\n")
    math(EXPR GF_I18N_INDEX "${GF_I18N_INDEX}+1")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GF_I18N_FILE}")
endforeach()
string(APPEND GF_I18N_HEADER "inline constexpr Language languages[] = {\n${GF_I18N_ROWS}};\n}\n")
file(WRITE "${GF_I18N_GENERATED}/CatalogData.h" "${GF_I18N_HEADER}")
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Source/features/i18n/Strings.cpp"
    PROPERTIES INCLUDE_DIRECTORIES "${GF_I18N_GENERATED}")
