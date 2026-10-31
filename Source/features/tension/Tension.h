#pragma once

// F-M21 Tension / release curves: decision-stage mapping, width delta, the curve store
// shared between the editor (message thread) and the audio thread, and the status the
// editor shows. Depends only on Curves.h and EngineSettings.

#include "Curves.h"
#include "../../dsp/EngineSettings.h"

#include <array>
#include <atomic>
#include <mutex>
#include <type_traits>

namespace gapfill::tension
{

/** Decision stage (Effective::applyTension): density and intensity swing around the base by
    0.5 * depth * (2c - 1); fxSend01 = 1 - depth * max (0, 1 - 2c). Identity when inactive. */
void applyDecision (EngineSettings& sLocal, double ppq, bool transportValid, int tsNum, int tsDen) noexcept;

/** Per-chunk width target delta (normalised width, base + delta stays in [0, 2]); 0 when inactive. */
float widthDelta (const TensionSettings&, float baseWidth01, double ppqMid, bool transportValid, int tsNum, int tsDen) noexcept;

class TensionService
{
public:
    TensionService() noexcept;
    /** Non-RT editor/host restore: replaces valid curves; concurrent writers
        serialize privately. The audio thread must never call this method. */
    bool publishCurves (const TensionCurves&) noexcept;
    TensionCurves curves() const noexcept;                 ///< any thread; two bounded, lock-free attempts

    /** Audio thread: records where the playhead is for the editor. */
    void setStatus (const TensionOut&) noexcept;
    float lastPhase() const noexcept  { return phase.load (std::memory_order_relaxed); }
    bool lastActive() const noexcept  { return active.load (std::memory_order_relaxed); }
    bool isDefault() const noexcept;                       ///< all lanes flat: nothing to save

private:
    static_assert (std::is_trivially_copyable_v<TensionCurves>
                   && std::has_unique_object_representations_v<TensionCurves>
                   && sizeof (TensionCurves) % sizeof (std::uint32_t) == 0);
    static_assert (std::atomic<std::uint32_t>::is_always_lock_free
                   && std::atomic<std::uint64_t>::is_always_lock_free);
    static constexpr std::size_t wordCount = sizeof (TensionCurves) / sizeof (std::uint32_t);
    std::atomic<std::uint64_t> seq { 0 };
    std::array<std::atomic<std::uint32_t>, wordCount> stored {};
    std::mutex curveWriter;                              // NON-RT writers only; readers never acquire it
    std::atomic<float> phase { 0.0f };
    std::atomic<bool> active { false };
};

} // namespace gapfill::tension
