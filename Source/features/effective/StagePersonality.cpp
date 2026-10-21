#include "Effective.h"
#include "../personality/Personality.h"

namespace gapfill::Effective
{

// C-1: table-driven overlay on weights and four scalars (see personality/Personality.cpp).
// Identity for personality Off.
void applyPersonality (EngineSettings& s, int personality)
{
    personality::apply (s, personality);
}

} // namespace gapfill::Effective
