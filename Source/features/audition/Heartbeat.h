#pragma once

// E-1 "is the host really running audio?" check for editor-side triggers
// (docs/dev/03 E-1, Threading): MeterState::blockCounter must have advanced within
// the last kPreviewHeartbeatMs, otherwise nothing is pushed and the UI says
// "Start playback to audition". Message thread only.

#include "PreviewChannel.h"
#include "../../dsp/VisualisationState.h"

#include <juce_core/juce_core.h>

namespace gapfill::audition
{

class Heartbeat
{
public:
    /** Call from the editor timer (and before acting on a press). */
    void poll (const MeterState& meters) noexcept
    {
        const auto blocks = meters.blockCounter.load (std::memory_order_relaxed);

        if (blocks != lastBlocks)
        {
            lastBlocks = blocks;
            lastAdvanceMs = juce::jmax<std::uint32_t> (1u, juce::Time::getMillisecondCounter());
        }
    }

    bool isRunning() const noexcept
    {
        return lastAdvanceMs != 0 && (juce::Time::getMillisecondCounter() - lastAdvanceMs) <= kPreviewHeartbeatMs;
    }

private:
    std::uint32_t lastBlocks = 0;
    std::uint32_t lastAdvanceMs = 0;
};

} // namespace gapfill::audition
