#include "EngineTargets.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gapfill::targets
{

namespace
{
    bool eq (const char* a, const char* b) noexcept { return std::strcmp (a, b) == 0; }
}

Ref refForId (const char* id) noexcept
{
    struct Row { const char* id; Kind kind; std::uint8_t sub; };
    static const Row rows[] =
    {
        { "fill", Kind::Fill, 0 }, { "intensity", Kind::Intensity, 0 }, { "complexity", Kind::Complexity, 0 },
        { "chaos", Kind::Chaos, 0 }, { "density", Kind::Density, 0 }, { "fillType", Kind::FillType, 0 },
        { "protTrans", Kind::ProtTrans, 0 }, { "protNext", Kind::ProtNext, 0 }, { "protSC", Kind::ProtSC, 0 },
        { "minGap", Kind::MinGap, 0 }, { "maxFill", Kind::MaxFill, 0 }, { "protectHold", Kind::ProtectHold, 0 },
        { "xoverLM", Kind::XoverLM, 0 }, { "xoverMH", Kind::XoverMH, 0 },
        { "bandLowOn", Kind::BandOn, 0 }, { "bandMidOn", Kind::BandOn, 1 }, { "bandHighOn", Kind::BandOn, 2 },
        { "bandLowAmt", Kind::BandAmt, 0 }, { "bandMidAmt", Kind::BandAmt, 1 }, { "bandHighAmt", Kind::BandAmt, 2 },
        { "bandLowInt", Kind::BandInt, 0 }, { "bandMidInt", Kind::BandInt, 1 }, { "bandHighInt", Kind::BandInt, 2 },
        { "bandLowProb", Kind::BandProb, 0 }, { "bandMidProb", Kind::BandProb, 1 }, { "bandHighProb", Kind::BandProb, 2 },
    };

    for (const auto& r : rows)
        if (eq (id, r.id))
            return { r.kind, r.sub };

    for (int a = 0; a < numAlgos; ++a)
        if (eq (id, algoWeightID ((Algo) a)))
            return { Kind::Weight, (std::uint8_t) a };

    return {};
}

std::uint32_t macroBit (Ref ref) noexcept
{
    switch (ref.kind)
    {
        case Kind::Fill:       return 1u << 0;
        case Kind::Intensity:  return 1u << 1;
        case Kind::Complexity: return 1u << 2;
        case Kind::Chaos:      return 1u << 3;
        case Kind::Density:    return 1u << 4;
        case Kind::Weight:     return 1u << (6 + std::min<int> (ref.sub, 14));
        default:               return 0u;
    }
}

void apply (EngineSettings& s, Ref ref, float real) noexcept
{
    if (! std::isfinite (real))
        return;

    const auto pct = real * 0.01f;
    const auto sub = (size_t) std::min<int> (ref.sub, 14);
    const auto band = (size_t) std::min<int> (ref.sub, 2);

    switch (ref.kind)
    {
        case Kind::Fill:        s.fill01 = pct; break;
        case Kind::Intensity:   s.intensity01 = pct; break;
        case Kind::Complexity:  s.complexity01 = pct; break;
        case Kind::Chaos:       s.chaos01 = pct; break;
        case Kind::Density:     s.density01 = pct; break;
        case Kind::FillType:    s.fillTypeIndex = (int) std::lround (real); break;
        case Kind::Weight:      s.weights[sub] = pct; break;
        case Kind::ProtTrans:   s.protTrans01 = pct; break;
        case Kind::ProtNext:    s.protNext01 = pct; break;
        case Kind::ProtSC:      s.protSC01 = pct; break;
        case Kind::MinGap:      s.minGapMs = real; break;
        case Kind::MaxFill:     s.maxFillMs = real; break;
        case Kind::ProtectHold: s.protectHoldMs = real; break;
        case Kind::BandOn:      s.bandOn[band] = real > 0.5f; break;
        case Kind::BandAmt:     s.bandAmt[band] = pct; break;
        case Kind::BandInt:     s.bandInt[band] = pct; break;
        case Kind::BandProb:    s.bandProb[band] = pct; break;
        case Kind::XoverLM:     s.xoverLM = real; s.xoverMH = std::max (s.xoverMH, s.xoverLM * 1.5f); break;
        case Kind::XoverMH:     s.xoverMH = std::max (real, s.xoverLM * 1.5f); break;
        case Kind::None:        break;
    }
}

} // namespace gapfill::targets
