# E-1 Fill Audition, U-3 MIDI note trigger (docs/dev/03-SPEC-ESSENTIALS.md, 05-SPEC-USABILITY-VALUE.md).
list(APPEND GAPFILL_SOURCES
    Source/features/audition/PreviewChannel.cpp
    Source/features/audition/AuditionEngine.cpp
    Source/features/audition/Audition.cpp
)
list(APPEND GAPFILL_SOURCES
    Source/features/audition/MidiTrigger.cpp
    Source/features/recipes/RecipeServices.cpp
    Source/features/recipes/RecipeState.cpp
)
list(APPEND GAPFILL_SOURCES
    Source/features/audition/AuditionGui.cpp
    Source/features/audition/Help.cpp
)
