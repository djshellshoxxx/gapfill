# WP6 live control: command map (X-1), panic (N-USE-a), scenes (F-L07), bank (N-VAL),
# live pads (F-M10/F-S07/F-P02), macros (C-2/F-M23), battles (F-M18).
list(APPEND GAPFILL_SOURCES
    Source/features/common/EngineTargets.cpp
    Source/features/commands/CommandMap.cpp
    Source/features/panic/Panic.cpp
    Source/features/scenes/SceneModel.cpp
    Source/features/scenes/SceneOverlay.cpp
    Source/features/scenes/SceneController.cpp
    Source/features/effective/StageScene.cpp
    Source/features/bank/BankFile.cpp
    Source/features/live/LiveControl.cpp
    Source/gui/slots/PanicSlot.cpp
)
