#pragma once

// F-X05 Local preference learning (docs/dev/features/F-X05): the pure model.
//
// Transparent naive-Bayes-style log-likelihood ratios over 18 binary indicators
// (15 algorithms, 3 bands) counted from the user's Like (+1) and Skip (-1)
// ratings. No hidden state; bounded by construction (multipliers 0.5 .. 2).

#include "../../dsp/FillRecipe.h"

#include <array>
#include <cstdint>
#include <vector>

namespace gapfill::taste
{

inline constexpr int kAlgoFeatures = 15;
inline constexpr int kBandFeatures = 3;
inline constexpr int kFeatures = kAlgoFeatures + kBandFeatures;

/** Parameters as the scheduler-side code sees them. Neutral = off. */
struct TasteSettings
{
    bool  on = false;
    float strength01 = 0.5f;
};

struct Counts
{
    std::array<std::uint16_t, kFeatures> liked {}, skipped {};
    std::uint16_t likes = 0, skips = 0;
};

/** What the audio thread multiplies in. */
struct TasteBias
{
    float algo[kAlgoFeatures];
    float band[kBandFeatures];
    bool ready = false;

    TasteBias() noexcept
    {
        for (auto& a : algo) a = 1.0f;
        for (auto& b : band) b = 1.0f;
    }
};

/** Adds one rating (+1 liked, -1 skipped); other values are ignored. */
void addRating (Counts&, const FillRecipe&, int rating) noexcept;

/** s_f = ln((L_f + 2) / (L + 4)) - ln((D_f + 2) / (D + 4)). */
float score (const Counts&, int feature) noexcept;

/** ready = L >= 3 && D >= 3 && L + D >= 8. Below that every multiplier is 1. */
bool isReady (const Counts&) noexcept;

float confidence (const Counts&) noexcept;       ///< min (1, (L + D) / 40)

TasteBias biasFrom (const Counts&, float strength01) noexcept;

/** int8 q = round (64 ln m), m = exp (q / 64), range +-44, as stored in the project. */
void quantise (const TasteBias&, std::int8_t (&q)[kFeatures]) noexcept;
TasteBias dequantise (const std::int8_t (&q)[kFeatures], bool ready) noexcept;

const char* featureName (int feature) noexcept;

struct ExplainItem { int feature = 0; int liked = 0, total = 0; float score = 0.0f; };

/** Features with L_f + D_f >= 3, ordered by |score|; toward = score > 0, away = score < 0. */
void explain (const Counts&, int maxItems, std::vector<ExplainItem>& toward, std::vector<ExplainItem>& away);

} // namespace gapfill::taste
