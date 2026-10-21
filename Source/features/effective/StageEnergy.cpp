#include "Effective.h"

namespace gapfill::Effective
{

// F-L22 Musical energy tracking (docs/dev/12): sparse sections get more fills, busy
// ones fewer but bolder. Offsets per level {Low, Mid, High}, scaled by the Follow
// amount; the tracker publishes the level once per chunk (features/context).
// Identity (no write at all) while energy.adapt01 is 0 or the level is Mid.
void applyEnergy (EngineSettings& s, double /*ppq*/, const TransportInfo&)
{
    const auto& e = s.perf.energy;

    if (e.adapt01 <= 0.0f || e.level == 1)
        return;

    static constexpr float kDensity[3] = { +0.15f, 0.0f, -0.15f };
    static constexpr float kIntensity[3] = { -0.10f, 0.0f, +0.15f };
    const int lv = e.level < 0 ? 0 : (e.level > 2 ? 2 : e.level);

    const auto clamp01 = [] (float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };

    if (e.target == 0 || e.target == 2)
        s.density01 = clamp01 (s.density01 + e.adapt01 * kDensity[lv]);

    if (e.target == 1 || e.target == 2)
        s.intensity01 = clamp01 (s.intensity01 + e.adapt01 * kIntensity[lv]);
}

} // namespace gapfill::Effective
