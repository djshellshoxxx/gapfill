#include "SceneOverlay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>

namespace gapfill::scenes
{

namespace
{
    constexpr int kMaxTargets = 1024;
    std::array<targets::Ref, kMaxTargets> targetTable {};
    std::once_flag targetsFlag;
}

void initOverlayTargets()
{
    std::call_once (targetsFlag, []
    {
        const auto n = std::min (numParams(), kMaxTargets);

        for (int i = 0; i < n; ++i)
            targetTable[(size_t) i] = targets::refForId (paramIdAt (i).toRawUTF8());
    });
}

void OverlayView::applyTo (EngineSettings& s) const noexcept
{
    for (int i = 0; i < n; ++i)
        if ((targets::macroBit (refs[i]) & s.perf.macroOwnedMask) == 0)    // macros own their targets
            targets::apply (s, refs[i], real[i]);
}

//==============================================================================
namespace quantiser
{
    double beatLength (int tsDen) noexcept
    {
        return 4.0 / (tsDen > 0 ? tsDen : 4);
    }

    double barLength (int tsNum, int tsDen) noexcept
    {
        return (tsNum > 0 ? tsNum : 4) * beatLength (tsDen);
    }

    double nextBoundary (Quant mode, double nowPpq, double guardQuarters, int tsNum, int tsDen,
                         int phraseBars, int phraseOffsetBars) noexcept
    {
        const auto t = nowPpq + guardQuarters;
        const auto eps = 1.0e-9;

        if (mode == Quant::Beat)
        {
            const auto len = beatLength (tsDen);
            return (std::floor (t / len + eps) + 1.0) * len;
        }

        const auto bar = barLength (tsNum, tsDen);
        const auto firstBar = (long long) std::floor (t / bar + eps) + 1;

        if (mode == Quant::Phrase && phraseBars > 0)
        {
            const long long p = phraseBars;
            const long long off = ((phraseOffsetBars % p) + p) % p;
            const long long delta = (((off - firstBar) % p) + p) % p;
            return (double) (firstBar + delta) * bar;
        }

        return (double) firstBar * bar;
    }
}

//==============================================================================
SceneEngine::SceneEngine()
{
    reset();
}

void SceneEngine::reset() noexcept
{
    phase = Phase::None;
    currentView.clear();
    lastEndPpq = -1.0;
    lastValid = false;
    uiBoundary.store (-1.0, std::memory_order_relaxed);
    uiPending.store (0, std::memory_order_relaxed);
}

void SceneEngine::cancelPending() noexcept
{
    if (phase == Phase::Pending)
    {
        phase = Phase::None;
        currentView.clear();
        uiBoundary.store (-1.0, std::memory_order_relaxed);
        uiPending.store (0, std::memory_order_relaxed);
    }
}

void SceneEngine::load (const Overlay& o) noexcept
{
    const int n = std::min<int> (o.n, kMaxOverlayEntries);
    currentView.n = n;

    for (int i = 0; i < n; ++i)
    {
        currentView.refs[i] = o.idx[i] < kMaxTargets ? targetTable[o.idx[i]] : targets::Ref {};
        currentView.real[i] = o.real[i];
    }
}

void SceneEngine::arm (const TransportSnap& t, Result& r) noexcept
{
    const auto mode = (Quant) active.quant;
    currentView.active = true;

    if (mode == Quant::Immediate || mode == Quant::Inherit)
    {
        currentView.immediate = true;
        return;
    }

    if (! t.valid)
    {
        if (active.waitForTransport == 0)
        {
            currentView.immediate = true;      // "No transport - switching now"
            r.usedTransportFallback = true;
        }
        else
        {
            currentView.immediate = false;
            currentView.boundaryPpq = 1.0e300; // never, until a transport exists
        }

        return;
    }

    currentView.immediate = false;
    const auto guard = 0.001 * t.sampleRate / std::max (1.0, t.samplesPerBeat);
    currentView.boundaryPpq = quantiser::nextBoundary (mode, t.ppq, guard, t.tsNum, t.tsDen,
                                                       t.phraseBars, t.phraseOffset);
}

SceneEngine::Result SceneEngine::beginChunk (TripleBuffer<Overlay>& pub, const TransportSnap& t) noexcept
{
    Result r;

    if (pub.consume (incoming))
    {
        switch ((Overlay::Kind) incoming.kind)
        {
            case Overlay::Cancel:
                cancelPending();
                break;

            case Overlay::Clear:
                if (phase != Phase::None && incoming.id == active.id)
                {
                    phase = Phase::None;
                    currentView.clear();
                }
                break;

            case Overlay::Set:
                if (phase != Phase::None && incoming.id == active.id)
                {
                    // Same request, fewer entries (a manual edit removed one):
                    // keep the boundary, replace the content.
                    active = incoming;
                    load (active);
                }
                else
                {
                    active = incoming;
                    currentView.clear();
                    load (active);
                    phase = Phase::Pending;
                    arm (t, r);
                }
                break;
        }
    }

    if (phase == Phase::Pending)
    {
        // Loop / seek with a pending change: re-arm from the new position, never drop.
        if (t.valid && lastValid && ! currentView.immediate)
        {
            const auto backwards = t.ppq < lastEndPpq - 1.0e-6;
            const auto jumped = t.ppq > lastEndPpq + 1.0;

            if (backwards || jumped)
                arm (t, r);
        }

        if (! t.valid && ! currentView.immediate && active.waitForTransport == 0)
        {
            currentView.immediate = true;
            r.usedTransportFallback = true;
        }

        if (t.valid && currentView.boundaryPpq >= 1.0e299 && ! currentView.immediate)
            arm (t, r);                        // transport appeared while waiting

        const auto endPpq = t.valid ? t.ppq + (double) t.numSamples / std::max (1.0, t.samplesPerBeat) : 0.0;

        if (currentView.immediate || (t.valid && endPpq > currentView.boundaryPpq))
        {
            phase = Phase::Applied;
            r.appliedId = active.id;
            uiBoundary.store (-1.0, std::memory_order_relaxed);
            uiPending.store (0, std::memory_order_relaxed);
        }
        else
        {
            uiBoundary.store (currentView.boundaryPpq, std::memory_order_relaxed);
            uiPending.store (active.id, std::memory_order_relaxed);
        }
    }

    lastValid = t.valid;
    lastEndPpq = t.valid ? t.ppq + (double) t.numSamples / std::max (1.0, t.samplesPerBeat) : -1.0;
    return r;
}

} // namespace gapfill::scenes
