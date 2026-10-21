#include "Overlay.h"

#include <algorithm>
#include <cmath>

namespace gapfill::mutation
{

void OverlayState::apply (EngineSettings& s) const noexcept
{
    OverlayMsg incoming[4];
    const int n = queue.pop (incoming, 4);

    for (int i = 0; i < n; ++i)
    {
        current = incoming[i];
        active = current.active;
    }

    if (! active)
        return;

    const auto v = [this] (int i)
    {
        const auto x = current.v[i];
        return std::isfinite (x) ? std::min (1.0f, std::max (0.0f, x)) : 0.0f;
    };

    s.fill01 = v (0);
    s.intensity01 = v (1);
    s.complexity01 = v (2);
    s.chaos01 = v (3);
    s.density01 = v (4);

    for (int i = 0; i < numAlgos; ++i)
        s.weights[i] = v (5 + i);

    for (int b = 0; b < 3; ++b)
    {
        s.bandAmt[b] = v (20 + b * 3);
        s.bandInt[b] = v (21 + b * 3);
        s.bandProb[b] = v (22 + b * 3);
    }
}

} // namespace gapfill::mutation
