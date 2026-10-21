#pragma once

// N-MUS Scale Lock: the engine-facing entry points (docs/dev/12 section 5).
// Each is called from exactly one explicit line in its seam:
//   gatherScale        FeatureHooks.cpp gather list
//   decidePitch        FillEngine::adjustRecipe
//   prepareVoiceSetup  FillEngine::toVoiceSetup

#include "ScaleLock.h"
#include "../../Parameters.h"
#include "../../dsp/EngineSettings.h"
#include "../../dsp/FillVoice.h"
#include "../../dsp/SchedulerTypes.h"

namespace gapfill::scale
{

/** Reads scaleRoot / scaleType / scaleAmount into s.scale. Off = neutral. */
void gatherScale (EngineSettings& s, const ParamCache& p) noexcept;

/** Recipe stage. Pitch Rise / Pitch Fall: draws the semitone shift from the
    salted sub-RNG, snaps it with probability scaleAmount and stores it in
    FillRecipe::pitchSemis (flagSnapped when changed). No-op when the lock is
    off or the recipe was replayed from Lock. */
void decidePitch (FillPlan& plan, const GapContext& ctx, const EngineSettings& s) noexcept;

/** Voice setup stage: hands the scale to the voice (Granular, Cloud, Comb) and
    turns the recipe's pitch into VoiceSetup::pitchOverride. */
void prepareVoiceSetup (VoiceSetup& setup, const FillPlan& plan, const EngineSettings& s) noexcept;

} // namespace gapfill::scale
