#pragma once
#include "ControllerTypes.h"
#include <array>
#include <cstddef>
#include <span>

namespace gapfill::controller
{
/** Fixed-capacity, message-thread feedback policy. Clock values are monotonic
    milliseconds supplied by the adapter; send returning false retains dirty state.
    At most 30Hz/address, 512 messages/s, 250ms CC echo suppression. A full
    80-output resync is spread over <=200ms at a regular 30Hz service cadence.
    No device, host, JUCE, allocation, or global clock dependency. */
class FeedbackCore
{
public:
    bool configure (std::span<const OutEvent>) noexcept;
    void setValue (std::size_t index, std::uint8_t value) noexcept;
    void noteInput (std::uint8_t channel, std::uint8_t cc, double nowMs) noexcept;
    void resync (double nowMs) noexcept;
    int tick (double nowMs, bool (*send) (void*, OutEvent) noexcept, void* context) noexcept;
    std::size_t size() const noexcept { return count; }
private:
    struct Entry
    {
        OutEvent value;
        int lastSent = -1;
        double lastSentMs = -1.0e9;
        double receivedMs = -1.0e9;
    };
    std::array<Entry, kMaxFeedbackOutputs> entries {};
    std::size_t count = 0, cursor = 0;
    double tokens = 0.0, refillMs = -1.0;
    std::array<double, 512> sentTimes {};
    std::array<double, 128> lastCcSent {};
    std::size_t sentCursor = 0;
};
}
