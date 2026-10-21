# WP8 voice effects (docs/dev/12 FX-0, N-FX1, N-FX2, F-M16, F-M14, F-M20, F-L14, F-M19).
# The parameter builders are listed in params.cmake; this fragment owns the rest.
list(APPEND GAPFILL_SOURCES
    Source/features/fx/FxHooks.cpp
    Source/features/fx/FxChain.cpp
    Source/features/fx/VoiceFxChain.cpp
    Source/features/fx/FxUiState.cpp
    Source/features/fx/RatchetCard.cpp
    Source/features/fx/RcrushCard.cpp
    Source/features/fx/TapeDestrCard.cpp
    Source/features/swell/SwellMode.cpp
    Source/features/cloud/CloudMode.cpp
    Source/features/scrub/ScrubVoice.cpp
    Source/gui/slots/EffectsSection.cpp
    Source/gui/slots/ScrubStrip.cpp
)
