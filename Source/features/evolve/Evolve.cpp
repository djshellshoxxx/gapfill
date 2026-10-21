#include "Evolve.h"
#include "../Salts.h"
#include "../timing/BarTiming.h"
#include "../../Presets.h"
#include "../../dsp/DspUtils.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace gapfill::evolve
{

namespace
{
    constexpr int kMaxProfiles = 16;

    long long floorDiv (long long a, long long b) noexcept
    {
        auto q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    }

    /** Raw (un-repaired) permutation of 0..n-1 for permutation block `block`. */
    std::array<int, kMaxProfiles> rawPermutation (long long block, int seed, int n) noexcept
    {
        std::array<int, kMaxProfiles> p {};

        for (int i = 0; i < n; ++i)
            p[(size_t) i] = i;

        Rng rng;
        rng.seed (hashCombine (hashCombine ((std::uint64_t) (std::int64_t) seed * 0x9E3779B1ULL, kSalt_Evolve),
                               (std::uint64_t) block));

        for (int i = n - 1; i > 0; --i)                      // Fisher-Yates
            std::swap (p[(size_t) i], p[(size_t) rng.nextInt (i + 1)]);

        return p;
    }
}

int profileForCycle (long long cycle, int seed, int numProfiles) noexcept
{
    const auto n = std::clamp (numProfiles, 1, kMaxProfiles);

    if (n < 3)
        return (int) (((cycle % n) + n) % n);

    const auto block = floorDiv (cycle, n);
    const auto index = (int) (cycle - block * n);

    auto perm = rawPermutation (block, seed, n);
    const auto previous = rawPermutation (block - 1, seed, n);

    // The first waypoint of a block must differ from the last of the previous block. Swapping
    // positions 0 and 1 never moves the last element, so this needs no recursion.
    if (perm[0] == previous[(size_t) (n - 1)])
        std::swap (perm[0], perm[1]);

    return perm[(size_t) index];
}

bool waypointAt (const EngineSettings& s, double ppq, const Effective::TransportInfo& t, Waypoint& w) noexcept
{
    const auto period = s.perf.evolve.periodBars;

    if (period <= 0 || ! std::isfinite (ppq))
        return false;

    const auto bpb = timing::beatsPerBar (t);

    if (bpb <= 0.0)
        return false;

    const auto cycleLength = (double) period * bpb;
    const auto position = ppq / cycleLength;
    const auto cycle = (long long) std::floor (position);
    const auto phase = position - std::floor (position);
    const auto n = getNumSurpriseProfiles();

    w.cycle = cycle;
    w.profile = profileForCycle (cycle, s.seed, n);
    w.previousProfile = profileForCycle (cycle - 1, s.seed, n);
    w.blend = phase < kBlendFraction ? smoothstep (0.0f, (float) kBlendFraction, (float) phase) : 1.0f;
    return true;
}

void apply (EngineSettings& s, double ppq, const Effective::TransportInfo& t) noexcept
{
    Waypoint w;

    if (! waypointAt (s, ppq, t, w))
        return;

    const auto& from = surpriseTargets (w.previousProfile);
    const auto& to = surpriseTargets (w.profile);
    const auto mix = [&] (float a, float b) { return lerp (a, b, w.blend); };
    const auto pull = [] (float effective, float target) { return std::clamp (lerp (effective, target, kPull), 0.0f, 1.0f); };

    s.fill01       = pull (s.fill01,       mix (from.fill, to.fill));
    s.intensity01  = pull (s.intensity01,  mix (from.intensity, to.intensity));
    s.complexity01 = pull (s.complexity01, mix (from.complexity, to.complexity));
    s.chaos01      = pull (s.chaos01,      mix (from.chaos, to.chaos));
    s.density01    = pull (s.density01,    mix (from.density, to.density));

    // The core / background weight pair; a forced Fill Type ignores weights, and a weight the user set to 0 stays 0.
    if (s.fillTypeIndex == 0)
        for (int i = 0; i < numAlgos; ++i)
            if (s.weights[i] > 0.0f)
                s.weights[i] = pull (s.weights[i], mix (from.weight[i], to.weight[i]));
}

} // namespace gapfill::evolve
