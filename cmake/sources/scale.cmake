# WP7 N-MUS Scale Lock (docs/dev/12 section 5). The parameter builder is listed in params.cmake.
list(APPEND GAPFILL_SOURCES
    Source/features/scale/ScaleLock.cpp
    Source/features/scale/ScaleHooks.cpp
    Source/gui/slots/ScaleStrip.cpp
    # F-L22 energy tracking (musical context)
    Source/features/context/EnergyTracker.cpp
    Source/features/context/ContextState.cpp
    Source/features/effective/StageEnergy.cpp
    Source/gui/slots/EnergyStrip.cpp
)
