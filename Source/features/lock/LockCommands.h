#pragma once

// Audio-thread handler for the C-5 / C-11 commands: LockLatest, LockRecipe, Unlock,
// LikeLatest, LikeRecipe (docs/dev/12 section 2.3 ids 2-6, permanent).

#include "../../dsp/EngineEvents.h"

namespace gapfill
{

class FillEngine;

namespace lock
{

/** Returns true when `command` was one of the five. */
bool handleCommand (FillEngine&, const Command&) noexcept;

} // namespace lock
} // namespace gapfill
