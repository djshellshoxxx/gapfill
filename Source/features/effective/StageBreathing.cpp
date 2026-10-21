#include "Effective.h"
#include "../breathing/Breathing.h"

namespace gapfill::Effective
{

// C-6: bar-locked density swell. Neutral while off, depth 0 or without a valid transport.
void applyBreathing (EngineSettings& sLocal, double ppq, const TransportInfo& ts)
{
    breathing::apply (sLocal, ppq, ts);
}

} // namespace gapfill::Effective
