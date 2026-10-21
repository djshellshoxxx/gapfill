#include "Effective.h"
#include "../phrase/Phrase.h"

namespace gapfill::Effective
{

// C-7: fill-probability ramp in the last bar of a phrase. Neutral while bars == 0 or bias == 0.
void applyPhrase (EngineSettings& sLocal, double ppq, const TransportInfo& ts)
{
    phrase::apply (sLocal, ppq, ts);
}

} // namespace gapfill::Effective
