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
        for (auto& processor : processors) processor.prepare (sampleRate);
        reset();
    }
    void reset() noexcept
    {
        for (auto& processor : processors) { processor.reset(); processor.release(); }
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
