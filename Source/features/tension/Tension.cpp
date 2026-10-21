#include "Tension.h"

#include <algorithm>
#include <cstring>

namespace gapfill::tension
{

void applyDecision (EngineSettings& s, double ppq, bool valid, int tsNum, int tsDen) noexcept
{
    const auto& t = s.perf.tension;

    if (! t.active())
        return;

    const auto out = tensionAt (ppq, valid, tsNum, tsDen, t);

    if (! out.active)
        return;

    const auto clamp01 = [] (float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };

    if (t.depth[Density] > 0.0f)
        s.density01 = clamp01 (s.density01 + 0.5f * t.depth[Density] * (2.0f * out.c[Density] - 1.0f));

    if (t.depth[Intensity] > 0.0f)
        s.intensity01 = clamp01 (s.intensity01 + 0.5f * t.depth[Intensity] * (2.0f * out.c[Intensity] - 1.0f));

    if (t.depth[FxSend] > 0.0f)
        s.perf.fxSend01 = clamp01 (1.0f - t.depth[FxSend] * std::max (0.0f, 1.0f - 2.0f * out.c[FxSend]));
}

float widthDelta (const TensionSettings& t, float base, double ppqMid, bool valid, int tsNum, int tsDen) noexcept
{
    if (! t.active() || t.depth[Width] <= 0.0f)
        return 0.0f;

    const auto out = tensionAt (ppqMid, valid, tsNum, tsDen, t);

    if (! out.active)
        return 0.0f;

    const float target = std::clamp (base + 0.5f * t.depth[Width] * (2.0f * out.c[Width] - 1.0f), 0.0f, 2.0f);
    return target - base;
}

bool TensionService::publishCurves (const TensionCurves& c) noexcept
{
    if (! validate (c))
        return false;

    seq.fetch_add (1, std::memory_order_acq_rel);
    stored = c;
    seq.fetch_add (1, std::memory_order_release);
    return true;
}

TensionCurves TensionService::curves() const noexcept
{
    TensionCurves copy;

    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const auto a = seq.load (std::memory_order_acquire);

        if ((a & 1u) != 0)
            continue;

        std::memcpy (&copy, &stored, sizeof (TensionCurves));

        if (seq.load (std::memory_order_acquire) == a)
            return copy;
    }

    return TensionCurves {};                              // writer busy twice: flat (neutral) for this chunk
}

void TensionService::setStatus (const TensionOut& o) noexcept
{
    phase.store (o.phase, std::memory_order_relaxed);
    active.store (o.active, std::memory_order_relaxed);
}

bool TensionService::isDefault() const noexcept
{
    const auto c = curves();
    const TensionCurves flat;

    for (int i = 0; i < kLanes; ++i)
    {
        const auto& a = c.lane[i];
        const auto& b = flat.lane[i];

        if (a.numPoints != b.numPoints || a.interp != b.interp)
            return false;

        for (int p = 0; p < a.numPoints; ++p)
            if (a.pt[p].x != b.pt[p].x || a.pt[p].y != b.pt[p].y)
                return false;
    }

    return true;
}

} // namespace gapfill::tension
