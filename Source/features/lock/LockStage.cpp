#include "LockStage.h"
#include "../Salts.h"
#include "../../dsp/DspUtils.h"

namespace gapfill::lockstage
{

namespace
{
    /** Nearest band to `wanted` that is switched on and has a non-zero probability, or -1. */
    int nearestUsableBand (int wanted, const EngineSettings& s) noexcept
    {
        int best = -1;
        int bestDistance = 99;

        for (int b = 0; b < 3; ++b)
        {
            if (! s.bandOn[b] || s.bandProb[b] <= 0.0f)
                continue;

            const auto d = std::abs (b - wanted);

            if (d < bestDistance)
            {
                best = b;
                bestDistance = d;
            }
        }

        return best;
    }
}

void gather (EngineSettings& s, const ParamCache& params) noexcept
{
    s.perf.lockOn = pb (params.fl01_lock);
}

RejectReason apply (const LockState& lock, FillPlan& plan, const GapContext& ctx,
                    const EngineSettings& s, double sampleRate, float lockedMinMs) noexcept
{
    const auto& locked = lock.recipe;

    // ---- length ----------------------------------------------------------
    const auto lockedSamples = (std::int64_t) ((double) locked.lengthMs * 0.001 * sampleRate);
    const auto lengthSamples = juce::jmin (lockedSamples, ctx.usable);
    const auto lengthMs = (float) (1000.0 * (double) lengthSamples / sampleRate);

    if (lengthMs < 0.6f * lockedMinMs)
    {
        plan.lengthMs = lengthMs;
        plan.neededMs = 0.6f * lockedMinMs;
        return RejectReason::LockSkipped;
    }

    // ---- band (spectral mode only) ----------------------------------------
    int band = -1;

    if (s.mode == DetMode::Spectral)
    {
        band = plan.band;                       // locked broadband: keep the lane the roll chose

        if (locked.band >= 0)
        {
            band = nearestUsableBand (locked.band, s);

            if (band < 0)
                return RejectReason::NoBand;
        }
    }

    // ---- the plan ---------------------------------------------------------
    plan.recipe = locked;
    plan.recipe.band = (std::int8_t) band;
    plan.recipe.lengthMs = (std::uint16_t) juce::jlimit (0, 65535, (int) std::lround (lengthMs));
    plan.recipe.lengthClass = lengthClassFor (lengthMs, s.maxFillMs);
    plan.recipe.flags = (std::uint8_t) ((locked.flags & ~(FillRecipe::flagPreview | FillRecipe::flagBiased))
                                          | FillRecipe::flagFromLock);

    // A fresh seed per fill keeps repeats from being literal loops.
    plan.recipe.seed = (std::uint32_t) hashCombine (hashCombine (ctx.key, kSalt_Lock),
                                                    (std::uint64_t) (locked.algo + 1));

    plan.algoIndex = locked.algo;
    plan.band = band;
    plan.lengthSamples = lengthSamples;
    plan.lengthMs = lengthMs;
    return RejectReason::None;
}

} // namespace gapfill::lockstage
