#include "Quality.h"

#include "../../dsp/EngineSettings.h"

#include <algorithm>
#include <cstdio>

namespace gapfill::quality
{

const char* levelName (int level) noexcept
{
    switch (level)
    {
        case Balanced: return "Balanced";
        case Lite:     return "Lite";
        default:       return "Full";
    }
}

void applyToSettings (EngineSettings& s, int level) noexcept
{
    const auto& caps = capsFor (level);
    s.perf.qualityLevel = level < 0 ? 0 : (level >= NumLevels ? NumLevels - 1 : level);
    s.fx.lite = caps.spectralCardsOff;
    s.fx.tailCapScale = caps.tailScale;
    s.fx.cloud.maxGrains = caps.cloudGrains;
}

const char* footerText (int mode, int appliedLevel, char* buffer, int bufferSize) noexcept
{
    if (buffer == nullptr || bufferSize <= 0)
        return "";

    buffer[0] = 0;

    if (mode == ModeAuto)
        std::snprintf (buffer, (size_t) bufferSize, "Quality: %s (auto)", levelName (appliedLevel));
    else if (appliedLevel != Full)
        std::snprintf (buffer, (size_t) bufferSize, "Quality: %s", levelName (appliedLevel));

    return buffer;
}

//==============================================================================
void AutoGovernor::reset() noexcept
{
    *this = AutoGovernor();
}

int AutoGovernor::update (double dt, float cpu01, std::uint32_t overrunCount) noexcept
{
    dt = std::max (0.0, dt);
    clock += dt;
    sinceChange += dt;

    if (! seeded)
    {
        lastOverruns = overrunCount;      // overruns that happened before Auto was engaged do not count
        seeded = true;
    }

    hot  = cpu01 > kHotThreshold ? hot + dt : 0.0;
    calm = cpu01 < kCalmThreshold ? calm + dt : 0.0;

    if (overrunCount != lastOverruns)
    {
        // Several overruns inside one chunk count as one event per update call; two events within 5 s trigger.
        overrunAt[0] = overrunAt[1];
        overrunAt[1] = clock;
        lastOverruns = overrunCount;
    }

    const bool overrunTrigger = overrunAt[0] > -1.0e8 && overrunAt[1] - overrunAt[0] <= kOverrunWindowSeconds
                             && clock - overrunAt[1] <= kOverrunWindowSeconds;

    if (sinceChange >= kMinDwellSeconds)
    {
        if ((hot >= kHotSeconds || overrunTrigger) && current < Lite)
        {
            ++current;
            sinceChange = 0.0;
            hot = calm = 0.0;
            overrunAt[0] = overrunAt[1] = -1.0e9;
        }
        else if (calm >= kCalmSeconds && current > Full)
        {
            --current;
            sinceChange = 0.0;
            hot = calm = 0.0;
        }
    }

    return current;
}

//==============================================================================
void QualityController::reset() noexcept
{
    governor.reset();
    applied = requested = Full;
    pendingFor = 0.0;
}

QualityController::Step QualityController::update (int mode, bool nonRealtime, double dt, float cpu01,
                                                   std::uint32_t overrunCount, bool engineIdle) noexcept
{
    Step step;

    if (mode == ModeAuto)
        requested = nonRealtime ? Full : governor.update (nonRealtime ? 0.0 : dt, cpu01, overrunCount);
    else
        requested = std::clamp (mode, (int) Full, (int) Lite);

    if (requested != applied)
    {
        pendingFor += std::max (0.0, dt);

        if (engineIdle || pendingFor >= kForceAfterSeconds)
        {
            step.forceFade = ! engineIdle;
            applied = requested;
            step.changed = true;
            pendingFor = 0.0;
        }
    }
    else
    {
        pendingFor = 0.0;
    }

    step.level = applied;
    return step;
}

} // namespace gapfill::quality
