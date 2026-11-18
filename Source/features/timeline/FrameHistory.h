#pragma once

// F-M03 message-thread history. The input is a trivially-copyable frame adapter;
// no audio, GUI or JUCE types are owned here. Configure before ingestion.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "../../dsp/VisualisationRate.h"

namespace gapfill::timeline
{

class FrameHistory
{
public:
    struct Column
    {
        float peak = 0.0f, confidence = 0.0f, energy = 0.0f;
        float bandConfidence[3] {};
        float duck = 1.0f;
        std::int64_t position = 0;
        std::int32_t ppqTick = INT32_MIN;
        std::uint32_t periodSamples = 64;
        std::uint8_t flags = 0, algorithm = 0xff, band = 0xff, reason = 0;
        bool incompleteBefore = false;
        std::uint8_t timeSigNumerator = 0, timeSigDenominator = 0;
        std::uint8_t protectionKinds = 0;
    };

    static constexpr int maxColumns = 24000;
    static constexpr std::uint8_t discontinuityFlag = 1u << 7;

    /** Message thread only. Existing samples are discarded when the coordinate
        system changes, avoiding interpolation between sample rates. */
    void configure (double sampleRate, double seconds)
    {
        rate = std::isfinite (sampleRate) ? std::clamp (sampleRate, 8000.0, (double) UINT32_MAX) : 48000.0;
        windowSeconds = std::isfinite (seconds) ? std::clamp (seconds, 4.0, 16.0) : 8.0;
        const auto period = visualisation::periodForRate (rate);
        const auto wanted = (int) std::clamp (std::ceil (rate * windowSeconds / (double) period), 1.0, (double) maxColumns);
        ring.assign ((std::size_t) wanted, Column {});
        clear();
        haveAnchor = false;
    }

    /** Input supplies peak/conf/energy/bandConf/duck, posLo/ppqTick/periodSamples
        and flags/algo/band/reason. All work is bounded; no allocation occurs. */
    template <typename Frame>
    void push (const Frame& frame) noexcept
    {
        if (ring.empty())
            return;

        const auto period = std::max (std::uint32_t { 1 }, frame.periodSamples);
        const auto delta = frame.posLo - lastPositionLo;
        const bool seam = (frame.flags & discontinuityFlag) != 0;
        const bool loss = havePosition && ! seam && delta != lastPeriod;

        if (! havePosition || seam)
        {
            newestPosition = (std::int64_t) frame.posLo;
            if (haveAnchor)
            {
                newestPosition = (anchor & ~std::int64_t { 0xffffffffll }) | frame.posLo;
                if (newestPosition - anchor > INT32_MAX)
                    newestPosition -= 0x100000000ll;
                else if (anchor - newestPosition > INT32_MAX)
                    newestPosition += 0x100000000ll;
            }
        }
        else
            newestPosition += (std::int64_t) (std::int32_t) delta;

        if (seam)
        {
            head = 0;
            count = 0;
        }

        Column column;
        column.peak = frame.peak;
        column.confidence = frame.conf;
        column.energy = frame.energy;
        std::copy (frame.bandConf, frame.bandConf + 3, column.bandConfidence);
        column.duck = frame.duck;
        column.position = newestPosition;
        column.ppqTick = frame.ppqTick;
        column.periodSamples = period;
        column.flags = frame.flags;
        column.algorithm = frame.algo;
        column.band = frame.band;
        column.reason = frame.reason;
        column.incompleteBefore = loss;
        if constexpr (requires { frame.timeSigNumerator; frame.timeSigDenominator; })
        {
            column.timeSigNumerator = frame.timeSigNumerator;
            column.timeSigDenominator = frame.timeSigDenominator;
        }
        if constexpr (requires { frame.protectionKinds; })
            column.protectionKinds = frame.protectionKinds;

        if (count == (int) ring.size())
        {
            ring[(std::size_t) head] = column;
            head = (head + 1) % (int) ring.size();
        }
        else
        {
            ring[(std::size_t) ((head + count) % (int) ring.size())] = column;
            ++count;
        }

        lastPositionLo = frame.posLo;
        lastPeriod = period;
        havePosition = true;

        const auto oldestAllowed = newestPosition - (std::int64_t) std::ceil (rate * windowSeconds);
        while (count > 1 && oldest().position <= oldestAllowed)
        {
            head = (head + 1) % (int) ring.size();
            --count;
        }
    }

    template <typename Frame>
    void push (const Frame* frames, int number) noexcept
    {
        for (int i = 0; i < number; ++i)
            push (frames[i]);
    }

    int size() const noexcept { return count; }
    int capacity() const noexcept { return (int) ring.size(); }
    std::size_t memoryBytes() const noexcept { return ring.capacity() * sizeof (Column); }
    double sampleRate() const noexcept { return rate; }
    double seconds() const noexcept { return windowSeconds; }
    /** Full output position on editor creation, to resolve the first low-word
        frame after the process has already crossed a 32-bit wrap. */
    void setPositionAnchor (std::int64_t position) noexcept { anchor = position; haveAnchor = true; }

    const Column& at (int index) const noexcept
    {
        static constexpr Column empty {};
        if (count == 0)
            return empty;
        return ring[(std::size_t) ((head + std::clamp (index, 0, std::max (0, count - 1))) % (int) ring.size())];
    }

    void clear() noexcept
    {
        head = count = 0;
        havePosition = false;
        newestPosition = 0;
        lastPositionLo = 0;
        lastPeriod = 64;
    }

private:
    const Column& oldest() const noexcept { return ring[(std::size_t) head]; }

    std::vector<Column> ring;
    int head = 0, count = 0;
    double rate = 48000.0, windowSeconds = 8.0;
    bool havePosition = false;
    bool haveAnchor = false;
    std::int64_t newestPosition = 0;
    std::int64_t anchor = 0;
    std::uint32_t lastPositionLo = 0, lastPeriod = 64;
};

} // namespace gapfill::timeline
