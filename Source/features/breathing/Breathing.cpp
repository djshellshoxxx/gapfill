#include "Breathing.h"
#include "../timing/BarTiming.h"

#include <algorithm>
#include <cmath>

namespace gapfill::breathing
{

float curveValue (int curve, double phase) noexcept
{
    const auto p = timing::frac (std::isfinite (phase) ? phase : 0.0);

    switch (curve)
    {
        case RiseFall: return (float) (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
        case Pulse:    return p < 0.5 ? 1.0f : -1.0f;
        case Sine:
        default:       return (float) std::sin (2.0 * 3.14159265358979323846 * p);
    }
}

double phaseAt (const BreathSettings& b, double ppq, const Effective::TransportInfo& t) noexcept
{
    if (! b.on || b.depth01 <= 0.0f || b.bars <= 0 || ! std::isfinite (ppq))
        return -1.0;

    const auto bpb = timing::beatsPerBar (t);

    if (bpb <= 0.0)
        return -1.0;

    return timing::frac (ppq / ((double) b.bars * bpb));
}

void apply (EngineSettings& s, double ppq, const Effective::TransportInfo& t) noexcept
{
    const auto phase = phaseAt (s.perf.breath, ppq, t);

    if (phase < 0.0)
        return;

    const auto depth = std::clamp (s.perf.breath.depth01, 0.0f, 1.0f);
    s.density01 = std::clamp (s.density01 + 0.5f * depth * curveValue (s.perf.breath.curve, phase), 0.0f, 1.0f);
}

} // namespace gapfill::breathing
