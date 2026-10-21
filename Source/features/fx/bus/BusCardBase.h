#pragma once

// Common scaffolding of the WP9 bus cards (docs/dev/12 section 7).
//
// The base class owns what every bus card must do the same way:
//  * the 20 ms Amount ramp (an "off" card is never called by the chain; a card
//    that is switched off ramps to 0, resets its state and reports isIdle),
//  * panic / fadeOut (mutes the effect contribution within the requested time and
//    clears the tail; a new fill start un-mutes),
//  * the per-card tail cap (when the input has been silent for tailCapMs the
//    effect contribution is faded out and the state cleared),
//  * the engine duck (renderChunk receives it per sample so the tail of the
//    effect stays out of the protected region before the next hit),
//  * chunking (renderChunk never sees more than kChunk samples).
//
// A card implements amountOf / renderChunk (and optionally the hooks below).

#include "BusDsp.h"
#include "../EffectCard.h"

#include <array>
#include <atomic>

namespace gapfill::fx::bus
{

class BusCardBase : public BusFx
{
public:
    static constexpr int kChunk = 256;

    void prepare (double sampleRate, int maxBlock, int channels) override
    {
        sr = sampleRate;
        blockMax = std::max (1, maxBlock);
        numCh = std::max (1, channels);
        rampSamples = std::max (1, (int) (0.020 * sr));
        tailCapSamples = (std::int64_t) (tailCapMs() * 0.001 * sr);
        prepareCard();
        reset();
    }

    void reset() override
    {
        amount.snap (0.0f);
        muted = false;
        silentSamples = 0;
        tailGate.snap (1.0f);
        resetCard();
    }

    void fadeOut (int samples) override
    {
        muted = true;
        panicSamples = std::max (1, samples);
        panicPending = true;
    }

    bool enabled (const FxSettings& s) const noexcept override { return amountOf (s) > kOffThreshold; }

    bool isIdle() const noexcept override { return amount.cur <= 1.0e-5f && amount.target <= 1.0e-5f; }

    void processBus (juce::AudioBuffer<float>& wet, int start, int n, const BusFxContext& ctx) noexcept final
    {
        noteCall();

        if (wet.getNumChannels() < 2 || n <= 0)
            return;

        juce::ScopedNoDenormals noDenormals;

        FxSettings fallback;
        const auto& s = ctx.settings != nullptr ? *ctx.settings : fallback;

        if (ctx.numStarts > 0)
            muted = false;

        blockSettings (s, ctx);

        auto* l = wet.getWritePointer (0, start);
        auto* r = wet.getWritePointer (1, start);

        for (int pos = 0; pos < n; pos += kChunk)
        {
            const auto len = std::min (kChunk, n - pos);
            const auto target = (muted || ! enabled (s)) ? 0.0f : amountOf (s);

            if (panicPending)
            {
                amount.setTarget (0.0f, panicSamples);
                panicPending = false;
            }
            else if (! muted || amount.target != 0.0f)
            {
                amount.setTarget (target, muted ? panicSamples : rampSamples);
            }

            // tail cap: silent input for tailCapMs fades the contribution out
            float peak = 0.0f;

            for (int i = 0; i < len; ++i)
                peak = std::max (peak, std::max (std::abs (l[pos + i]), std::abs (r[pos + i])));

            if (peak > 1.0e-4f)
                silentSamples = 0;
            else
                silentSamples += len;

            // F-D03: Balanced / Lite shorten every tail cap (x0.5 / x0.25); Full is exactly the card's own cap.
            const auto cap = s.tailCapScale == 1.0f ? tailCapSamples : (std::int64_t) ((double) tailCapSamples * (double) s.tailCapScale);
            const bool capped = cap > 0 && silentSamples > cap;
            tailGate.setTarget (capped ? 0.0f : 1.0f, capped ? std::max (1, (int) (0.25 * sr)) : std::max (1, (int) (0.005 * sr)));

            for (int i = 0; i < len; ++i)
                amt[(size_t) i] = amount.next() * tailGate.next();

            renderChunk (l + pos, r + pos, len, amt.data(),
                         ctx.duck != nullptr ? ctx.duck + start + pos : nullptr, ctx, s);

            if (capped && tailGate.cur <= 1.0e-4f)
            {
                resetCard();
                tailGate.snap (0.0f);
            }

            if (amount.settled() && amount.cur <= 1.0e-5f && amount.target <= 1.0e-5f)
            {
                resetCard();
                amount.snap (0.0f);
            }
        }
    }

protected:
    /** Effect strength 0..1 for these settings (card is "enabled" when this exceeds kOffThreshold). */
    virtual float amountOf (const FxSettings&) const noexcept = 0;

    virtual void prepareCard() {}
    virtual void resetCard() noexcept {}

    /** Once per processBus call, before the first chunk (read settings, fill starts, capture requests). */
    virtual void blockSettings (const FxSettings&, const BusFxContext&) noexcept {}

    /** In-place; amt[i] is the smoothed Amount (0..1) already multiplied by the tail gate;
        duck (nullable) is the engine duck for the same samples. */
    virtual void renderChunk (float* l, float* r, int n, const float* amt, const float* duck,
                              const BusFxContext&, const FxSettings&) noexcept = 0;

    double sr = 48000.0;
    int blockMax = 512;
    int numCh = 2;
    int rampSamples = 960;

private:
    std::array<float, kChunk> amt {};
    Ramp amount, tailGate;
    bool muted = false;
    bool panicPending = false;
    int panicSamples = 192;
    std::int64_t silentSamples = 0;
    std::int64_t tailCapSamples = 0;
};

} // namespace gapfill::fx::bus
