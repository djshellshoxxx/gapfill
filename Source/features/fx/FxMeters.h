#pragma once

// Lock-free effect telemetry (docs/dev/12 FX-0): written by the audio thread
// with relaxed atomics, read by the editor timer and F-S01 / N-USE-b.

#include "EffectCard.h"

#include <atomic>
#include <cstdint>

namespace gapfill::fx
{

struct FxMeters
{
    /** Last sampled microseconds spent in the card per call (sampled every 64 calls). */
    std::atomic<float> cpuMicros[kNumCards] {};
    /** Worst sampled value since reset. */
    std::atomic<float> cpuWorstMicros[kNumCards] {};

    // --- N-FX1 ---
    std::atomic<std::int32_t> swellLandingBar { 0 };     ///< 1-based bar of the last landing, 0 = none
    std::atomic<std::int32_t> swellLandingBeat { 0 };    ///< 1-based beat within that bar
    std::atomic<std::uint32_t> swellFills { 0 };
    std::atomic<bool> swellNoTransport { false };        ///< last swell fell back to Gap end

    // --- N-FX2 ---
    std::atomic<std::int32_t> cloudGrains { 0 };         ///< estimated simultaneous grains of the last cloud
    std::atomic<bool> cloudLimited { false };
    std::atomic<std::uint32_t> cloudDropped { 0 };       ///< grains that could not be allocated

    // --- F-X01 (WP7): the last harmonic pitch step ---
    std::atomic<float> hgF0 { 0.0f };                    ///< detected f0 of the last harmonic fill, 0 = none
    std::atomic<float> hgConfidence { 0.0f };
    std::atomic<std::int32_t> hgState { 0 };             ///< 0 none yet, 1 harmonic used, 2 not pitched (plain Granular)

    // --- F-M16 / tempo-locked cards ---
    std::atomic<bool> tempoFallback { false };

    void reset() noexcept
    {
        for (int i = 0; i < kNumCards; ++i)
        {
            cpuMicros[i].store (0.0f, std::memory_order_relaxed);
            cpuWorstMicros[i].store (0.0f, std::memory_order_relaxed);
        }

        swellLandingBar.store (0, std::memory_order_relaxed);
        swellLandingBeat.store (0, std::memory_order_relaxed);
        swellFills.store (0, std::memory_order_relaxed);
        swellNoTransport.store (false, std::memory_order_relaxed);
        cloudGrains.store (0, std::memory_order_relaxed);
        cloudLimited.store (false, std::memory_order_relaxed);
        cloudDropped.store (0, std::memory_order_relaxed);
        tempoFallback.store (false, std::memory_order_relaxed);
    }
};

} // namespace gapfill::fx
