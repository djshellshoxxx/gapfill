#pragma once

// F-X01: the scheduler-side decision, called from context::prepareVoiceSetup (the toVoiceSetup stage).

#include "../../dsp/EngineSettings.h"
#include "../../dsp/FillVoice.h"
#include "../../dsp/SchedulerTypes.h"
#include "../fx/FxMeters.h"

namespace gapfill::harmonic
{

/** For a Granular fill with hgAmount > 0: rolls the salted coin, runs the pitch step on the source
    around the anchor and, when the source is pitched (confidence >= 0.7), fills setup.harmonicSetup.
    Anything else leaves it disabled so the voice plays ordinary Granular. Writes the pitch readout
    into `meters` (may be null). */
void decide (VoiceSetup& setup, const FillPlan& plan, const GapContext& ctx, const EngineSettings& s,
             const SourceHistory& history, fx::FxMeters* meters) noexcept;

} // namespace gapfill::harmonic
