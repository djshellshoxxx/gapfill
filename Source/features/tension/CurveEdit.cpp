#include "CurveEdit.h"

#include <algorithm>
#include <cmath>

namespace gapfill::tension::edit
{

int movePoint (TensionLane& l, int index, int x, int y) noexcept
{
    normalise (l);

    if (index < 0 || index >= l.numPoints)
        return -1;

    l.pt[index].y = (std::uint16_t) std::clamp (y, 0, 1000);

    if (index > 0 && index < l.numPoints - 1)
    {
        const int lo = l.pt[index - 1].x + kMinSpacing;
        const int hi = l.pt[index + 1].x - kMinSpacing;
        l.pt[index].x = (std::uint16_t) std::clamp (x, lo, std::max (lo, hi));
    }

    normalise (l);
    return index;
}

int addPoint (TensionLane& l, int x, int y) noexcept
{
    normalise (l);

    if (l.numPoints >= kMaxPoints)
        return -1;

    x = std::clamp (x, 0, 1000);

    // The segment that contains x.
    int seg = 0;

    while (seg < l.numPoints - 2 && x >= l.pt[seg + 1].x)
        ++seg;

    const int lo = l.pt[seg].x + kMinSpacing;
    const int hi = l.pt[seg + 1].x - kMinSpacing;

    if (hi < lo)
        return -1;                                    // no room between these two points

    x = std::clamp (x, lo, hi);

    for (int i = l.numPoints; i > seg + 1; --i)
        l.pt[i] = l.pt[i - 1];

    l.pt[seg + 1].x = (std::uint16_t) x;
    l.pt[seg + 1].y = (std::uint16_t) std::clamp (y, 0, 1000);
    ++l.numPoints;
    normalise (l);
    return seg + 1;
}

bool removePoint (TensionLane& l, int index) noexcept
{
    normalise (l);

    if (l.numPoints <= 2 || index <= 0 || index >= l.numPoints - 1)
        return false;

    for (int i = index; i < l.numPoints - 1; ++i)
        l.pt[i] = l.pt[i + 1];

    --l.numPoints;
    normalise (l);
    return true;
}

void resetLane (TensionLane& l) noexcept
{
    l = TensionLane {};
}

void copyShape (TensionLane& l, int shape) noexcept
{
    if (shape < Build || shape >= Custom)
    {
        resetLane (l);
        return;
    }

    constexpr int n = kMaxPoints;
    l = TensionLane {};
    l.interp = Linear;
    l.numPoints = (std::uint8_t) n;

    for (int i = 0; i < n; ++i)
    {
        l.pt[i].x = (std::uint16_t) ((i * 1000 + (n - 1) / 2) / (n - 1));
        l.pt[i].y = (std::uint16_t) std::clamp ((int) std::lround (shapeValue (shape, (float) l.pt[i].x * 0.001f) * 1000.0f), 0, 1000);
    }

    normalise (l);
}

int nearestPoint (const TensionLane& l, int x, int y, float xWeight) noexcept
{
    int best = 0;
    float bestD = 1.0e30f;

    for (int i = 0; i < std::clamp<int> (l.numPoints, 2, kMaxPoints); ++i)
    {
        const float dx = (float) (l.pt[i].x - x) * xWeight, dy = (float) (l.pt[i].y - y);
        const float d = dx * dx + dy * dy;

        if (d < bestD)
        {
            bestD = d;
            best = i;
        }
    }

    return best;
}

int nudgePoint (TensionLane& l, int index, int dx, int dy) noexcept
{
    normalise (l);

    if (index < 0 || index >= l.numPoints)
        return -1;

    return movePoint (l, index, (int) l.pt[index].x + dx, (int) l.pt[index].y + dy);
}

} // namespace gapfill::tension::edit
