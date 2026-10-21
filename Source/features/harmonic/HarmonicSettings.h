#pragma once

// F-X01 settings as the scheduler sees them (part of PerformanceSettings). Neutral = amount 0.

namespace gapfill::harmonic
{

struct HarmonicParams
{
    float amount01 = 0.0f;      ///< hgAmount: probability that a Granular fill becomes harmonic
    int   voices = 3;           ///< hgVoices 1..6
    float motion01 = 0.3f;      ///< hgMotion
    float tone = 0.0f;          ///< hgTone -1 .. 1
};

} // namespace gapfill::harmonic
