#include "FxChain.h"

#include "../../dsp/DspUtils.h"

#include <algorithm>

namespace gapfill::fx
{

FxChain::FxChain()
{
    build();
}

void FxChain::build()
{
    // Permanent order (docs/dev/12 FX-0): resonator -> fsdelay -> dub -> convo ->
    // spfreeze. Each bus card (WP9) adds one explicit line here.
    count = 0;
    addBusCard (&busCards.formant);        // F-L20 (CardId order)
    addBusCard (&busCards.resonator);      // F-M15
    addBusCard (&busCards.fsdelay);        // F-L19
    addBusCard (&busCards.dub);            // F-L15
    addBusCard (&busCards.convo);          // F-L21
    addBusCard (&busCards.spfreeze);       // F-X02
}

bool FxChain::addBusCard (BusFx* card) noexcept
{
    if (card == nullptr || count >= kMaxBusCards)
        return false;

    cards[(size_t) count++] = card;
    return true;
}

void FxChain::prepare (double sampleRate, int maxBlock, int channels)
{
    sr = sampleRate;
    blockCapacity = std::max (1, maxBlock);
    duckBuf.assign ((size_t) blockCapacity, 1.0f);
    plain.setSize (2, blockCapacity, false, true, false);

    for (int i = 0; i < count; ++i)
        cards[(size_t) i]->prepare (sampleRate, maxBlock, channels);

    meters.reset();
    reset();
}

void FxChain::reset()
{
    for (int i = 0; i < count; ++i)
        cards[(size_t) i]->reset();

    plain.clear();
    std::fill (duckBuf.begin(), duckBuf.end(), 1.0f);
}

void FxChain::fadeOut (int samples)
{
    for (int i = 0; i < count; ++i)
        cards[(size_t) i]->fadeOut (samples);
}

bool FxChain::anyBusActive (const FxSettings& s) const noexcept
{
    for (int i = 0; i < count; ++i)
        if (cards[(size_t) i]->enabled (s) || ! cards[(size_t) i]->isIdle())
            return true;

    return false;
}

bool FxChain::needsSplit (const FxSettings& s) const noexcept
{
    return ! s.bandFx.allOn() && anyBusActive (s);
}

int FxChain::maxLatencySamples() const noexcept
{
    int m = 0;

    for (int i = 0; i < count; ++i)
        m = std::max (m, cards[(size_t) i]->latencySamples());

    return m;
}

EngineBlockIO FxChain::beginBlock (const FxSettings& s, int numSamples) noexcept
{
    EngineBlockIO io;

    if (! anyBusActive (s))
        return io;

    const auto n = std::min (numSamples, blockCapacity);

    io.duck = duckBuf.data();

    if (needsSplit (s))
    {
        plain.clear (0, n);
        plain.clear (1, 0, n);
        io.plainWet = &plain;
    }

    return io;
}

void FxChain::processBus (juce::AudioBuffer<float>& wet, int start, int n, const FxSettings& s,
                          const EngineBlockIO& io, std::int64_t blockStartPos) noexcept
{
    if (n <= 0 || ! anyBusActive (s))
        return;                                  // identity: nothing published, nothing touched

    BusFxContext ctx;
    ctx.sampleRate = sr;
    ctx.fillStarts = io.starts;
    ctx.numStarts = io.numStarts;
    ctx.blockStartPos = blockStartPos;
    ctx.duck = io.duck;
    ctx.settings = &s;
    ctx.samplesPerBeat = io.samplesPerBeat;
    ctx.scaleMask = io.scaleMask;
    ctx.scaleRoot = io.scaleRoot;
    ctx.history = io.history;
    ctx.historyOffset = io.historyOffset;

    const bool timed = (timingCounter++ & 63u) == 0;
    const auto t0 = timed ? juce::Time::getHighResolutionTicks() : 0;

    for (int i = 0; i < count; ++i)
    {
        auto* card = cards[(size_t) i];

        if (! card->enabled (s) && card->isIdle())
            continue;

        card->processBus (wet, start, n, ctx);

        // +12 dBFS hard ceiling, NaN guard
        for (int ch = 0; ch < wet.getNumChannels(); ++ch)
        {
            auto* d = wet.getWritePointer (ch, start);

            for (int k = 0; k < n; ++k)
                d[k] = sanitise (d[k], 3.98f);
        }

        if (timed)
        {
            const auto us = (float) (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1.0e6);
            const auto idx = (int) card->id();
            meters.cpuMicros[idx].store (us, std::memory_order_relaxed);

            if (us > meters.cpuWorstMicros[idx].load (std::memory_order_relaxed))
                meters.cpuWorstMicros[idx].store (us, std::memory_order_relaxed);
        }
    }

    if (io.plainWet != nullptr)
    {
        for (int ch = 0; ch < std::min (wet.getNumChannels(), io.plainWet->getNumChannels()); ++ch)
            juce::FloatVectorOperations::add (wet.getWritePointer (ch, start),
                                              io.plainWet->getReadPointer (ch), n);
    }
}

std::size_t FxChain::preallocatedBytes() const noexcept
{
    return (std::size_t) plain.getNumChannels() * (std::size_t) plain.getNumSamples() * sizeof (float)
         + duckBuf.size() * sizeof (float);
}

} // namespace gapfill::fx
