#pragma once

// F-L13 engine-facing settings (kept tiny so EngineSettings.h can hold them by value).

namespace gapfill::scdrive
{

class SidechainOnsetTracker;

enum Gate { GateOff = 0, GateWhenQuiet = 1, GateWhenActive = 2 };

struct ScSettings
{
    /** Null (and therefore inert) whenever the sidechain bus is not connected or both
        controls are off: the engine never sees a tracker it should not consult. */
    const SidechainOnsetTracker* tracker = nullptr;
    int   gate = GateOff;
    float threshDb = -30.0f;
    float snap01 = 0.0f;                 ///< 0..1 of the 30 ms snap window

    bool active() const noexcept   { return tracker != nullptr && (gate != GateOff || snap01 > 0.0f); }
};

} // namespace gapfill::scdrive
