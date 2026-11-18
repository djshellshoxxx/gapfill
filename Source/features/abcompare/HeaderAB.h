#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <atomic>

namespace gapfill::abcompare
{

/** Processor-owned sound snapshots shared by header controls and MIDI. All
    tree operations are message-thread only. Host restore may requestReset(). */
class HeaderAB
{
public:
    /** Configure once before the first snapshot. These parameter nodes never
        enter the A/B sound snapshots; ModelVersions and other sound metadata do. */
    void setPerformanceIds (juce::StringArray ids) { performanceIds = std::move (ids); }
    void requestReset() noexcept { resetRequested.store (true, std::memory_order_release); }

    void synchronize (const juce::ValueTree& current)
    {
        if (resetRequested.exchange (false, std::memory_order_acq_rel) || ! a.isValid() || ! b.isValid())
        {
            a = copySound (current);
            b = a.createCopy();
            sideB = false;
        }
    }

    juce::ValueTree swap (const juce::ValueTree& current)
    {
        captureActive (current);
        sideB = ! sideB;
        return snapshot (sideB).createCopy();
    }

    void captureActive (const juce::ValueTree& current)
    {
        synchronize (current);
        (sideB ? b : a) = copySound (current);
    }

    void copyToOther (const juce::ValueTree& current)
    {
        captureActive (current);
        (sideB ? a : b) = copySound (current);
    }

    bool isUsingB() const noexcept { return sideB; }
    const juce::ValueTree& snapshot (bool useB) const noexcept { return useB ? b : a; }

   #ifdef GAPFILL_UNIT_TESTS
    void setSnapshotsForTests (const juce::ValueTree& first, const juce::ValueTree& second)
    {
        a = first.createCopy(); b = second.createCopy(); sideB = false;
        resetRequested.store (false, std::memory_order_release);
    }
   #endif

private:
    juce::ValueTree copySound (const juce::ValueTree& state) const
    {
        auto copy = state.createCopy();
        for (int i = copy.getNumChildren(); --i >= 0;)
            if (performanceIds.contains (copy.getChild (i).getProperty ("id").toString()))
                copy.removeChild (i, nullptr);
        return copy;
    }

    juce::ValueTree a, b;
    juce::StringArray performanceIds;
    bool sideB = false;
    std::atomic<bool> resetRequested { false };
};

} // namespace gapfill::abcompare
