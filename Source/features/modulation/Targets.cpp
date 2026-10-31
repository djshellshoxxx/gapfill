#include "Targets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>

namespace gapfill::modulation
{
namespace
{
    const TargetInfo extraTargets[] {
        { "mistakes", "Mistakes", 0, 100 },
        { "rcBits", "Crush bits", 2, 16, 1, true },
        { "rcRate", "Crush rate", 1, 64, std::log (.5f) / std::log (7.0f / 63.0f), true },
        { "resDecayMs", "Resonator decay (ms)", 20, 2000, std::log (.5f) / std::log (180.0f / 1980.0f) },
        { "resDamp", "Resonator damping", 0, 100 },
        { "ratGate", "Ratchet gate", 10, 100 },
        { "tdMix", "Tape mix (active path)", 0, 100 },
        { "fxSend", "FX send", 0, 1 },
        { "bandLowAmt", "Low band fill", 0, 100 },
        { "bandMidAmt", "Mid band fill", 0, 100 },
        { "bandHighAmt", "High band fill", 0, 100 },
        { "fm20_amount", "Tape damage amount", 0, 100 }
    };
    static_assert (std::size (extraTargets) + macros::kNumTargets == kTargetCount);
    std::array<targets::Ref, kTargetCount> references {};
    std::once_flag initFlag;

    float unit (float value) noexcept { return std::isfinite (value) ? std::clamp (value, 0.0f, 1.0f) : 0.0f; }
}

TargetInfo targetInfo (int index) noexcept
{
    if (index < 0 || index >= kTargetCount) return {};
    if (index >= macros::kNumTargets) return extraTargets[index - macros::kNumTargets];
    const auto& legacy = macros::targetInfo (index);
    return { legacy.id, legacy.label, legacy.lo, legacy.hi };
}

const char* targetParameterId (int index, bool detailedTapeActive) noexcept
{
    if (index < 0 || index >= kTargetCount || index == 43) return nullptr;
    if (index < macros::kNumTargets) return macros::targetParameterId (index);
    if (index == 39) return "resDecay";
    if (index == 42) return detailedTapeActive ? "fm20_mix" : "tdAmount";
    return targetInfo (index).id;
}

void initTargets()
{
    std::call_once (initFlag, []
    {
        for (int index = 0; index < kTargetCount; ++index)
            if (const auto* id = targetParameterId (index)) references[(size_t) index] = targets::sceneRefForId (id);
    });
}

float targetRealValue (int index, float normalised) noexcept
{
    const auto info = targetInfo (index);
    if (info.id == nullptr) return 0;
    auto proportion = unit (normalised);
    if (std::abs (info.skew - 1.0f) > 1e-6f && proportion > 0) proportion = std::exp (std::log (proportion) / info.skew);
    auto real = info.low + (info.high - info.low) * proportion;
    if (info.integer) real = std::floor (real + .5f);
    return std::clamp (real, info.low, info.high);
}

TargetBase captureTargetBase (const EngineSettings& settings) noexcept
{
    TargetBase result;
    for (int lane = 0; lane < tension::kLanes; ++lane) result.tensionDepth[lane] = settings.perf.tension.depth[lane];
    return result;
}

void applyTarget (EngineSettings& settings, int index, float normalised, const TargetBase& base, TargetOutputs& outputs) noexcept
{
    if (index < 0 || index >= kTargetCount) return;
    const auto real = targetRealValue (index, normalised);
    switch (index)
    {
        case 5: settings.perf.macroWidthOn = true; settings.perf.macroWidth01 = real * .01f; return;
        case 32:
            for (int lane = 0; lane < tension::kLanes; ++lane)
                settings.perf.tension.depth[lane] = base.tensionDepth[lane] * unit (normalised);
            return;
        case 39: settings.fx.resonator.decaySeconds = real * .001f; return;
        case 42:
            if (settings.fx.tapeExtra.amount01 > 0) settings.fx.tapeExtra.mix01 = real * .01f;
            else settings.fx.tape.mixOverride01 = real * .01f;
            return;
        case 43: outputs.fxSend01 = real; return;
        case 47: settings.fx.tapeExtra.amount01 = real * .01f; return;
        default: targets::apply (settings, references[(size_t) index], real); return;
    }
}

std::uint32_t coreOwnership (int index) noexcept
{
    return index >= 0 && index < macros::kNumC2Targets ? targets::macroBit (references[(size_t) index]) : 0;
}

std::uint32_t advancedOwnership (int index) noexcept
{
    return index >= macros::kNumC2Targets && index < kTargetCount ? 1u << (index - macros::kNumC2Targets) : 0;
}

bool isContinuousTarget (int index) noexcept
{
    return index >= 0 && index < kTargetCount
        && (index == 5 || index == 32 || index == 42 || index == 43 || index == 47 || targets::isBusEffect (references[(size_t) index]));
}
} // namespace gapfill::modulation
