#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gapfill::convreverb
{
enum class Source : std::uint8_t { LastHit, Strongest, Manual };
enum class Stereo : std::uint8_t { Stereo, Mono };

/** Detailed F-L21 only. enabled=false keeps the released cv* card untouched.
    Settings carry no captured PCM, worker identity or host-owned pointer. */
struct Settings
{
    bool enabled = false;
    float mix01 = 0.0f;
    Source source = Source::LastHit;
    bool hold = false;
    float lengthMs = 250.0f;
    float preDelayMs = 0.0f;
    float dampHz = 12000.0f;
    float decay01 = 0.4f;
    Stereo stereo = Stereo::Stereo;
};

inline Settings sanitise (Settings value) noexcept
{
    const auto finiteClamp = [] (float input, float lo, float hi, float fallback)
    { return std::isfinite (input) ? std::clamp (input, lo, hi) : fallback; };
    value.mix01 = finiteClamp (value.mix01, 0.0f, 1.0f, 0.0f);
    value.lengthMs = finiteClamp (value.lengthMs, 50.0f, 800.0f, 250.0f);
    value.preDelayMs = finiteClamp (value.preDelayMs, 0.0f, 50.0f, 0.0f);
    value.dampHz = finiteClamp (value.dampHz, 2000.0f, 20000.0f, 12000.0f);
    value.decay01 = finiteClamp (value.decay01, 0.0f, 1.0f, 0.4f);
    if (value.source > Source::Manual) value.source = Source::LastHit;
    if (value.stereo > Stereo::Mono) value.stereo = Stereo::Stereo;
    return value;
}
} // namespace gapfill::convreverb
