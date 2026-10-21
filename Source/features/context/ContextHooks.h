#pragma once

// WP7 musical-context: the engine-facing voice-setup stage. Called from the one
// line in FillEngine::toVoiceSetup; delegates to each context feature in order.

#include "../../dsp/EngineSettings.h"
#include "../../dsp/FillVoice.h"
#include "../../dsp/SchedulerTypes.h"

namespace gapfill::context
{

/** N-MUS Scale Lock (scale for the voice, recipe pitch override), then F-L09
    Groove shifts for Stutter repeats and Delay taps. Neutral settings write nothing. */
void prepareVoiceSetup (VoiceSetup& setup, const FillPlan& plan, const GapContext& ctx,
                        const EngineSettings& s) noexcept;

} // namespace gapfill::context
