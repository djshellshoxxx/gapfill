#include "Effective.h"
#include "../evolve/Evolve.h"

namespace gapfill::Effective
{

// C-8: drifts the creative values towards Surprise-profile waypoints. Neutral while Evolve is Off
// or without a valid transport / time signature.
void applyEvolve (EngineSettings& sLocal, double ppq, const TransportInfo& ts)
{
    evolve::apply (sLocal, ppq, ts);
}

} // namespace gapfill::Effective
