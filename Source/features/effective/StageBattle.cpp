#include "Effective.h"
#include "../battle/Battle.h"

namespace gapfill::Effective
{

// F-M18: the active style of the current bar window replaces the base values (macro-owned targets
// excepted). Identity when Battle is off, inactive, or no battle runtime is attached.
void applyBattle (EngineSettings& s, double ppq, const TransportInfo& ts)
{
    if (s.perf.battle != nullptr)
        s.perf.battle->applyAt (s, ppq, ts.valid, ts.tsNum, ts.tsDen);
}

} // namespace gapfill::Effective
