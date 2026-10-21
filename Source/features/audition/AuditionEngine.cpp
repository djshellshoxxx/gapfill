// E-1 Fill Audition, engine side (docs/dev/03-SPEC-ESSENTIALS.md E-1).
//
// These are FillEngine members defined outside FillEngine.cpp so the hot file
// only gets hook lines. Everything here runs on the audio thread, allocates
// nothing and never touches the gap state machine: a preview must not change
// `inGap`, `gapEvaluated`, `gapIndex`, `lastFillStartPos`, `fillsStarted`,
// `recentAlgos`, the history ring or any later natural decision.

#include "../../dsp/FillEngine.h"
#include "../Salts.h"

namespace gapfill
{

void FillEngine::requestPreview (std::uint8_t velocity) noexcept
{
    preview.request = {};
    preview.request.pending = true;
    preview.request.velocity = velocity;
}

void FillEngine::requestPreviewRecipe (const FillRecipe& recipe, std::uint8_t velocity, int gainCentiDb) noexcept
{
    preview.request = {};
    preview.request.pending = true;
    preview.request.velocity = velocity;
    preview.request.hasRecipe = true;
    preview.request.recipe = recipe;
    preview.request.gainCentiDb = juce::jlimit (-600, 600, gainCentiDb);
}

FillEngine::GapBookkeeping FillEngine::getGapBookkeeping() const noexcept
{
    GapBookkeeping b;
    b.inGap = inGap;
    b.gapEvaluated = gapEvaluated;
    b.gapStartPos = gapStartPos;
    b.lastFillStartPos = lastFillStartPos;
    b.gapIndex = gapIndex;
    b.fillsStarted = fillsStarted;

    for (int i = 0; i < historyLength; ++i)
        b.recent[i] = recentAlgos[i];

    return b;
}

void FillEngine::resetPreview() noexcept
{
    // A reset fades the preview out instead of cutting it (docs/dev/03 E-1 item 4).
    preview.fadeOut (48);
    preview.request = {};
    preview.counter = 0;

    if (! preview.isActive())
    {
        preview.status = PreviewStatus::Idle;
        preview.algo = -1;
    }
}

void FillEngine::servePendingPreview (std::int64_t outputPos, const AnalysisEngine& analysis,
                                      const SourceHistory& history, const EngineSettings& s,
                                      FillEventQueue* events) noexcept
{
    if (! preview.request.pending)
        return;

    const auto request = preview.request;
    preview.request = {};

    startPreview (request.velocity, outputPos, analysis, history, s,
                  request.hasRecipe ? &request.recipe : nullptr, request.gainCentiDb, events);
}

bool FillEngine::startPreview (std::uint8_t velocity, std::int64_t outputPos,
                               const AnalysisEngine& analysis, const SourceHistory& history,
                               const EngineSettings& s, const FillRecipe* recipeIn,
                               int gainCentiDb, FillEventQueue* events) noexcept
{
    auto fail = [this] (PreviewStatus why)
    {
        preview.status = why;
        preview.algo = preview.isActive() ? preview.algo : -1;
        ++preview.seq;
        return false;
    };

    if (s.bypassed)
        return fail (PreviewStatus::RejectedBypassed);

    const auto& frame = analysis.frameAt (outputPos);

    // Same "nothing to sample" rule as checkEligibility.
    if (frame.reference < 3.0e-4f)
        return fail (PreviewStatus::RejectedNoSource);

    // Deterministic for a given seed: the counter is only restarted by reset().
    ++preview.counter;

    GapContext ctx;
    ctx.outputPos = outputPos;
    ctx.blockStartPos = outputPos;
    ctx.frame = &frame;
    ctx.conf = 1.0f;
    ctx.key = hashCombine (hashCombine ((std::uint64_t) s.seed * 0x9E3779B1ULL, (std::uint64_t) preview.counter),
                           kSalt_Preview);
    ctx.minGapSamples = (std::int64_t) juce::jmax (16.0, s.minGapMs * 0.001 * sr);
    ctx.maxFillSamples = (std::int64_t) juce::jmax (32.0, s.maxFillMs * 0.001 * sr);

    // Capped at min (Max Fill, 2000 ms), hard cap 5 s. A preview is not a gap, so
    // no next-event boundary and no pre-guard.
    const auto cap = (std::int64_t) (juce::jmin (kPreviewMaxSeconds, kPreviewHardCapSecs) * sr);
    ctx.available = ctx.usable = juce::jmin (ctx.maxFillSamples, cap);
    ctx.preGuard = 0;

    Rng rng;
    rng.seed (ctx.key);

    FillPlan plan;

    if (recipeIn != nullptr)
    {
        // E-1 addendum (U-1): replay that recipe, skipping algorithm / band / length selection.
        if (! isValid (*recipeIn))
            return fail (PreviewStatus::RejectedBuild);

        const auto lengthMs = juce::jmin ((float) recipeIn->lengthMs, (float) (kPreviewMaxSeconds * 1000.0));
        plan.recipe = *recipeIn;
        plan.algoIndex = recipeIn->algo;
        plan.band = recipeIn->band;
        plan.lengthSamples = juce::jmin (cap, (std::int64_t) ((double) juce::jmax (20.0f, lengthMs) * 0.001 * sr));
        plan.lengthMs = (float) (1000.0 * (double) plan.lengthSamples / sr);
        plan.recipe.lengthMs = (std::uint16_t) juce::jlimit (0, 65535, (int) std::lround (plan.lengthMs));
        plan.recipe.seed = (std::uint32_t) hashCombine (ctx.key, (std::uint64_t) (plan.algoIndex + 1) * 0x27D4EB2FULL);
        plan.recipe.flags = (std::uint8_t) (plan.recipe.flags | FillRecipe::flagFromLock);   // force modifiers / pitch
    }
    else
    {
        int band = -1;

        if (s.mode == DetMode::Spectral)
        {
            // Weighted by Band Probability, ignoring the live confidence.
            float weights[3] { 0.0f, 0.0f, 0.0f };
            float total = 0.0f;

            for (int b = 0; b < 3; ++b)
                if (s.bandOn[b])
                    total += (weights[b] = juce::jmax (0.02f, s.bandProb[b]));

            if (total <= 0.0f)
                return fail (PreviewStatus::RejectedNoBand);

            auto pick = rng.nextFloat() * total;
            band = 2;

            for (int b = 0; b < 3; ++b)
            {
                pick -= weights[b];

                if (pick <= 0.0f && weights[b] > 0.0f)
                {
                    band = b;
                    break;
                }
            }

            if (weights[band] <= 0.0f)
                for (int b = 2; b >= 0; --b)
                    if (weights[b] > 0.0f)
                    {
                        band = b;
                        break;
                    }
        }

        // The same stage as natural fills (so a fix in one is a fix in both), but a
        // preview never uses the Fill Lock substitution.
        auto previewSettings = s;
        previewSettings.perf.lockOn = false;

        if (buildRecipe (ctx, rng, previewSettings, band, plan) != RejectReason::None)
            return fail (PreviewStatus::RejectedBuild);
    }

    auto setup = toVoiceSetup (plan, ctx, s, history);
    plan.recipe.flags = (std::uint8_t) ((plan.recipe.flags & ~FillRecipe::flagFromLock) | FillRecipe::flagPreview);

    // Velocity maps to amplitude: 0.8 * lerp (0.5, 1.0, vel / 127); toVoiceSetup supplied the 0.8.
    setup.amp *= lerp (0.5f, 1.0f, juce::jlimit (0.0f, 1.0f, (float) velocity / 127.0f));

    // F-S06: the recipe A/B level-matching trim (0 for every other preview: x 1.0f is exact).
    if (gainCentiDb != 0)
        setup.amp *= std::pow (10.0f, (float) gainCentiDb / 2000.0f);

    // The previous preview fades out over 5 ms while the new one starts.
    preview.beginRestart (history);

    if (! startVoice (preview.voice, setup, plan, history))
        return fail (PreviewStatus::RejectedBuild);

    preview.beginMeasuring();

    preview.status = PreviewStatus::Playing;
    preview.algo = plan.algoIndex;
    ++preview.seq;

    if (events != nullptr)
    {
        FillEvent e;
        e.type = FillEventType::Preview;
        e.recipe = plan.recipe;
        e.pos = outputPos;
        e.lengthSamples = (std::int32_t) plan.lengthSamples;
        events->push (e);
    }

    return true;
}

} // namespace gapfill
