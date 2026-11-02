#pragma once

// F-X04 decision modulators (X04b): Calm, Lift, Settle. Pure functions of the published
// ArrangementState and the settings; no RNG, no state, identity when off, learning or unsure.

#include "SectionTracker.h"

namespace gapfill::arrangement
{

struct Fx04Settings
{
    int modelVersion = 1;                         // metadata, never a host parameter
    bool  on = false;
    float calm01 = 0.5f, lift01 = 0.5f, settle01 = 0.3f, sensitivity01 = 0.5f;
    ArrangementState state;                       ///< filled per chunk by the context layer
};

struct ModulatorResult
{
    float densityFactor = 1.0f;                   ///< calm * settle
    float probabilityFactor = 1.0f;               ///< V2 Calm attenuates the final decision probability
    float lift = 0.0f;                            ///< 0..1
    bool  active = false;
};

/** `bpm` > 0 and `beatsPerBar` > 0 when the host tempo is known (frames are 0.5 s). */
ModulatorResult evaluate (const Fx04Settings&, double bpm, double beatsPerBar) noexcept;

/** V2 prediction alignment. Quarter-note PPQ, positive BPM and a valid time
    signature are required; without them the published forecast is unknown.
    A positive phraseBars prefers its grid over the single-bar grid. */
ArrangementState snapPrediction (ArrangementState, bool transportValid, double ppq,
                                double bpm, int numerator, int denominator, int phraseBars) noexcept;

} // namespace gapfill::arrangement
