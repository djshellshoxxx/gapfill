#pragma once

// Last decision-stage figures, written by the audio thread once per evaluated
// gap and read by the editor timer (C-6 breathing LED, C-7 phrase position bar,
// C-8 waypoint label, F-D01 bar strip). Relaxed atomics; -1 / negative means
// "not evaluated" (feature off or no valid transport).

#include <atomic>
#include <cstdint>

namespace gapfill
{

struct DecisionTelemetry
{
    std::atomic<float> breathPhase { -1.0f };      ///< C-6: phase 0..1 of the breathing cycle
    std::atomic<float> breathOffset { 0.0f };      ///< C-6: signed density offset applied (-0.5..0.5)
    std::atomic<float> phrasePosition { -1.0f };   ///< C-7: position 0..1 in the phrase
    std::atomic<float> phraseMultiplier { 1.0f };  ///< C-7: probability multiplier applied
    std::atomic<int>   evolveProfile { -1 };       ///< C-8: current waypoint (Surprise profile index)
    std::atomic<int>   barFills { 0 };             ///< F-D01: fills started in the current bar
    std::atomic<int>   barIndexSeen { 0 };         ///< F-D01: the bar they were counted in (low 31 bits)
    std::atomic<std::uint32_t> decisions { 0 };    ///< gaps evaluated since reset (tests)

    void reset() noexcept
    {
        breathPhase.store (-1.0f); breathOffset.store (0.0f);
        phrasePosition.store (-1.0f); phraseMultiplier.store (1.0f);
        evolveProfile.store (-1); barFills.store (0); barIndexSeen.store (0); decisions.store (0);
    }
};

} // namespace gapfill
