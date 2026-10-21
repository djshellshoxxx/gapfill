#include "ShapingRuntime.h"
#include "../../Parameters.h"

#include <cstring>

namespace gapfill
{

void EffectiveCreative::publish (const EngineSettings& s) noexcept
{
    const float head[5] = { s.fill01, s.intensity01, s.complexity01, s.chaos01, s.density01 };

    for (int i = 0; i < 5; ++i)
        value[(std::size_t) i].store (head[i], std::memory_order_relaxed);

    for (int a = 0; a < numAlgos; ++a)
        value[(std::size_t) (5 + a)].store (s.weights[a], std::memory_order_relaxed);

    valid.store (true, std::memory_order_relaxed);
}

const char* effectiveControlId (int index) noexcept
{
    static constexpr const char* head[5] = { "fill", "intensity", "complexity", "chaos", "density" };

    if (index >= 0 && index < 5)
        return head[index];

    if (index >= 5 && index < EffectiveCreative::numControls)
        return algoWeightID ((Algo) (index - 5));

    return nullptr;
}

int effectiveControlIndex (const char* parameterId) noexcept
{
    if (parameterId == nullptr)
        return -1;

    for (int i = 0; i < EffectiveCreative::numControls; ++i)
        if (std::strcmp (effectiveControlId (i), parameterId) == 0)
            return i;

    return -1;
}

} // namespace gapfill
