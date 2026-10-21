# WP7 F-L09 Groove inheritance and the shared musical-context glue.
list(APPEND GAPFILL_SOURCES
    Source/features/context/ContextHooks.cpp
    Source/features/groove/GrooveTracker.cpp
    Source/features/groove/GrooveWarp.cpp
    Source/features/groove/GrooveState.cpp
    Source/features/groove/GrooveStateTree.cpp
    Source/gui/slots/GrooveStrip.cpp
    # F-X03 automatic key detection
    Source/features/keydetect/KeyEstimator.cpp
    Source/features/keydetect/ChromaTracker.cpp
    Source/features/keydetect/AutoKey.cpp
    Source/features/keydetect/KeyStateTree.cpp
    Source/gui/slots/KeyStrip.cpp
)
