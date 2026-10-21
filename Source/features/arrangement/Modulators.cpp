#include "Modulators.h"

#include <algorithm>

namespace gapfill::arrangement
{

namespace
{
    constexpr float kMinConfidence = 0.3f;

    float smoothstep (float a, float b, float x) noexcept
    {
        const float t = std::clamp ((x - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
}

ModulatorResult evaluate (const Fx04Settings& s, double bpm, double beatsPerBar) noexcept
{
    ModulatorResult r;
    const auto& st = s.state;

    if (! s.on || st.learning || st.confidence < kMinConfidence)
        return r;

    const bool tempo = bpm > 1.0 && beatsPerBar > 0.0;
    const float framesPerBar = tempo ? (float) (beatsPerBar * 60.0 / bpm * 2.0) : 0.0f;       // 0.5 s frames

    const float settleFrames = tempo ? 2.0f * framesPerBar : 4.0f;
    const float liftWindow = tempo ? 4.0f * framesPerBar : 8.0f;

    const float calm = 1.0f - s.calm01 * 0.8f * smoothstep (0.35f, 0.0f, st.energy01) * st.confidence;
    const float settle = 1.0f - s.settle01 * (1.0f - smoothstep (0.0f, std::max (1.0f, settleFrames), (float) st.framesInto));

    const float proximity = st.expectedIn >= 0 ? smoothstep (liftWindow, 0.0f, (float) st.expectedIn) : 0.0f;
    const float buildTerm = std::max (0.0f, st.trend) * 0.5f;

    r.densityFactor = calm * settle;
    r.lift = std::clamp (s.lift01 * st.confidence * std::max (proximity, buildTerm), 0.0f, 1.0f);
    r.active = true;
    return r;
}

} // namespace gapfill::arrangement
