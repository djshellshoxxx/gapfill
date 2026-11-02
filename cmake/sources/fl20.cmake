# F-L20 per-voice formant, independent of the existing bus formant card.
list(APPEND GAPFILL_SOURCES
    Source/params/fl20.cpp
    Source/features/formant/FormantProcessor.cpp
    Source/features/formant/StereoStft.cpp
    Source/features/formant/Gui.cpp
    Source/features/formant/Info.cpp
    Source/features/formant/Help.cpp
)
