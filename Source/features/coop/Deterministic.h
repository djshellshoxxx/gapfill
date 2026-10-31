#pragma once

// F-X06 pure deterministic election over a coherent, immutable membership.
// Fixed musical slots, rather than process IDs or render order, seed ownership.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace gapfill::coop::deterministic
{

inline constexpr int kCapacity = 32;
enum class Grid : std::uint8_t { Beat, TwoBeats, Bar, TwoBars, FourBars };

struct Member
{
    std::uint64_t token = 0;                 ///< lifetime identity, never reused
    std::uint8_t slot = 0;                   ///< stable musical identity, 1..32
    std::uint8_t priority = 50;              ///< integer percent, 0 retains weight .05
};

struct Snapshot
{
    std::uint64_t generation = 0;
    std::uint8_t group = 0;
    int count = 0;
    std::array<Member, kCapacity> members {};
};

inline bool valid (const Snapshot& s) noexcept
{
    if (s.generation == 0 || s.group < 1 || s.group > 8 || s.count < 1 || s.count > kCapacity)
        return false;

    for (int i = 0; i < s.count; ++i)
    {
        const auto& a = s.members[(std::size_t) i];
        if (a.token == 0 || a.slot < 1 || a.slot > kCapacity || a.priority > 100)
            return false;
        for (int j = 0; j < i; ++j)
        {
            const auto& b = s.members[(std::size_t) j];
            if (a.slot == b.slot || a.token == b.token)
                return false;
        }
    }
    return true;
}

inline std::uint64_t mix (std::uint64_t value) noexcept
{
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

/** Weighted rendezvous: the lowest exponential race wins. Member removal only
    changes slots the removed member owned. No legacy RNG or mutable state. */
inline std::uint8_t ownerSlot (const Snapshot& s, std::int64_t timeSlot) noexcept
{
    if (! valid (s))
        return 0;

    double best = std::numeric_limits<double>::infinity();
    std::uint8_t winner = 0;
    const auto key = mix (0xC0090011ULL ^ mix ((std::uint64_t) timeSlot) ^ ((std::uint64_t) s.group << 56));
    for (int i = 0; i < s.count; ++i)
    {
        const auto& member = s.members[(std::size_t) i];
        const auto hash = mix (key ^ mix (member.slot));
        const double u = ((double) (hash >> 11) + 1.0) / 9007199254740994.0;
        const auto score = -std::log (u) / std::max (0.05, (double) member.priority / 100.0);
        if (score < best || (score == best && member.slot < winner))
        {
            best = score;
            winner = member.slot;
        }
    }
    return winner;
}

inline double gridBeats (Grid grid, int numerator, int denominator) noexcept
{
    const bool meterValid = numerator > 0 && numerator <= 32 && denominator > 0
                         && denominator <= 32 && (denominator & (denominator - 1)) == 0;
    const double bar = meterValid ? (double) numerator * 4.0 / denominator : 4.0;
    switch (grid)
    {
        case Grid::Beat: return 1.0;
        case Grid::TwoBeats: return 2.0;
        case Grid::Bar: return bar;
        case Grid::TwoBars: return 2.0 * bar;
        case Grid::FourBars: return 4.0 * bar;
    }
    return bar;
}

inline bool slotIndex (double ppq, Grid grid, int numerator, int denominator, std::int64_t& out) noexcept
{
    const auto value = std::floor (ppq / gridBeats (grid, numerator, denominator));
    if (! std::isfinite (value) || value < -0x1p52 || value > 0x1p52)
        return false;
    out = (std::int64_t) value;
    return true;
}

struct Scale { float density = 1.0f, fill = 1.0f; };

inline Scale yieldFor (bool owner, float percent) noexcept
{
    if (owner)
        return {};
    const auto amount = std::isfinite (percent) ? std::clamp (percent, 0.0f, 100.0f) * 0.01f : 0.0f;
    return { 1.0f - amount, 1.0f - 0.5f * amount };
}

} // namespace gapfill::coop::deterministic
