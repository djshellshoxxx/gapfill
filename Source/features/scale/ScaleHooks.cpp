#include "ScaleHooks.h"

#include "../Salts.h"

#include <cmath>

namespace gapfill::scale
{

void gatherScale (EngineSettings& s, const ParamCache& p) noexcept
{
    s.scaleLock.set = makeScaleSet (pi (p.scaleRoot), pi (p.scaleType));
    s.scaleLock.amount01 = juce::jlimit (0.0f, 1.0f, pv (p.scaleAmount) / 100.0f);
}

void decidePitch (FillPlan& plan, const GapContext& ctx, const EngineSettings& s) noexcept
{
    const auto algo = (Algo) plan.algoIndex;

    if (! s.scaleLock.active() || (algo != Algo::PitchRise && algo != Algo::PitchFall))
        return;

    auto& r = plan.recipe;

    if ((r.flags & FillRecipe::flagFromLock) != 0)
        return;                                         // replay keeps the recorded pitch

    // The same distribution the voice uses, drawn from the salted sub-RNG (the
    // voice's own draw order is untouched).
    const auto intensity = plan.band >= 0 ? juce::jlimit (0.0f, 1.0f, s.bandInt[plan.band]) : s.intensity01;

    Rng sub;
    sub.seed (hashCombine (ctx.key, kSalt_Scale));
    const auto amount = lerp (5.0f, 26.0f, intensity) * (1.0f + s.chaos01 * sub.nextFloat() * 0.5f);
    const auto sign = algo == Algo::PitchRise ? 1 : -1;
    const auto drawn = sign * (int) std::lround (amount);

    auto semis = drawn;

    const bool snap = s.scaleLock.amount01 >= 0.9999f || sub.chance (s.scaleLock.amount01);

    if (snap)
        semis = snapInterval (drawn, s.scaleLock.set);

    r.pitchSemis = (std::int8_t) juce::jlimit (-48, 48, semis);

    if (semis != drawn)
        r.flags |= FillRecipe::flagSnapped;
}

void prepareVoiceSetup (VoiceSetup& setup, const FillPlan& plan, const EngineSettings& s) noexcept
{
    setup.scaleLock = s.scaleLock;

    const auto algo = plan.recipe.algo;

    if (s.scaleLock.active() && (algo == (std::uint8_t) Algo::PitchRise || algo == (std::uint8_t) Algo::PitchFall)
        && plan.recipe.pitchSemis != 0)
        setup.pitchOverride = plan.recipe.pitchSemis;
}

} // namespace gapfill::scale
