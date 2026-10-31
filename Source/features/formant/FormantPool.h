#pragma once

#include "FormantProcessor.h"

namespace gapfill::formant
{

/** Audio-thread-only ownership; counters may be observed by the GUI. */
class FormantPool
{
public:
    void prepare (double sampleRate)
    {
        // Every voice (including preview) must release before changing plans.
        for (auto& processor : processors)
        {
            jassert (! processor.isClaimed());
            processor.prepare (sampleRate);
        }
        reset();
    }
    void reset() noexcept
    {
        // FillEngine resets natural voices first, but E-1 deliberately lets an
        // active preview finish its 48-sample fade. Its processor is still
        // owned: retain both the claim and FFT history until that voice exits.
        for (auto& processor : processors)
            if (! processor.isClaimed())
                processor.reset();
        shortSkipped.store (0, std::memory_order_relaxed);
        busySkipped.store (0, std::memory_order_relaxed);
        claims.store (0, std::memory_order_relaxed);
    }
    FormantProcessor* request (const FormantSetup& setup, int plannedSamples, int minimumSourceSamples) noexcept
    {
        if (! setup.enabled || setup.amount01 <= 0.0f) return nullptr;
        if (plannedSamples < minimumSourceSamples + processors[0].latencySamples())
        {
            shortSkipped.fetch_add (1, std::memory_order_relaxed);
            return nullptr;
        }
        for (auto& processor : processors)
            if (processor.claim (setup))
            {
                claims.fetch_add (1, std::memory_order_relaxed);
                return &processor;
            }
        busySkipped.fetch_add (1, std::memory_order_relaxed);
        return nullptr;
    }
    /** Audio-thread-only observation; not a cross-thread GUI counter. */
    int activeClaimCount() const noexcept
    {
        int n = 0;
        for (const auto& processor : processors) n += processor.isClaimed() ? 1 : 0;
        return n;
    }
    std::uint64_t skippedShort() const noexcept { return shortSkipped.load (std::memory_order_relaxed); }
    std::uint64_t skippedBusy() const noexcept { return busySkipped.load (std::memory_order_relaxed); }
    std::uint64_t claimCount() const noexcept { return claims.load (std::memory_order_relaxed); }
    std::uint64_t faultCount() const noexcept
    {
        return processors[0].faultCount() + processors[1].faultCount();
    }
    std::size_t preallocatedBytes() const noexcept
    {
        return processors[0].preallocatedBytes() + processors[1].preallocatedBytes();
    }

private:
    std::array<FormantProcessor, 2> processors;
    std::atomic<std::uint64_t> shortSkipped { 0 }, busySkipped { 0 }, claims { 0 };
};

} // namespace gapfill::formant
