#include "Effective.h"
#include "../tension/Tension.h"

namespace gapfill::Effective
{

// F-M21: density / intensity follow the tension curve; fxSend01 carries the effect-amount lane.
// Identity while perf.tension is off, all depths are 0, or the transport / time signature is invalid.
void applyTension (EngineSettings& sLocal, double ppq, const TransportInfo& ts)
{
    tension::applyDecision (sLocal, ppq, ts.valid, ts.tsNum, ts.tsDen);
}

} // namespace gapfill::Effective
