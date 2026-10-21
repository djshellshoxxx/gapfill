#include "Phrase.h"
#include "../timing/BarTiming.h"

#include <algorithm>
#include <cmath>

namespace gapfill::phrase
{

double positionAt (const PhraseSettings& ph, double ppq, const Effective::TransportInfo& t) noexcept
{
    if (ph.bars <= 0 || ph.bias01 <= 0.0f || ! std::isfinite (ppq))
        return -1.0;

    const auto bpb = timing::beatsPerBar (t);

    if (bpb <= 0.0)
        return -1.0;

    const auto lenBeats = (double) ph.bars * bpb;
    const auto offset = (double) (std::max (0, ph.offsetBars) % ph.bars) * bpb;   // taken modulo the phrase length
    return timing::frac ((ppq - offset) / lenBeats);
}

bool inFinalBar (const PhraseSettings& ph, double ppq, const Effective::TransportInfo& t) noexcept
{
    PhraseSettings probe = ph;
    probe.bias01 = 1.0f;                      // the position does not depend on the bias
    const auto p = positionAt (probe, ppq, t);
    return p >= 0.0 && p >= 1.0 - 1.0 / (double) ph.bars;
}

float rampAt (double position, int bars) noexcept
{
    if (bars <= 0 || position < 0.0)
        return 0.0f;

    const auto f = 1.0 - 1.0 / (double) bars;
    const auto x = std::clamp ((position - f) / (1.0 - f), 0.0, 1.0);
    return (float) (x * x * (3.0 - 2.0 * x));
}

float multiplierFor (float bias01, float r) noexcept
{
    return 1.0f + std::clamp (bias01, 0.0f, 1.0f) * 1.5f * std::clamp (r, 0.0f, 1.0f);
}

void apply (EngineSettings& s, double ppq, const Effective::TransportInfo& t) noexcept
{
    const auto p = positionAt (s.perf.phrase, ppq, t);

    if (p < 0.0)
        return;

    const auto r = rampAt (p, s.perf.phrase.bars);

    if (r <= 0.0f)
        return;                                  // outside the final bar: exactly 1

    s.perf.probabilityMul *= multiplierFor (s.perf.phrase.bias01, r);
}

} // namespace gapfill::phrase
