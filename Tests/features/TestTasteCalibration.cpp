#include "../TestFramework.h"
#include "features/taste/TasteService.h"

#include <cmath>
#include <cstdio>

using namespace gapfill;
using namespace gapfill::taste;

namespace
{
    FillRecipe ratedRecipe (int algo, int index)
    {
        FillRecipe r;
        r.algo = (std::uint8_t) algo;
        r.band = -1;
        r.lengthMs = 120;
        r.seed = (std::uint32_t) index + 1;
        return r;
    }

    struct Population
    {
        double sum = 0.0, squares = 0.0;
        void add (double share) { sum += share; squares += share * share; }
        double mean (int n) const { return sum / n; }
        double confidence95 (int n) const { const auto m = mean (n); return 1.96 * std::sqrt (std::max (0.0, squares / n - m * m) / n); }
    };

    double shareOf (const TasteBias& bias, const bool (&truth)[kAlgoFeatures])
    {
        double total = 0.0, liked = 0.0;
        for (int a = 0; a < kAlgoFeatures; ++a)
        {
            const auto m = bias.ready ? bias.algo[a] : 1.0f;
            total += m;
            if (truth[a]) liked += m;
        }
        return liked / total;
    }
}

GF_TEST (TasteCalibration, thirty_ratings_meet_M1_in_training_and_unseen_seed_populations)
{
    // Every simulated user contributes, including profiles that never become
    // ready. This is the actual quantised per-rating publication path, not an
    // unsmoothed final model or a population selected for favourable outcomes.
    constexpr int users = 10000;
    for (int cohort = 0; cohort < 2; ++cohort)
    {
        Population result;
        int ready = 0;
        for (int user = 1; user <= users; ++user)
        {
            const auto seed = (juce::int64) cohort * 1000000 + user;
            juce::Random choose (seed * 77), feedback (seed);
            bool truth[kAlgoFeatures] {};
            int n = 0;
            while (n < 3)
            {
                const auto a = choose.nextInt (kAlgoFeatures);
                if (! truth[a]) { truth[a] = true; ++n; }
            }
            Counts counts;
            std::int8_t q[kFeatures] {};
            for (int rating = 0; rating < 30; ++rating)
            {
                const auto a = feedback.nextInt (kAlgoFeatures);
                const bool liked = feedback.nextFloat() < 0.8f ? truth[a] : ! truth[a];
                addRating (counts, ratedRecipe (a, rating), liked ? 1 : -1);
                quantiseRatingUpdate (biasFrom (counts, 1.0f, 2), q, q);
            }
            ready += isReady (counts) ? 1 : 0;
            result.add (shareOf (dequantise (q, isReady (counts)), truth));
        }
        std::printf ("         Taste v2 cohort %d: n=%d ready=%d liked-share %.6f +/- %.6f (95%% CI), baseline .2\n",
                     cohort, users, ready, result.mean (users), result.confidence95 (users));
        GF_EXPECT (result.mean (users) - result.confidence95 (users) >= 0.35);
    }
}

GF_TEST (TasteCalibration, quantised_single_rating_delta_stays_under_point_one_even_in_place)
{
    for (int before = -44; before <= 44; ++before)
        for (const auto target : { 0.5f, 1.0f, 2.0f })
        {
            std::int8_t q[kFeatures];
            std::fill (std::begin (q), std::end (q), (std::int8_t) before);
            const auto old = dequantise (q, true);
            TasteBias bias;
            std::fill (std::begin (bias.algo), std::end (bias.algo), target);
            std::fill (std::begin (bias.band), std::end (bias.band), target);
            quantiseRatingUpdate (bias, q, q);
            const auto next = dequantise (q, true);
            for (int a = 0; a < kAlgoFeatures; ++a)
                GF_EXPECT (std::abs (next.algo[a] - old.algo[a]) < 0.1f);
            for (int b = 0; b < kBandFeatures; ++b)
                GF_EXPECT (std::abs (next.band[b] - old.band[b]) < 0.1f);
        }
}

GF_TEST (TasteCalibration, service_publishes_exact_calibrated_updates_and_preserves_loaded_profile_on_version_change)
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("gapfill_calibration", ".xml");
    TasteService service (file);
    service.timerTick (1, true, 1.0f);
    Counts counts;
    std::int8_t expected[kFeatures] {};
    for (int rating = 0; rating < 30; ++rating)
    {
        const auto recipe = ratedRecipe (rating % kAlgoFeatures, rating);
        const auto value = rating % 4 == 0 ? 1 : -1;
        addRating (counts, recipe, value);
        quantiseRatingUpdate (biasFrom (counts, 1.0f, 2), expected, expected);
        service.rate (recipe, value);
        const auto published = service.projectProfile();
        for (int f = 0; f < kFeatures; ++f)
            GF_EXPECT (published.q[f] == expected[f]);
    }
    const auto saved = service.projectProfile();
    service.setModelVersion (1);
    const auto restored = service.projectProfile();
    for (int f = 0; f < kFeatures; ++f)
        GF_EXPECT (restored.q[f] == saved.q[f]);
    GF_EXPECT (! file.existsAsFile()); // rating itself never writes; timer does debounced I/O
}

GF_TEST (TasteCalibration, version_one_formula_and_version_two_readiness_remain_explicit)
{
    Counts c;
    for (int i = 0; i < 30; ++i)
        addRating (c, ratedRecipe (i % 15, i), i % 3 == 0 ? 1 : -1);
    const auto legacy = biasFrom (c, 1.0f, 1);
    for (int f = 0; f < kAlgoFeatures; ++f)
    {
        const auto s = std::log (((float) c.liked[(std::size_t) f] + 2.0f) / ((float) c.likes + 4.0f))
                     - std::log (((float) c.skipped[(std::size_t) f] + 2.0f) / ((float) c.skips + 4.0f));
        GF_EXPECT (legacy.algo[f] == std::clamp (std::exp (0.75f * s), 0.5f, 2.0f));
    }
    const auto latest = biasFrom (c, 1.0f, 2);
    GF_EXPECT (latest.ready);
    for (const auto m : latest.algo) GF_EXPECT (m >= 0.5f && m <= 2.0f);
    GF_EXPECT (biasFrom (c, 0.0f, 2).algo[0] == 1.0f);
    Counts empty;
    GF_EXPECT (! biasFrom (empty, 1.0f, 2).ready);
    GF_EXPECT (biasFrom (empty, 1.0f, 2).algo[0] == 1.0f);
}
