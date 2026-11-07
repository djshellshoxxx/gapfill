# F-G01: Glitch rack (GLITCH tab). Portable DSP core + parameter group + adapters.
list(APPEND GAPFILL_SOURCES
    Source/params/glitch.cpp
    Source/features/glitch/GlitchRack.cpp
    Source/features/glitch/GlitchHooks.cpp
    Source/features/glitch/Info.cpp
    Source/features/glitch/Help.cpp
    Source/features/glitch/Gui.cpp
)
