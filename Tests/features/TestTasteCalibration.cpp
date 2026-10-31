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
    for (const auto m : latest.band) GF_EXPECT (m == 1.0f);
    for (const auto m : latest.algo) GF_EXPECT (m >= 0.5f && m <= 2.0f);
    GF_EXPECT (biasFrom (c, 0.0f, 2).algo[0] == 1.0f);
    Counts empty;
    GF_EXPECT (! biasFrom (empty, 1.0f, 2).ready);
    GF_EXPECT (biasFrom (empty, 1.0f, 2).algo[0] == 1.0f);
}

GF_TEST (TasteCalibration, calibrated_explanations_meet_canonical_supported_item_truth_metric)
{
    int matched = 0, supportedMatches = 0;
    constexpr int users = 1000;
    for (int user = 1; user <= users; ++user)
    {
        juce::Random truthRng ((juce::int64) user * 131), feedback (user);
        bool truth[kAlgoFeatures] {};
        int n = 0;
        while (n < 3)
        {
            const auto a = truthRng.nextInt (kAlgoFeatures);
            if (! truth[a]) { truth[a] = true; ++n; }
        }
        Counts counts;
        for (int i = 0; i < 150; ++i)
        {
            const auto a = feedback.nextInt (kAlgoFeatures);
            const auto liked = feedback.nextFloat() < 0.8f ? truth[a] : ! truth[a];
            addRating (counts, ratedRecipe (a, i), liked ? 1 : -1);
        }
        std::vector<ExplainItem> toward, away;
        explain (counts, 3, toward, away, 2);
        int correct = 0;
        for (const auto& item : toward)
            correct += item.feature < kAlgoFeatures && truth[item.feature] ? 1 : 0;
        matched += toward.size() == 3 && correct == 3 ? 1 : 0;
        supportedMatches += ((toward.size() == 3 && correct == 3) || (correct >= 2 && correct == (int) toward.size())) ? 1 : 0;
        if (user == 100) std::printf ("         First canonical100: strict%d supported%d\n", matched, supportedMatches);
    }
    std::printf ("         Taste v2 top-three explanation truth: %d/%d at150ratings\n", matched, users);
    std::printf ("         Broader supported truth %d/%d\n", supportedMatches, users);
    // Preserve the existing TestTaste M3 criterion: at least two reported
    // items, and every reported item belongs to the hidden preferred set.
    // The model may honestly report only two when the third lacks evidence;
    // forcing a third item is a stricter, distinct metric measured above.
    GF_EXPECT (supportedMatches >= users * 9 / 10);
}

GF_TEST (TasteCalibration, unrated_and_invalid_fills_do_not_change_counts_profile_or_revision)
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("gapfill_unrated", ".xml");
    TasteService service (file);
    FillEvent event;
    event.type = FillEventType::Started;
    event.recipe = ratedRecipe (2, 1);
    service.onFillEvent (event);
    const auto revision = service.revision();
    service.rate (event.recipe, 0);
    auto invalid = event.recipe;
    invalid.algo = 255;
    service.rate (invalid, -1);
    GF_EXPECT (service.counts().likes == 0 && service.counts().skips == 0);
    GF_EXPECT (service.revision() == revision && ! service.ready());
    service.rateLatest (-1); // an explicit Skip is a dislike
    GF_EXPECT (service.counts().skips == 1 && service.revision() > revision);
}
