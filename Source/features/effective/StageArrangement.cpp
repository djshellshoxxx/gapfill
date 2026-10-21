#include "Effective.h"
#include "../arrangement/Modulators.h"

#include <algorithm>

namespace gapfill::Effective
{

// F-X04: Calm / Settle scale the fill density, Lift raises it and lengthens Max Fill.
// Identity (no write) while off, learning or unsure of the structure.
void applyArrangement (EngineSettings& sLocal, double /*ppq*/, const TransportInfo& ts)
{
    const auto& fx = sLocal.perf.arr;

    if (! fx.on)
        return;

    const double beatsPerBar = ts.tsNum > 0 && ts.tsDen > 0 ? 4.0 * (double) ts.tsNum / (double) ts.tsDen : 0.0;
    const auto r = arrangement::evaluate (fx, ts.valid ? ts.bpm : 0.0, ts.valid ? beatsPerBar : 0.0);

    if (! r.active)
        return;

    sLocal.density01 = std::clamp (sLocal.density01 * r.densityFactor + 0.5f * r.lift, 0.0f, 1.0f);
    sLocal.maxFillMs = std::clamp (sLocal.maxFillMs * (1.0f + 0.5f * r.lift), 20.0f, 2000.0f);
}

} // namespace gapfill::Effective
