#pragma once

// F-L09 settings as the scheduler sees them (part of PerformanceSettings).

#include "GrooveTracker.h"

namespace gapfill::groove
{

struct GrooveSettings
{
    float amount01 = 0.0f;      ///< Groove amount; 0 = off (neutral)
    int   grid = 1;             ///< 0 = 1/8, 1 = 1/16
    bool  hold = false;
    bool  syncOn = true;        ///< tempo-synced subdivisions exist (Sync switch; its default is on)
    GrooveSnapshot snap;        ///< what the tracker has learned (invalid = no offsets)

    bool active() const noexcept { return amount01 > 0.0f && syncOn && snap.valid; }
};

} // namespace gapfill::groove
