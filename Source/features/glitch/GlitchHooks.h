#pragma once

// F-G01 Glitch rack: the adapter between host parameters and the portable core.

#include "GlitchTypes.h"
#include "../../Parameters.h"

namespace gapfill::glitch
{

/** Reads the fg01_* parameters (and the global Seed) into a settings snapshot.
    Audio-thread safe: relaxed atomic loads only. */
Settings gatherSettings (const ParamCache& p) noexcept;

} // namespace gapfill::glitch
