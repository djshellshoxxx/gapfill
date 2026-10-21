#pragma once

// C-11 More Like That: the bias state (docs/dev/04-SPEC-CREATIVE.md C-11).
//
// Contract
//  * Audio-thread owned. A Like stores the recipe's algorithm and band and arms the
//    next four STARTED natural fills.
//  * The bias only changes the weights seen by chooseAlgorithm / chooseBand
//    (x2.5 same algorithm, x1.4 same family, x2 same band). It never changes
//    eligibility, never enables a zero-weight algorithm (0 x anything = 0) and never
//    draws from the RNG, so the draw count of every later decision is unchanged.
//  * Rejected gaps and previews do not count; a transport jump cancels; a new Like
//    restarts the count; Fill Lock suppresses the bias.

#include "../../Parameters.h"
#include "../../dsp/FillRecipe.h"

#include <array>

namespace gapfill
{

enum class AlgoFamily : std::uint8_t { Rhythmic, Textural, PitchTape, Destructive };

/** rhythmic: Stutter / Scramble / Delay; textural: Granular / Freeze / Texture / Spectral / Comb;
    pitch-tape: PitchRise / PitchFall / TapeStop / Filter; destructive: Crush / Distort / Reverse. */
inline constexpr std::array<AlgoFamily, (size_t) numAlgos> kAlgoFamilies = []
{
    std::array<AlgoFamily, (size_t) numAlgos> t {};

    t[(size_t) Algo::Stutter]   = AlgoFamily::Rhythmic;
    t[(size_t) Algo::Scramble]  = AlgoFamily::Rhythmic;
    t[(size_t) Algo::Delay]     = AlgoFamily::Rhythmic;
    t[(size_t) Algo::Granular]  = AlgoFamily::Textural;
    t[(size_t) Algo::Freeze]    = AlgoFamily::Textural;
    t[(size_t) Algo::Texture]   = AlgoFamily::Textural;
    t[(size_t) Algo::Spectral]  = AlgoFamily::Textural;
    t[(size_t) Algo::Comb]      = AlgoFamily::Textural;
    t[(size_t) Algo::PitchRise] = AlgoFamily::PitchTape;
    t[(size_t) Algo::PitchFall] = AlgoFamily::PitchTape;
    t[(size_t) Algo::TapeStop]  = AlgoFamily::PitchTape;
    t[(size_t) Algo::Filter]    = AlgoFamily::PitchTape;
    t[(size_t) Algo::Crush]     = AlgoFamily::Destructive;
    t[(size_t) Algo::Distort]   = AlgoFamily::Destructive;
    t[(size_t) Algo::Reverse]   = AlgoFamily::Destructive;
    return t;
}();

struct LikeState
{
    static constexpr int   startsPerLike = 4;
    static constexpr float sameAlgo   = 2.5f;
    static constexpr float sameFamily = 1.4f;
    static constexpr float sameBand   = 2.0f;

    FillRecipe recipe;
    int remaining = 0;

    bool active() const noexcept                { return remaining > 0; }

    void start (const FillRecipe& r) noexcept   { if (isValid (r)) { recipe = r; remaining = startsPerLike; } }
    void cancel() noexcept                      { remaining = 0; }
    void noteStarted() noexcept                 { if (remaining > 0) --remaining; }

    float algoFactor (int algo) const noexcept
    {
        if (remaining <= 0 || algo < 0 || algo >= numAlgos)
            return 1.0f;

        if (algo == recipe.algo)
            return sameAlgo;

        return kAlgoFamilies[(size_t) algo] == kAlgoFamilies[(size_t) juce::jlimit (0, numAlgos - 1, (int) recipe.algo)]
                   ? sameFamily : 1.0f;
    }

    float bandFactor (int band) const noexcept
    {
        return remaining > 0 && band >= 0 && band == recipe.band ? sameBand : 1.0f;
    }
};

} // namespace gapfill
