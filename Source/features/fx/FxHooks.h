#pragma once

// The scheduler/engine-facing entry points of the effect features (WP8). Each
// is called from exactly one explicit line in the seam it belongs to
// (FeatureHooks.cpp gather list, FillEngine::adjustRecipe, FillEngine::toVoiceSetup).

#include "FxMeters.h"
#include "../../Parameters.h"
#include "../../dsp/EngineSettings.h"
#include "../../dsp/FillVoice.h"
#include "../../dsp/SchedulerTypes.h"

namespace gapfill::fx
{

/** Message/audio-thread safe read of the effect parameters into s.fx. */
void gatherFx (EngineSettings& s, const ParamCache& p) noexcept;

/** Recipe stage: decides the Reverse Swell / Granular Cloud render modes
    (recipe flags bit3 / bit4) with salted sub-RNGs. Recipes replayed from Lock
    keep the flags they were recorded with. */
void decideRenderModes (FillPlan& plan, const GapContext& ctx, const EngineSettings& s) noexcept;

/** Voice setup stage: latches the effect settings into the voice, plans the
    swell landing (shortening the voice to land on the target) and marks band
    voices that bypass the effects (F-L14). */
void prepareVoiceSetup (VoiceSetup& setup, const FillPlan& plan, const GapContext& ctx,
                        const EngineSettings& s, FxMeters* meters) noexcept;

} // namespace gapfill::fx
