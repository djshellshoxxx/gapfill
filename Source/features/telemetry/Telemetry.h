#pragma once

// F-S01 CPU and latency display (docs/dev/12 F-S01).
//
// CpuMeter keeps a sampled estimate of how much of each block's time budget
// processBlock used: an exponential moving average of
// (wall time of processBlock) / (block duration). It is an ESTIMATE of this
// plugin instance only and every label says so; it is never the host's CPU
// figure. Cost on the audio thread: two high-resolution tick reads per block,
// no allocation, no locks, and one relaxed atomic load when disabled.

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>

namespace gapfill::telemetry
{

class CpuMeter
{
public:
    /** Time constant of the moving average, independent of the host block size. */
    static constexpr double smoothingSeconds = 0.5;

    /** Message thread (editor open / health consumers present). While disabled the
        audio thread only performs one relaxed atomic load per block. */
    void setEnabled (bool shouldRun) noexcept   { enabled.store (shouldRun, std::memory_order_relaxed); }
    bool isEnabled() const noexcept             { return enabled.load (std::memory_order_relaxed); }

    /** Audio thread. Returns a token for end(); -1 when disabled. */
    std::int64_t begin() const noexcept
    {
        return isEnabled() ? (std::int64_t) juce::Time::getHighResolutionTicks() : -1;
    }

    /** Audio thread. Pass the token from begin() and the block just processed. */
    void end (std::int64_t token, int numSamples, double sampleRate) noexcept
    {
        if (token < 0 || numSamples <= 0 || sampleRate <= 0.0)
            return;

        const auto elapsed = juce::Time::highResolutionTicksToSeconds (
            (juce::int64) juce::Time::getHighResolutionTicks() - (juce::int64) token);
        record (elapsed, (double) numSamples / sampleRate);
    }

    /** Audio thread. The pure core of end(): `elapsedSeconds` of work for a block
        that lasts `blockSeconds`. Non-finite or negative input is ignored. */
    void record (double elapsedSeconds, double blockSeconds) noexcept
    {
        if (! (blockSeconds > 0.0) || ! (elapsedSeconds >= 0.0) || ! std::isfinite (elapsedSeconds))
            return;

        const auto ratio = (float) (elapsedSeconds / blockSeconds);
        const auto alpha = (float) (1.0 - std::exp (-blockSeconds / smoothingSeconds));
        const auto old = ema.load (std::memory_order_relaxed);
        ema.store (old + alpha * (ratio - old), std::memory_order_relaxed);
        lastRatio.store (ratio, std::memory_order_relaxed);

        if (ratio > 1.0f)
            overruns.fetch_add (1, std::memory_order_relaxed);
    }

    /** 0..1+ : share of the block budget used, smoothed. */
    float getCpu01() const noexcept             { return ema.load (std::memory_order_relaxed); }
    float getLastBlockRatio() const noexcept    { return lastRatio.load (std::memory_order_relaxed); }

    /** Blocks whose processing took longer than the block lasted (deadline overruns). */
    std::uint32_t getOverrunCount() const noexcept { return overruns.load (std::memory_order_relaxed); }

    /** Only while the audio thread is not running (prepare / tests). */
    void reset() noexcept
    {
        ema.store (0.0f, std::memory_order_relaxed);
        lastRatio.store (0.0f, std::memory_order_relaxed);
        overruns.store (0, std::memory_order_relaxed);
    }

private:
    std::atomic<bool> enabled { false };
    std::atomic<float> ema { 0.0f };
    std::atomic<float> lastRatio { 0.0f };
    std::atomic<std::uint32_t> overruns { 0 };
};

/** "PDC 512 smp (10.7 ms)" - the latency the plugin reports to the host. */
juce::String formatLatency (int latencySamples, double sampleRate);

/** "CPU ~12% (estimated)"; "CPU: not measured" while the meter is off. */
juce::String formatCpu (float cpu01, bool measured);

/** Lines for the Options / detail view: latency, then CPU. Every CPU figure is
    labelled "estimated"; the text states it is not the host's CPU meter. */
juce::String describeTelemetry (int latencySamples, double sampleRate, float cpu01, bool measured);

} // namespace gapfill::telemetry
