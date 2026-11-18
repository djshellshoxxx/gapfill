#include "EngineTargets.h"
#include "FxTargets.h"
#include "PerformanceTargets.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace gapfill::targets
{

namespace
{
    bool eq (const char* a, const char* b) noexcept { return std::strcmp (a, b) == 0; }

    std::uint32_t extendedOwnership (const char* id) noexcept
    {
        constexpr const char* ids[] { "mistakes", "rcBits", "rcRate", "resDecay", "resDamp", "ratGate",
            "tdMix", "fxSend", "bandLowAmt", "bandMidAmt", "bandHighAmt", "fm20_amount" };
        for (int index = 0; index < (int) std::size (ids); ++index)
            if (eq (id, ids[index])) return 1u << (15 + index);
        return 0;
    }
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
        { "hgAmount", Kind::HarmonicAmount, 0 }, { "hgVoices", Kind::HarmonicVoices, 0 },
        { "hgMotion", Kind::HarmonicMotion, 0 }, { "hgTone", Kind::HarmonicTone, 0 },
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

Ref sceneRefForId (const char* id) noexcept
{
    auto legacy = refForId (id);
    const auto performance = performanceTargets::indexForId (id);
    if (legacy)
    {
        legacy.performanceIndex = (std::uint8_t) (performance + 1);
        legacy.advancedMacroMask = extendedOwnership (id);
        return legacy;
    }
    if (eq (id, "energyAdapt")) return { Kind::EnergyAdapt, 0, 1u << 12 };
    if (eq (id, "scaleAmount")) return { Kind::ScaleAmount, 0, 1u << 13 };
    if (eq (id, "fl09_amount")) return { Kind::GrooveAmount, 0, 1u << 14 };
    const auto effect = fxTargets::indexForId (id);
    if (effect < 0)
        return performance >= 0 ? Ref { Kind::Performance, (std::uint8_t) performance, extendedOwnership (id), (std::uint8_t) (performance + 1) } : Ref {};
    constexpr const char* advanced[] = { "swellAmount", "cloudAmount", "ratAmount", "rcAmount", "tdAmount",
        "resAmount", "fsdAmount", "dubAmount", "fmtAmount", "sfzAmount", "cvAmount" };
    std::uint32_t mask = extendedOwnership (id);
    for (int target = 0; target < 11; ++target)
        if (eq (id, advanced[target])) mask = 1u << target;
    return { Kind::Effect, (std::uint8_t) effect, mask };
}

std::uint32_t macroBit (Ref ref) noexcept
{
    switch ((int) ref.kind)
    {
        case (int) Kind::Fill:       return 1u << 0;
        case (int) Kind::Intensity:  return 1u << 1;
        case (int) Kind::Complexity: return 1u << 2;
        case (int) Kind::Chaos:      return 1u << 3;
        case (int) Kind::Density:    return 1u << 4;
        case (int) Kind::Weight:     return 1u << (6 + std::min<int> (ref.sub, 14));
        default:                     return 0u;
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
        case Kind::HarmonicAmount: s.perf.harmonic.amount01 = std::clamp (pct, 0.0f, 1.0f); break;
        case Kind::HarmonicVoices: s.perf.harmonic.voices = std::clamp ((int) std::lround (real), 1, 6); break;
        case Kind::HarmonicMotion: s.perf.harmonic.motion01 = std::clamp (pct, 0.0f, 1.0f); break;
        case Kind::HarmonicTone:   s.perf.harmonic.tone = std::clamp (pct, -1.0f, 1.0f); break;
        case Kind::Effect:      fxTargets::apply (s, ref.sub, real); break;
        case Kind::EnergyAdapt: s.perf.energy.adapt01 = std::clamp (pct, 0.0f, 1.0f); break;
        case Kind::ScaleAmount: s.scaleLock.amount01 = std::clamp (pct, 0.0f, 1.0f); break;
        case Kind::GrooveAmount: s.perf.groove.amount01 = std::clamp (pct, 0.0f, 1.0f); break;
        case Kind::Performance: performanceTargets::apply (s, ref.sub, real); break;
        case Kind::None:        break;
    }
}

bool isBusEffect (Ref ref) noexcept
{
    return ref.kind == Kind::Effect && fxTargets::isBusEffect (ref.sub);
}

} // namespace gapfill::targets
