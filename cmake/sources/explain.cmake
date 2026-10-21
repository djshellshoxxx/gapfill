# WP3 Explain & assist: E-3 reasons, F-S01 telemetry, N-USE-b health, N-EASY fix-it,
# F-M03 timeline, F-D02 event recorder (docs/dev/12 section 13). Owned by WP3.
list(APPEND GAPFILL_SOURCES
    Source/features/reasons/Reasons.cpp
    Source/features/telemetry/Telemetry.cpp
    Source/features/health/Health.cpp
    Source/features/fixit/Fixit.cpp
    Source/features/timeline/Model.cpp
    Source/features/events/EventRecorder.cpp
    Source/features/events/Replay.cpp
    Source/features/events/Diagnostics.cpp
    Source/features/explain/ExplainState.cpp
    Source/features/explain/OverlaySlot.cpp
    Source/features/explain/Help.cpp
    Source/features/reasons/Gui.cpp
    Source/features/timeline/Gui.cpp
    Source/features/health/Gui.cpp
)
