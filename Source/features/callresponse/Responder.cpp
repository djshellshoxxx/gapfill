#include "Responder.h"

#include <algorithm>
#include <cmath>

namespace gapfill::callresponse
{

namespace
{
    // [motif][candidate]: 2 primary complement, 1 secondary, 0 other (the motif family itself is handled apart).
    constexpr float kComplement[4][4] =
    {
        /* Rhythmic    */ { 0.0f, 2.0f, 1.0f, 0.0f },
        /* Textural    */ { 2.0f, 0.0f, 0.0f, 1.0f },
        /* PitchTape   */ { 1.0f, 0.0f, 0.0f, 2.0f },
        /* Destructive */ { 0.0f, 1.0f, 2.0f, 0.0f },
    };
}

float complementMultiplier (AlgoFamily motif, AlgoFamily candidate) noexcept
{
    return kComplement[(int) motif][(int) candidate];
}

std::uint32_t packStatus (const ResponderStatus& s) noexcept
{
    return (std::uint32_t) s.state | ((std::uint32_t) std::clamp (s.k, 0, 7) << 2) | ((std::uint32_t) std::clamp (s.n, 0, 7) << 5)
         | ((std::uint32_t) std::clamp (s.confidencePercent, 0, 100) << 8) | ((std::uint32_t) s.family << 15)
         | ((s.off ? 0u : 1u) << 17);
}

ResponderStatus unpackStatus (std::uint32_t w) noexcept
{
    ResponderStatus s;
    s.state = (ResponderState) std::min<std::uint32_t> (2u, w & 3u);
    s.k = (int) ((w >> 2) & 7u);
    s.n = (int) ((w >> 5) & 7u);
    s.confidencePercent = (int) std::min<std::uint32_t> (100u, (w >> 8) & 127u);
    s.family = (AlgoFamily) ((w >> 15) & 3u);
    s.off = ((w >> 17) & 1u) == 0;
    return s;
}

void Responder::configure (const ResponderSettings& s, bool shouldPause) noexcept
{
    const bool modeChanged = s.mode != cfg.mode || s.calls != cfg.calls || s.replies != cfg.replies;
    cfg = s;
    cfg.calls = std::clamp (cfg.calls, 2, kMaxCalls);
    cfg.replies = std::clamp (cfg.replies, 1, 4);
    cfg.strength01 = std::clamp (cfg.strength01, 0.0f, 1.0f);
    paused = shouldPause;

    if (modeChanged)
        reset();
    else
        publish();
}

void Responder::reset() noexcept
{
    state = ResponderState::Collecting;
    collected = repliesLeft = repliesDone = 0;
    currentMotif = {};
    publish();
}

void Responder::onNaturalFillStarted (const FillRecipe& r, bool transportValid, double ppq, std::int64_t outputPos,
                                      double sampleRate) noexcept
{
    if (cfg.mode == 0 || paused)
        return;

    if (state == ResponderState::Responding)
    {
        ++repliesDone;

        if (--repliesLeft <= 0)
        {
            state = ResponderState::Collecting;                // response fills never join the next call
            collected = 0;
            repliesDone = 0;
        }

        publish();
        return;
    }

    CallEntry e;
    e.algo = r.algo;
    e.band = r.band;
    e.lengthClass = r.lengthClass;
    e.hasBeat = transportValid;
    e.startBeat = ppq;
    e.startSeconds = sampleRate > 0.0 ? (double) outputPos / sampleRate : 0.0;

    if (collected >= cfg.calls)                                 // sliding window while unclear
    {
        for (int i = 1; i < cfg.calls; ++i)
            window[i - 1] = window[i];

        collected = cfg.calls - 1;
    }

    window[collected++] = e;

    if (collected >= cfg.calls)
    {
        currentMotif = describe (window, collected);

        if (currentMotif.confidence >= kMotifGate)
        {
            state = ResponderState::Responding;
            repliesLeft = cfg.replies;
            repliesDone = 0;
        }
        else
            state = ResponderState::Unclear;
    }
    else
        state = ResponderState::Collecting;

    publish();
}

float Responder::algoFactor (int algo) const noexcept
{
    if (! active() || algo < 0 || algo >= numAlgos)
        return 1.0f;

    const auto s = cfg.strength01;
    const auto g = kAlgoFamilies[(size_t) algo];
    const auto f = currentMotif.family;
    float factor;

    if (cfg.mode == 1)                                          // Complement
    {
        factor = g == f ? 1.0f - 0.7f * s : 1.0f + s * kComplement[(int) f][(int) g];

        if (currentMotif.pitchDir > 0 && algo == (int) Algo::PitchFall)
            factor *= 1.0f + s;
        else if (currentMotif.pitchDir < 0 && algo == (int) Algo::PitchRise)
            factor *= 1.0f + s;
    }
    else                                                        // Echo
        factor = g == f ? 1.0f + 2.0f * s : 1.0f - 0.3f * s;

    return factor;
}

float Responder::bandFactor (int band) const noexcept
{
    if (! active() || band < 0 || currentMotif.band < 0)
        return 1.0f;

    const auto s = cfg.strength01;

    if (cfg.mode == 1)
        return band != currentMotif.band ? 1.0f + s : 1.0f;

    return band == currentMotif.band ? 1.0f + s : 1.0f;
}

float Responder::lengthScale() const noexcept
{
    if (! active())
        return 1.0f;

    const auto s = cfg.strength01;

    if (cfg.mode == 1)
        return currentMotif.meanLengthClass <= 1 ? 1.0f + 0.3f * s : 1.0f - 0.3f * s;

    // Echo: pull toward the motif's class midpoint (0.125 .. 0.875 of the longest fill) by 0.5 * strength.
    const float mid = 0.125f + 0.25f * (float) currentMotif.meanLengthClass;
    return 1.0f + 0.5f * s * (mid - 0.5f);
}

ResponderStatus Responder::status() const noexcept
{
    ResponderStatus s;
    s.off = cfg.mode == 0;
    s.state = state;
    s.family = currentMotif.family;
    s.confidencePercent = (int) std::lround (currentMotif.confidence * 100.0f);

    if (state == ResponderState::Responding)
    {
        s.k = repliesDone + 1;
        s.n = cfg.replies;
    }
    else
    {
        s.k = collected;
        s.n = cfg.calls;
    }

    return s;
}

void Responder::publish() noexcept
{
    word.store (packStatus (status()), std::memory_order_relaxed);
}

} // namespace gapfill::callresponse
