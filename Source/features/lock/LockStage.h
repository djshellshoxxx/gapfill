#pragma once

// C-5 Fill Lock: the `adjustRecipe` stage (docs/dev/04-SPEC-CREATIVE.md C-5, "Fit rules")
// and the gather line for the `fl01_lock` parameter.

#include "LockState.h"
#include "../../dsp/EngineSettings.h"
#include "../../dsp/SchedulerTypes.h"

namespace gapfill::lockstage
{

/** gatherLock (docs/dev/02 F0-5): the fl01_lock parameter into EngineSettings::perf.lockOn. */
void gather (EngineSettings&, const ParamCache&) noexcept;

/** True when Lock replaces the scheduler's algorithm choice for this gap: engaged,
    valid, and Fill Type is Auto (a forced Fill Type overrides Lock). */
inline bool isActive (const LockState& lock, const EngineSettings& s) noexcept
{
    return s.perf.lockOn && lock.substituting() && s.fillTypeIndex <= 0;
}

/** Rewrites `plan` to the locked recipe. Pins character (algorithm, band, modifiers,
    pitch, stereo, length class); never forces a fill, changes the roll, or relaxes
    protection.

      length   = min (locked length, usable); shorter than 0.6 x the locked algorithm's
                 minimum => LockSkipped
      band     = spectral mode only: the locked band, else the nearest enabled band with
                 Band Probability > 0, else NoBand
      weight 0 = the locked algorithm still plays (the user locked it deliberately)

    `lockedMinMs` is the scheduler's minimum length for the locked algorithm. */
RejectReason apply (const LockState& lock, FillPlan& plan, const GapContext& ctx,
                    const EngineSettings& s, double sampleRate, float lockedMinMs) noexcept;

} // namespace gapfill::lockstage
