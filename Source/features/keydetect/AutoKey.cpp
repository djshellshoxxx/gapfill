#include "AutoKey.h"

namespace gapfill::keydetect
{

ResolvedKey resolveKey (int manualRoot, int manualScaleType, bool autoOn, bool hold,
                        const KeyEstimate& heldKey, const KeyEstimate& detected) noexcept
{
    ResolvedKey r;

    const auto manual = [&]
    {
        if (manualScaleType > 0 && manualRoot >= 0 && manualRoot < 12)
        {
            r.rootPc = manualRoot;
            r.scaleType = manualScaleType;
            r.source = KeySource::Manual;
        }
    };

    const auto fromEstimate = [&] (const KeyEstimate& e, KeySource src)
    {
        r.rootPc = e.tonic;
        r.scaleType = e.mode == 0 ? 1 : 2;               // Major / Natural Minor
        r.source = src;
    };

    if (! autoOn)
        manual();
    else if (hold && heldKey.valid && heldKey.tonic >= 0)
        fromEstimate (heldKey, KeySource::Held);
    else if (detected.valid && detected.tonic >= 0)
        fromEstimate (detected, KeySource::Auto);
    else
        manual();

    return r;
}

void AutoKey::prepare (double sampleRate)
{
    sr = sampleRate;
    chroma.prepare (sampleRate);
    reset();
}

void AutoKey::reset() noexcept
{
    chroma.reset();
    hyst.reset();
    framesSinceEvaluation = 0;
    warmActive = false;
    wasHolding = false;
    published.store (pack ({}), std::memory_order_relaxed);
    held.store (pack ({}), std::memory_order_relaxed);
    running.store (false, std::memory_order_relaxed);
}

void AutoKey::requestWarmStart (const KeyEstimate& e) noexcept
{
    warmWord.store (pack (e), std::memory_order_relaxed);
    warmPending.store (true, std::memory_order_release);
}

void AutoKey::evaluateNow (const KeySettings& s) noexcept
{
    const auto d = evaluate (chroma.chroma(), chroma.tonalMass(), s.minConfidence01);

    // Warm start is replaced by evidence as soon as enough tonal material was heard.
    if (warmActive && chroma.tonalMass() >= 0.2f)
        warmActive = false;

    if (warmActive && ! s.hold)
        return;                                          // keep showing the saved key until evidence exists

    published.store (pack (hyst.update (d)), std::memory_order_relaxed);
}

void AutoKey::process (const float* mono, int numSamples, const KeySettings& s) noexcept
{
    if (resetRequested.exchange (false, std::memory_order_acq_rel))
        reset();

    if (warmPending.exchange (false, std::memory_order_acquire))
    {
        const auto e = unpack (warmWord.load (std::memory_order_relaxed));

        if (e.tonic >= 0)
        {
            hyst.seed (e);
            published.store (pack (e), std::memory_order_relaxed);
            warmActive = true;

            if (s.hold)
                held.store (pack (e), std::memory_order_relaxed);
        }
    }

    if (! s.autoOn)
    {
        if (running.load (std::memory_order_relaxed))
            reset();                                     // inactive: zero cost, nothing kept

        return;
    }

    running.store (true, std::memory_order_relaxed);
    chroma.setWindowSeconds (s.windowSeconds);

    // Hold copies the current valid estimate; releasing it forgets the copy.
    if (s.hold && ! wasHolding)
    {
        const auto cur = estimate();

        if (cur.valid && held.load (std::memory_order_relaxed) == pack ({}))
            held.store (pack (cur), std::memory_order_relaxed);
    }
    else if (! s.hold && wasHolding)
        held.store (pack ({}), std::memory_order_relaxed);

    wasHolding = s.hold;

    const int frames = chroma.process (mono, numSamples);
    framesSinceEvaluation += frames;

    while (framesSinceEvaluation >= kEvaluateEveryFrames)
    {
        framesSinceEvaluation -= kEvaluateEveryFrames;
        evaluateNow (s);
    }
}

} // namespace gapfill::keydetect
