#pragma once
#include "../../Salts.h"
#include <cstdint>

namespace gapfill::harmonic::psola::detail
{
/** Private sub-generator. Only the recorded32-bit recipe seed and the existing
    feature salt enter it; it never draws from the scheduler's generator. */
struct Random
{
    explicit Random (std::uint32_t recipeSeed) noexcept
    {
        const std::uint64_t seed = recipeSeed;
        state = seed ^ (kSalt_HGran + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
        state += 0x9e3779b97f4a7c15ull;
        state = (state ^ (state >> 30)) * 0xbf58476d1ce4e5b9ull;
        state = (state ^ (state >> 27)) * 0x94d049bb133111ebull;
        state ^= state >> 31;
        if (state == 0) state = 0x9e3779b97f4a7c15ull;
    }
    std::uint32_t next() noexcept
    {
        state ^= state >> 12; state ^= state << 25; state ^= state >> 27;
        return (std::uint32_t) ((state * 2685821657736338717ull) >> 32);
    }
    double uniform() noexcept { return (double) next() / 4294967296.0; }
    std::uint64_t state = 1;
};
} // namespace gapfill::harmonic::psola::detail
