#pragma once

#include <juce_core/juce_core.h>
#include <mutex>

namespace gapfill::state
{
/** Last completely validated host-state blob. Non-realtime callers only.
    The mutex protects this cache, not the feature serializers or APVTS.
    Audio never acquires it. Host saves, restored-state snapshots and the
    message-thread diagnostics timer may acquire it. */
class SavedStateCache
{
public:
    enum class Fallback { CallerPreserved, CachedRestored, Unavailable };

    void remember (const juce::MemoryBlock& complete)
    {
        if (complete.getSize() == 0)
            return;
        const std::scoped_lock guard (mutex);
        lastGood = complete;
    }

    Fallback retainOnFailure (juce::MemoryBlock& destination) const
    {
        if (destination.getSize() != 0)
            return Fallback::CallerPreserved;
        const std::scoped_lock guard (mutex);
        if (lastGood.getSize() == 0)
            return Fallback::Unavailable;
        destination = lastGood;
        return Fallback::CachedRestored;
    }

private:
    mutable std::mutex mutex;
    juce::MemoryBlock lastGood;
};
}
