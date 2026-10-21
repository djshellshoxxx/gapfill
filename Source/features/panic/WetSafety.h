#pragma once

// F-L25 wet safety: a stateless soft limiter on the WET bus only, the last stage
// before the mix (docs/dev/12 F-L25).
//
//     y = x / (1 + (|x| / c)^4)^(1/4),   c = 0.89  (-1 dBFS)
//
// Zero latency, no memory (no overshoot state, nothing to reset), odd-symmetric,
// |y| < c for every finite x. It is documented as "not a mastering limiter": it
// bends peaks rather than controlling loudness. Deviation from unity is 0.086 dB
// at -8 dBFS and 0.21 dB at -6 dBFS (the spec's "< 0.1 dB below -6 dBFS" does not
// hold for this formula and constant; see docs/progress/WP6.md).

#include <cmath>

namespace gapfill::panic
{

inline constexpr float kWetSafetyCeiling = 0.89f;

inline float wetSafetySample (float x) noexcept
{
    const float r = std::abs (x) * (1.0f / kWetSafetyCeiling);
    const float r2 = r * r;
    return x / std::sqrt (std::sqrt (1.0f + r2 * r2));
}

inline void wetSafetyProcess (float* left, float* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        left[i] = wetSafetySample (left[i]);
        right[i] = wetSafetySample (right[i]);
    }
}

} // namespace gapfill::panic
