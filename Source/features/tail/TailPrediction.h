#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace gapfill::tail
{
enum class LegacyStage : std::uint8_t { Formant, Resonator, Shift, Dub, Convolution, Freeze, Count };
inline constexpr std::size_t kLegacyStages = (std::size_t) LegacyStage::Count;
inline constexpr double kBaseSeconds = 2.5;
inline constexpr double kMaximumStageSeconds = 511.0 / 16.0;
inline constexpr double kInvalidHostBoundSeconds = 3600;

/** Prepared upper bounds include each card's cap, completion fade, bounded
    chunk overshoot and spectral support. Defaults conservatively cover shipped
    Full-quality cards down to8kHz before prepare. No audio-owned idle state. */
struct PreparedBounds
{
    std::array<double, kLegacyStages> legacySeconds { 0.5, 2.5, 4.5, 16.5, 2.0, 5.25 };
    bool valid = true;
};
struct Activation
{
    // Root snapshots raw parameter atomics at the host query, including before
    // the first audio callback. Apply feature-mode suppression in that adapter.
    std::array<bool, kLegacyStages> legacy {};
    // Largest active detailed PARALLEL return bound, never their sum. Root
    // obtains these from prepared bounds and the corresponding raw mode/mix.
    double parallelMaximumSeconds = 0;
};

/** A conservative source->legacy-serial->parallel-return bound. Even when the
    parallel sends branch before serial processing this remains an upper bound.
    Off is exactly the released2.5s. It never changes samples or PDC. */
inline double predictSeconds (const PreparedBounds& bounds, const Activation& active) noexcept
{
    const bool anyLegacy = std::any_of (active.legacy.begin(), active.legacy.end(), [] (bool value) { return value; });
    if (! anyLegacy && active.parallelMaximumSeconds == 0) return kBaseSeconds;
    if (! bounds.valid || ! std::isfinite (active.parallelMaximumSeconds)
        || active.parallelMaximumSeconds < 0 || active.parallelMaximumSeconds > kMaximumStageSeconds)
        return kInvalidHostBoundSeconds;
    double result = kBaseSeconds + active.parallelMaximumSeconds;
    for (std::size_t i = 0; i < kLegacyStages; ++i)
        if (active.legacy[i])
        {
            const double value = bounds.legacySeconds[i];
            if (! std::isfinite (value) || value < 0 || value > kMaximumStageSeconds)
                return kInvalidHostBoundSeconds;
            result += value;
        }
    return result;
}

/** One coherent atomic word for all six prepared bounds. Round UP to1/16s;
    the extra host-render silence is <62.5ms/card and never under-reports. Only
    constructor/prepare/control owners publish, never sample processing. */
class PublishedBounds
{
public:
    PublishedBounds() noexcept { publish ({}); }
    bool publish (const PreparedBounds& bounds) noexcept
    {
        std::uint64_t packed = 0;
        bool ok = bounds.valid;
        for (std::size_t i = 0; i < kLegacyStages; ++i)
        {
            const double value = bounds.legacySeconds[i];
            if (! std::isfinite (value) || value < 0 || value > kMaximumStageSeconds)
            {
                ok = false;
                continue;
            }
            const auto ticks = (std::uint64_t) std::ceil (value * 16);
            packed |= ticks << (i * 9);
        }
        if (! ok) packed |= invalidBit;
        word.store (packed, std::memory_order_release);
        return ok;
    }
    PreparedBounds load() const noexcept
    {
        const auto packed = word.load (std::memory_order_acquire);
        PreparedBounds result;
        for (std::size_t i = 0; i < kLegacyStages; ++i)
            result.legacySeconds[i] = (double) ((packed >> (i * 9)) & 511u) / 16;
        result.valid = (packed & invalidBit) == 0;
        return result;
    }
private:
    static constexpr std::uint64_t invalidBit = std::uint64_t { 1 } << 63;
    std::atomic<std::uint64_t> word { 0 };
};
static_assert (std::atomic<std::uint64_t>::is_always_lock_free);
static_assert (sizeof (PublishedBounds) == sizeof (std::uint64_t));
} // namespace gapfill::tail
