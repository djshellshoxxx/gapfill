#include "VoiceFxChain.h"

#include "../../dsp/DspUtils.h"

namespace gapfill::fx
{

VoiceFxChain::VoiceFxChain()
{
    build();
}

void VoiceFxChain::build()
{
    // Permanent order (docs/dev/12 FX-0): ratchet -> tapedestr -> formant -> rcrush.
    // F-L20 inserts the formant card between tape destruction and rate crush.
    count = 0;
    cards[(size_t) count++] = &ratchet;
    cards[(size_t) count++] = &tape;
    cards[(size_t) count++] = &rcrush;
}

void VoiceFxChain::prepare (double sampleRate, int maxBlock, int channels)
{
    sr = sampleRate;
    endGuard = std::max (16, (int) (sampleRate * 0.0015));

    for (int i = 0; i < count; ++i)
        cards[(size_t) i]->prepare (sampleRate, maxBlock, channels);

    reset();
}

void VoiceFxChain::reset()
{
    for (int i = 0; i < count; ++i)
        cards[(size_t) i]->reset();

    cardActive.fill (false);
    active = false;
}

void VoiceFxChain::start (const VoiceFxStart& st)
{
    active = false;

    for (int i = 0; i < count; ++i)
    {
        const bool use = ! st.skipVoiceFx && cards[(size_t) i]->enabled (st.settings)
                           && cards[(size_t) i]->startFill (st);
        cardActive[(size_t) i] = use;
        active = active || use;
    }

    if (meters != nullptr && cardActive[0])
        meters->tempoFallback.store (ratchet.currentPlan().tempoFallback, std::memory_order_relaxed);
}

void VoiceFxChain::fadeOut (int samples)
{
    for (int i = 0; i < count; ++i)
        if (cardActive[(size_t) i])
            cards[(size_t) i]->fadeOut (samples);
}

void VoiceFxChain::process (float* l, float* r, int n, int samplesLeft, int voiceLength) noexcept
{
    if (! active || n <= 0)
        return;

    VoiceFxContext ctx;
    ctx.sampleRate = sr;
    ctx.samplesLeft = samplesLeft;
    ctx.voiceLength = voiceLength;

    const bool timed = meters != nullptr && (timingCounter++ & 63u) == 0;
    const auto t0 = timed ? juce::Time::getHighResolutionTicks() : 0;

    for (int i = 0; i < count; ++i)
    {
        if (! cardActive[(size_t) i])
            continue;

        cards[(size_t) i]->processVoice (l, r, n, ctx);

        // Output of every card is sanitised and hard-limited at +12 dBFS.
        for (int s = 0; s < n; ++s)
        {
            l[s] = sanitise (l[s], 3.98f);
            r[s] = sanitise (r[s], 3.98f);
        }
    }

    // Short fade over the last samples of the voice so delaying cards can never
    // leave a step where the voice is cut off.
    if (samplesLeft < n + endGuard)
    {
        for (int s = 0; s < n; ++s)
        {
            const auto left = samplesLeft + (n - 1 - s);   // samples remaining after sample s
            if (left < endGuard)
            {
                const auto g = (float) left / (float) endGuard;
                l[s] *= g;
                r[s] *= g;
            }
        }
    }

    if (timed)
    {
        const auto ticks = juce::Time::getHighResolutionTicks() - t0;
        const auto us = (float) (juce::Time::highResolutionTicksToSeconds (ticks) * 1.0e6);

        // Attribute the sampled call to the first active card (the chain is
        // cheap enough that per-card timing would cost more than it measures).
        for (int i = 0; i < count; ++i)
        {
            if (! cardActive[(size_t) i])
                continue;

            const auto idx = (int) cards[(size_t) i]->id();
            meters->cpuMicros[idx].store (us, std::memory_order_relaxed);

            if (us > meters->cpuWorstMicros[idx].load (std::memory_order_relaxed))
                meters->cpuWorstMicros[idx].store (us, std::memory_order_relaxed);

            break;
        }
    }
}

} // namespace gapfill::fx
