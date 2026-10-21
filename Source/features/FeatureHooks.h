#pragma once

// The single, reviewable list of calls the processor makes into features
// (docs/dev/02-FOUNDATIONS.md F0-5). Explicit calls instead of dynamic
// registration so the call ORDER is visible in one place. The processor calls
// these three functions and nothing else; a feature adds its own line to the
// matching list in FeatureHooks.cpp.

#include "effective/Effective.h"
#include "../dsp/EngineEvents.h"
#include "../dsp/EngineSettings.h"
#include "../Parameters.h"

namespace gapfill
{

class FillEngine;

namespace hooks
{

/** Block stage inputs for Effective::applyBlock (message-thread-safe reads of
    the ParamCache atomics only). */
Effective::ModulationInputs gatherModulationInputs (const ParamCache&) noexcept;

/** Copies each feature's parameters into `s` (s.perf and friends). Neutral
    parameter values must leave the defaults in place. */
void gatherFeatureSettings (EngineSettings& s, const ParamCache&) noexcept;

/** Audio thread. Handles one command drained from the processor's `commands`
    queue at the top of processBlock. Unknown / unhandled types are ignored. */
void dispatchCommand (FillEngine&, const Command&) noexcept;

} // namespace hooks
} // namespace gapfill
