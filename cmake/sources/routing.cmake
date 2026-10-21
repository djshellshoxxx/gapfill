# WP10 Routing & system: F-M17 wet-only, F-L12 wet bus, F-L13 sidechain-driven fills, F-L11 MIDI output,
# F-D03 quality modes, F-X06 co-op (docs/dev/12-SPEC-BACKLOG-AND-NEW.md section 8).
list(APPEND GAPFILL_SOURCES
    Source/features/routing/Info.cpp
    Source/features/scdrive/ScDrive.cpp
    Source/features/midiout/MidiOut.cpp
    Source/features/midiout/MidiOutState.cpp
    Source/features/midiout/OptionsRow.cpp
    Source/features/quality/Quality.cpp
    Source/features/coop/Coop.cpp
    Source/gui/slots/RoutingSlot.cpp
)

# F-L12: optional second output bus "Wet" (disabled by default). The processor skips it for AU
# (runtime wrapper check) until auval + Logic pass; -DGAPFILL_WET_BUS=OFF removes it everywhere.
option(GAPFILL_WET_BUS "Declare the optional 'Wet' output bus (F-L12)" ON)
# F-L11: MIDI output changes the plugin's declaration (producesMidi, NEEDS_MIDI_OUTPUT); release decision, default OFF.
option(GAPFILL_MIDI_OUT "Declare MIDI output and emit gap/fill notes (F-L11)" OFF)

if (GAPFILL_WET_BUS)
    add_compile_definitions(GAPFILL_WET_BUS=1)
else()
    add_compile_definitions(GAPFILL_WET_BUS=0)
endif()

if (GAPFILL_MIDI_OUT)
    add_compile_definitions(GAPFILL_MIDI_OUT=1)
    set(GAPFILL_NEEDS_MIDI_OUTPUT TRUE)
else()
    add_compile_definitions(GAPFILL_MIDI_OUT=0)
    set(GAPFILL_NEEDS_MIDI_OUTPUT FALSE)
endif()
