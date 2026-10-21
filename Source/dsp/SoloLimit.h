#pragma once

// Band Solo monitor limiter (QA report 2026-10-21, finding "Band Solo overshoot").
//
// Band Solo replaces the output with one live-retuned Linkwitz-Riley band. While the crossover moves, a
// hostile full-scale input (DC, Nyquist square) made that band overshoot up to 6.7x. soloLimit() is the
// identity for |x| <= 1 (every normal level, exactly), and above that bends smoothly (slope 1 at the knee)
// towards a ceiling of 2 (+6 dBFS), the same bound as the rest of the output contract. Non-finite input is
// passed through unchanged for the caller's own sanitising.
//
// Real-time safe: no allocation, no state, bounded work.

#include <cmath>

namespace gapfill::dsp
{

inline constexpr float kSoloKnee = 1.0f;
inline constexpr float kSoloCeiling = 2.0f;

inline float soloLimit (float x) noexcept
{
    const float a = std::abs (x);

    if (! (a > kSoloKnee))
        return x;

    constexpr float range = kSoloCeiling - kSoloKnee;
    const float limited = kSoloKnee + range * std::tanh ((a - kSoloKnee) / range);
    return std::isfinite (a) ? std::copysign (limited, x) : x;
}

} // namespace gapfill::dsp
