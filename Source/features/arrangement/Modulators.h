#pragma once

// F-X04 decision modulators (X04b): Calm, Lift, Settle. Pure functions of the published
// ArrangementState and the settings; no RNG, no state, identity when off, learning or unsure.

#include "SectionTracker.h"

namespace gapfill::arrangement
{

struct Fx04Settings
{
    bool  on = false;
    float calm01 = 0.5f, lift01 = 0.5f, settle01 = 0.3f, sensitivity01 = 0.5f;
    ArrangementState state;                       ///< filled per chunk by the context layer
};

struct ModulatorResult
{
    float densityFactor = 1.0f;                   ///< calm * settle
    float lift = 0.0f;                            ///< 0..1
    bool  active = false;
};

/** `bpm` > 0 and `beatsPerBar` > 0 when the host tempo is known (frames are 0.5 s). */
ModulatorResult evaluate (const Fx04Settings&, double bpm, double beatsPerBar) noexcept;

} // namespace gapfill::arrangement
