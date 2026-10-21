#include "Genome.h"

#include "../../dsp/DspUtils.h"

#include <algorithm>
#include <cmath>

namespace gapfill::mutation
{

const std::array<const char*, kNumGenes>& geneIds() noexcept
{
    static const std::array<const char*, kNumGenes> ids =
    {
        "fill", "intensity", "complexity", "chaos", "density",
        "wReverse", "wStutter", "wDelay", "wGranular", "wFreeze", "wPitchRise", "wPitchFall",
        "wFilter", "wTexture", "wCrush", "wDistort", "wScramble", "wSpectral", "wComb", "wTapeStop",
        "bandLowAmt", "bandLowInt", "bandLowProb",
        "bandMidAmt", "bandMidInt", "bandMidProb",
        "bandHighAmt", "bandHighInt", "bandHighProb"
    };

    return ids;
}

int genesInScope (int scope) noexcept
{
    return scope <= 0 ? 5 : scope == 1 ? 20 : kNumGenes;
}

namespace
{
    float uniform (std::uint64_t& state) noexcept
    {
        Rng r;
        r.state = state;
        const auto v = r.nextFloat();
        state = r.state;
        return v;
    }

    float clamp01 (float v) noexcept { return std::min (1.0f, std::max (0.0f, v)); }

    void guardWeights (Genes& g) noexcept
    {
        bool anyAlive = false;

        for (int i = 5; i < 20; ++i)
            anyAlive = anyAlive || g[(size_t) i] >= 0.05f;

        if (anyAlive)
            return;

        int largest = 5;

        for (int i = 5; i < 20; ++i)
            if (g[(size_t) i] > g[(size_t) largest])
                largest = i;

        g[(size_t) largest] = 0.5f;
    }
}

float gaussian (std::uint64_t& state) noexcept
{
    float sum = 0.0f;

    for (int i = 0; i < 12; ++i)
        sum += uniform (state);

    return sum - 6.0f;
}

float distanceInf (const Genes& a, const Genes& b) noexcept
{
    float d = 0.0f;

    for (size_t i = 0; i < a.size(); ++i)
        d = std::max (d, std::abs (a[i] - b[i]));

    return d;
}

ChildSet makeChildren (const Genes& parent, std::uint32_t pins, int scope, float rate, std::uint64_t labSeed,
                       int generation, const Genes* breedWith) noexcept
{
    ChildSet out;
    rate = std::min (kMaxRate, std::max (kMinRate, rate));
    const auto inScope = genesInScope (scope);
    const float sigma = breedWith != nullptr ? rate * 0.5f : rate;

    for (int c = 0; c < kNumChildren; ++c)
    {
        bool ok = false;
        Genes best = parent;

        for (int attempt = 0; attempt < 8 && ! ok; ++attempt)
        {
            const auto childIndex = (std::uint64_t) (c + kNumChildren * attempt);
            Rng seeded;
            seeded.seed (hashCombine (hashCombine (labSeed, (std::uint64_t) generation), childIndex));
            auto state = seeded.state;

            Genes g = parent;

            for (int i = 0; i < inScope; ++i)
            {
                if (pins & (1u << i))
                    continue;

                float base = parent[(size_t) i];

                if (breedWith != nullptr && uniform (state) < 0.5f)
                    base = (*breedWith)[(size_t) i];

                g[(size_t) i] = clamp01 (base + sigma * gaussian (state));
            }

            guardWeights (g);
            best = g;

            ok = distanceInf (g, parent) >= kMinParentDistance;

            for (int other = 0; other < c && ok; ++other)
                ok = distanceInf (g, out.child[(size_t) other]) >= kMinChildDistance;
        }

        out.child[(size_t) c] = best;
        out.flagged[(size_t) c] = ! ok;
    }

    return out;
}

int biggestDifferences (const Genes& parent, const Genes& child, GeneDiff* out, int n) noexcept
{
    std::array<GeneDiff, kNumGenes> all {};

    for (int i = 0; i < kNumGenes; ++i)
        all[(size_t) i] = { i, child[(size_t) i] - parent[(size_t) i] };

    std::stable_sort (all.begin(), all.end(), [] (const GeneDiff& a, const GeneDiff& b) { return std::abs (a.delta) > std::abs (b.delta); });

    int count = 0;

    for (int i = 0; i < n && i < kNumGenes; ++i)
        if (std::abs (all[(size_t) i].delta) > 1.0e-6f)
            out[count++] = all[(size_t) i];

    return count;
}

} // namespace gapfill::mutation
