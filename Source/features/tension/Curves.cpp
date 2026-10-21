#include "Curves.h"

#include <algorithm>
#include <cmath>

namespace gapfill::tension
{

namespace
{
    float smoothstep (float a, float b, float x) noexcept
    {
        const float t = std::clamp ((x - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    bool laneValid (const TensionLane& l) noexcept
    {
        if (l.numPoints < 2 || l.numPoints > kMaxPoints || l.interp > Step)
            return false;

        if (l.pt[0].x != 0 || l.pt[l.numPoints - 1].x != 1000)
            return false;

        for (int i = 0; i < l.numPoints; ++i)
        {
            if (l.pt[i].y > 1000 || l.pt[i].x > 1000)
                return false;

            if (i > 0 && l.pt[i].x <= l.pt[i - 1].x)
                return false;
        }

        return true;
    }
}

bool validate (const TensionCurves& c) noexcept
{
    for (const auto& l : c.lane)
        if (! laneValid (l))
            return false;

    return true;
}

void normalise (TensionLane& l) noexcept
{
    l.numPoints = (std::uint8_t) std::clamp<int> (l.numPoints, 2, kMaxPoints);
    l.interp = (std::uint8_t) std::min<int> (l.interp, Step);

    for (int i = 0; i < l.numPoints; ++i)
        l.pt[i].y = (std::uint16_t) std::min<int> (l.pt[i].y, 1000);

    l.pt[0].x = 0;
    l.pt[l.numPoints - 1].x = 1000;

    // Forward pass: at least kMinSpacing apart; backward pass keeps room for the pinned last point.
    for (int i = 1; i < l.numPoints - 1; ++i)
        l.pt[i].x = (std::uint16_t) std::max<int> (l.pt[i].x, l.pt[i - 1].x + kMinSpacing);

    for (int i = l.numPoints - 2; i >= 1; --i)
        l.pt[i].x = (std::uint16_t) std::min<int> (l.pt[i].x, l.pt[i + 1].x - kMinSpacing);

    for (int i = 1; i < l.numPoints; ++i)
        if (l.pt[i].x <= l.pt[i - 1].x)
            l.pt[i].x = (std::uint16_t) std::min (1000, l.pt[i - 1].x + 1);
}

float evalLane (const TensionLane& l, float phase01) noexcept
{
    const float x = std::clamp (phase01, 0.0f, 1.0f) * 1000.0f;
    const int n = std::clamp<int> (l.numPoints, 2, kMaxPoints);

    int i = 0;

    while (i < n - 2 && x >= (float) l.pt[i + 1].x)
        ++i;

    const float x0 = (float) l.pt[i].x, x1 = (float) l.pt[i + 1].x;
    const float y0 = (float) l.pt[i].y * 0.001f, y1 = (float) l.pt[i + 1].y * 0.001f;
    const float t = x1 > x0 ? std::clamp ((x - x0) / (x1 - x0), 0.0f, 1.0f) : 0.0f;

    switch (l.interp)
    {
        case Step:    return t >= 1.0f ? y1 : y0;
        case Smooth:  return y0 + (y1 - y0) * (t * t * (3.0f - 2.0f * t));
        default:      return y0 + (y1 - y0) * t;
    }
}

float shapeValue (int shape, float phase) noexcept
{
    const float p = std::clamp (phase, 0.0f, 1.0f);

    switch (shape)
    {
        case Build:      return p;
        case BuildDrop:  return p < 0.875f ? smoothstep (0.0f, 0.875f, p) : 0.5f;
        case Breakdown:  return p < 0.75f ? 1.0f - p / 0.75f : 0.5f * (p - 0.75f) / 0.25f;
        case Swell:      return std::sin (3.14159265f * p);
        default:         return 0.5f;
    }
}

TensionOut tensionAt (double ppq, bool transportValid, int tsNum, int tsDen, const TensionSettings& s) noexcept
{
    TensionOut out;

    if (! s.active() || ! transportValid || tsNum <= 0 || tsDen <= 0 || (tsDen & (tsDen - 1)) != 0 || ! std::isfinite (ppq))
        return out;

    const double beatsPerBar = 4.0 * (double) tsNum / (double) tsDen;
    const double barPos = ppq / beatsPerBar;
    const double u = barPos - (double) s.startBar;
    const double bars = (double) std::max (1, s.bars);

    if (u < 0.0)
        return out;                                   // before the start bar: neutral

    double phase;

    if (s.end == Loop)
        phase = (u / bars) - std::floor (u / bars);
    else if (u >= bars)
    {
        if (s.end == Release)
            return out;

        phase = 1.0;                                  // Hold: the last value
    }
    else
        phase = u / bars;

    out.active = true;
    out.phase = (float) phase;

    for (int i = 0; i < kLanes; ++i)
        out.c[i] = std::clamp (s.shape == Custom ? evalLane (s.curves.lane[i], (float) phase) : shapeValue (s.shape, (float) phase), 0.0f, 1.0f);

    return out;
}

} // namespace gapfill::tension
