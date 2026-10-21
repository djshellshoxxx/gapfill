// F-X05 Local preference learning (docs/dev/features/F-X05).

#include "../support/TestRig.h"
#include "../support/AllocGuard.h"

#include "features/taste/RatingStore.h"
#include "features/taste/TasteModel.h"
#include "features/taste/TasteService.h"
#include "features/taste/TasteStateTree.h"

#include <cmath>
#include <cstring>

using namespace gapfill;
using namespace gapfill::taste;
using namespace gftest;

namespace
{
    FillRecipe recipeFor (int algo, int band = -1, int lengthMs = 120, std::uint32_t seed = 1)
    {
        FillRecipe r;
        r.algo = (std::uint8_t) algo;
        r.band = (std::int8_t) band;
        r.lengthMs = (std::uint16_t) lengthMs;
        r.seed = seed;
        return r;
    }

    juce::File tempFile (const char* name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gapfill_taste_test").getChildFile (name);
        dir.deleteRecursively();
        dir.createDirectory();
        return dir.getChildFile ("ratings.xml");
    }

    /** A simulated user who likes the algorithms in `liked` and is `consistency` reliable. */
    Counts simulate (std::uint32_t seed, const bool (&liked)[15], int ratings, float consistency = 0.8f)
    {
        juce::Random rnd ((juce::int64) seed);
        Counts c;

        for (int i = 0; i < ratings; ++i)
        {
            const int algo = rnd.nextInt (15);
            const bool truth = liked[algo];
            const bool says = rnd.nextFloat() < consistency ? truth : ! truth;
            addRating (c, recipeFor (algo), says ? 1 : -1);
        }

        return c;
    }

    double likedShare (const TasteBias& b, const bool (&liked)[15])
    {
        double total = 0.0, part = 0.0;

        for (int a = 0; a < 15; ++a)
        {
            total += b.algo[a];
            part += liked[a] ? b.algo[a] : 0.0;
        }

        return part / total;
    }
}

//==============================================================================
GF_TEST (Taste, scores_zero_without_ratings_and_not_ready_is_identity)
{
    Counts c;

    for (int f = 0; f < kFeatures; ++f)
        GF_EXPECT (std::abs (score (c, f)) < 1.0e-6f);

    GF_EXPECT (! isReady (biasFrom (c, 1.0f).ready));

    // Below the readiness threshold every multiplier is exactly 1.
    for (int i = 0; i < 7; ++i)
        addRating (c, recipeFor (i % 3), i % 2 ? 1 : -1);

    const auto b = biasFrom (c, 1.0f);
    GF_EXPECT (! b.ready);

    for (float m : b.algo) GF_EXPECT (m == 1.0f);
    for (float m : b.band) GF_EXPECT (m == 1.0f);

    // Needs both kinds of rating.
    Counts onlyLikes;

    for (int i = 0; i < 12; ++i)
        addRating (onlyLikes, recipeFor (2), 1);

    GF_EXPECT (! isReady (onlyLikes));
}

GF_TEST (Taste, single_like_changes_a_multiplier_by_less_than_0_1)
{
    bool liked[15] {};
    liked[0] = liked[1] = liked[2] = true;
    auto c = simulate (5, liked, 24);
    GF_EXPECT (isReady (c));

    const auto before = biasFrom (c, 1.0f);
    addRating (c, recipeFor (4), 1);
    const auto after = biasFrom (c, 1.0f);

    for (int a = 0; a < 15; ++a)
        GF_EXPECT_MSG (std::abs (after.algo[a] - before.algo[a]) < 0.1f, "algo " + juce::String (a));
}

GF_TEST (Taste, learns_liked_algorithms_and_never_collapses)
{
    double shareSum = 0.0;
    int users = 0, collapsed = 0;

    for (std::uint32_t u = 1; u <= 100; ++u)
    {
        bool liked[15] {};
        juce::Random pick ((juce::int64) u * 77);
        int set = 0;

        while (set < 3)
        {
            const int a = pick.nextInt (15);

            if (! liked[a]) { liked[a] = true; ++set; }
        }

        const auto bias = biasFrom (simulate (u, liked, 30), 1.0f);

        if (! bias.ready)
            continue;

        ++users;
        shareSum += likedShare (bias, liked);

        for (int a = 0; a < 15; ++a)
        {
            collapsed += bias.algo[a] < 0.5f - 1.0e-6f || bias.algo[a] > 2.0f + 1.0e-6f;
            GF_EXPECT (bias.algo[a] >= 0.5f - 1.0e-6f && bias.algo[a] <= 2.0f + 1.0e-6f);
        }
    }

    GF_EXPECT (users >= 95);
    GF_EXPECT (collapsed == 0);
    const double average = shareSum / users;
    GF_EXPECT_MSG (average >= 0.20 + 0.15 - 0.02, "liked share " + juce::String (average));      // baseline 20 %
}

GF_TEST (Taste, explanation_top3_matches_the_simulated_truth)
{
    int good = 0, trials = 100;

    for (std::uint32_t u = 1; u <= (std::uint32_t) trials; ++u)
    {
        bool liked[15] {};
        juce::Random pick ((juce::int64) u * 131);
        int set = 0;

        while (set < 3)
        {
            const int a = pick.nextInt (15);

            if (! liked[a]) { liked[a] = true; ++set; }
        }

        const auto c = simulate (u, liked, 150);                           // an engaged user: about ten ratings per fill type
        std::vector<ExplainItem> toward, away;
        explain (c, 3, toward, away);

        int correct = 0;

        for (const auto& it : toward)
            correct += it.feature < 15 && liked[it.feature];

        good += (toward.size() == 3 && correct == 3) || (correct >= 2 && correct == (int) toward.size());
    }

    GF_EXPECT_MSG (good >= 90, "good " + juce::String (good) + " of 100");

    // Counts in the explanation match the data.
    Counts c;

    for (int i = 0; i < 5; ++i) addRating (c, recipeFor (3), 1);
    for (int i = 0; i < 2; ++i) addRating (c, recipeFor (3), -1);
    addRating (c, recipeFor (6), -1);                                       // too few ratings: not listed

    std::vector<ExplainItem> toward, away;
    explain (c, 3, toward, away);
    GF_EXPECT (toward.size() == 1 && toward[0].feature == 3 && toward[0].liked == 5 && toward[0].total == 7);
    GF_EXPECT (away.empty());
}

GF_TEST (Taste, quantise_roundtrip_error_under_1_percent)
{
    bool liked[15] {};
    liked[1] = liked[7] = liked[9] = true;
    const auto bias = biasFrom (simulate (9, liked, 40), 1.0f);
    std::int8_t q[kFeatures];
    quantise (bias, q);
    const auto back = dequantise (q, true);

    for (int a = 0; a < 15; ++a)
        GF_EXPECT (std::abs (back.algo[a] / bias.algo[a] - 1.0f) < 0.01f);

    for (int b = 0; b < 3; ++b)
        GF_EXPECT (std::abs (back.band[b] / bias.band[b] - 1.0f) < 0.01f);

    for (auto v : q)
        GF_EXPECT (v >= -44 && v <= 44);
}

GF_TEST (Taste, apply_multiplies_weights_only_and_respects_precedence)
{
    GapFillAudioProcessor proc;
    auto& svc = proc.getContextState().tasteService();
    svc.useLibraryFile (tempFile ("apply"));

    // Ten ratings: stutter-ish liked, crush-ish skipped.
    for (int i = 0; i < 6; ++i) svc.rate (recipeFor ((int) Algo::Stutter, -1, 100 + i, (std::uint32_t) i + 1), 1);
    for (int i = 0; i < 5; ++i) svc.rate (recipeFor ((int) Algo::Crush, -1, 100 + i, (std::uint32_t) i + 50), -1);

    GF_EXPECT (svc.ready());

    EngineSettings base;

    for (auto& w : base.weights) w = 0.8f;

    base.weights[(int) Algo::Scramble] = 0.0f;                    // user switched it off
    base.perf.taste.on = true;

    auto s = base;
    svc.apply (s);
    GF_EXPECT (s.weights[(int) Algo::Stutter] > base.weights[(int) Algo::Stutter]);
    GF_EXPECT (s.weights[(int) Algo::Crush] < base.weights[(int) Algo::Crush]);
    GF_EXPECT (s.weights[(int) Algo::Scramble] == 0.0f);          // never enables something at weight 0

    for (int a = 0; a < numAlgos; ++a)
        GF_EXPECT (s.weights[a] >= 0.5f * base.weights[a] - 1.0e-6f && s.weights[a] <= 2.0f * base.weights[a] + 1.0e-6f);

    // Everything else is untouched.
    auto restoredWeights = s;
    std::memcpy (restoredWeights.weights, base.weights, sizeof (base.weights));
    std::memcpy (restoredWeights.bandProb, base.bandProb, sizeof (base.bandProb));
    GF_EXPECT (std::memcmp (&restoredWeights, &base, sizeof (EngineSettings)) == 0);

    // Off, locked and forced fill type leave the settings alone.
    auto off = base; off.perf.taste.on = false;
    auto copyOff = off; svc.apply (off);
    GF_EXPECT (std::memcmp (&off, &copyOff, sizeof (EngineSettings)) == 0);

    auto locked = base; locked.perf.lockOn = true;
    auto copyLocked = locked; svc.apply (locked);
    GF_EXPECT (std::memcmp (&locked, &copyLocked, sizeof (EngineSettings)) == 0);

    auto forced = base; forced.fillTypeIndex = 3;
    auto copyForced = forced; svc.apply (forced);
    GF_EXPECT (std::memcmp (&forced, &copyForced, sizeof (EngineSettings)) == 0);

    // Not ready (a fresh service): identity.
    GapFillAudioProcessor fresh;
    auto notReady = base; auto copyNr = notReady;
    fresh.getContextState().tasteService().apply (notReady);
    GF_EXPECT (std::memcmp (&notReady, &copyNr, sizeof (EngineSettings)) == 0);
}

GF_TEST (Taste, commands_flow_from_the_audio_thread_to_the_library)
{
    GapFillAudioProcessor proc;
    auto& ctx = proc.getContextState();
    auto& svc = ctx.tasteService();
    svc.useLibraryFile (tempFile ("commands"));

    FillEvent started;
    started.type = FillEventType::Started;
    started.recipe = recipeFor ((int) Algo::Granular, -1, 150, 9);
    ctx.onFillEvent (started);

    Command likeLatest; likeLatest.type = CommandType::LikeLatest;
    Command skipRecipe; skipRecipe.type = CommandType::SkipRecipe; skipRecipe.recipe = recipeFor ((int) Algo::Crush, 1, 90, 4);
    ctx.onCommand (likeLatest);
    ctx.onCommand (skipRecipe);

    ctx.timerTick (1000, false, 0.5f);                          // learning off: ratings are not collected
    GF_EXPECT (svc.counts().likes == 0 && svc.counts().skips == 0);

    ctx.onCommand (likeLatest);
    ctx.onCommand (skipRecipe);
    ctx.timerTick (2000, true, 0.5f);
    GF_EXPECT (svc.counts().likes == 1 && svc.counts().skips == 1);

    // The library file is written after the 500 ms debounce.
    ctx.timerTick (2300, true, 0.5f);
    GF_EXPECT (! svc.store().file().existsAsFile());
    ctx.timerTick (2600, true, 0.5f);
    GF_EXPECT (svc.store().file().existsAsFile());

    // Undo removes the last rating.
    GF_EXPECT (svc.undoLast());
    GF_EXPECT (svc.counts().skips == 0 && svc.counts().likes == 1);
}

GF_TEST (Taste, library_cap_256_fifo_dedupe_and_atomic_write)
{
    auto file = tempFile ("library");
    RatingStore store (file);
    GF_EXPECT (store.load());                                    // a missing file is not an error

    for (int i = 0; i < 300; ++i)
        store.add (recipeFor (i % 15, -1, 50 + i, (std::uint32_t) i + 1), i % 2 ? 1 : -1);

    GF_EXPECT (store.entries().size() == 256);                  // FIFO eviction
    GF_EXPECT (store.entries().front().recipe.lengthMs == 50 + 44);

    // The same recipe again: the last rating wins and there is still one entry.
    const auto dup = recipeFor (3, -1, 1000, 77);
    store.add (dup, 1);
    store.add (dup, -1);
    int found = 0, rating = 0;

    for (const auto& e : store.entries())
        if (std::memcmp (&e.recipe, &dup, sizeof (FillRecipe)) == 0) { ++found; rating = e.rating; }

    GF_EXPECT (found == 1 && rating == -1);

    GF_EXPECT (store.save());
    GF_EXPECT (file.existsAsFile() && file.getSize() <= kMaxLibraryBytes);

    RatingStore again (file);
    GF_EXPECT (again.load() && again.entries().size() == store.entries().size());

    // No temporary file is left behind.
    int files = 0;

    for (const auto& f : file.getParentDirectory().findChildFiles (juce::File::findFiles, false))
        files += f.existsAsFile() ? 1 : 0;

    GF_EXPECT (files == 1);
}

GF_TEST (Taste, corrupt_oversized_or_partial_library_is_ignored_safely)
{
    auto file = tempFile ("corrupt");
    file.replaceWithText ("<Ratings v=\"1\"><R r=\"zz\" v=\"1\" s=\"1\"/><not xml");
    RatingStore bad (file);
    GF_EXPECT (! bad.load());
    GF_EXPECT (bad.entries().empty() && bad.notice().isNotEmpty());

    // An entry with a bad recipe is skipped, the rest is kept.
    RatingStore good (tempFile ("partial"));
    good.add (recipeFor (2, -1, 80, 5), 1);
    GF_EXPECT (good.save());
    auto text = good.file().loadFileAsString();
    text = text.replace ("</Ratings>", "<R r=\"abc\" v=\"1\" s=\"9\"/><R r=\"" + juce::String ("00").paddedRight ('0', 32) + "\" v=\"7\" s=\"10\"/></Ratings>");
    good.file().replaceWithText (text);
    RatingStore reread (good.file());
    GF_EXPECT (reread.load() && reread.entries().size() == 1);

    // Oversized files are refused.
    auto big = tempFile ("big");
    big.replaceWithText (juce::String::repeatedString ("x", (int) kMaxLibraryBytes + 10));
    RatingStore huge (big);
    GF_EXPECT (! huge.load() && huge.entries().empty());

    // A read-only target folder reports an error and does not throw.
    RatingStore nowhere (juce::File ("/proc/definitely/not/writable/ratings.xml"));
    nowhere.add (recipeFor (1), 1);
    GF_EXPECT (! nowhere.save() && nowhere.notice().isNotEmpty());
}

GF_TEST (Taste, two_instances_merge_by_recipe_bytes)
{
    auto file = tempFile ("merge");
    RatingStore a (file), b (file);
    a.load(); b.load();

    a.add (recipeFor (1, -1, 100, 1), 1);
    GF_EXPECT (a.save());
    b.add (recipeFor (2, -1, 110, 2), -1);
    GF_EXPECT (b.save());                                         // re-reads the file first and merges

    RatingStore merged (file);
    GF_EXPECT (merged.load() && merged.entries().size() == 2);

    // Undo does not resurrect the entry from the merge.
    GF_EXPECT (b.removeLast());
    GF_EXPECT (b.save());
    RatingStore after (file);
    after.load();
    GF_EXPECT (after.entries().size() == 1);
}

GF_TEST (Taste, reset_clears_profile_and_optionally_library)
{
    GapFillAudioProcessor proc;
    auto& svc = proc.getContextState().tasteService();
    svc.useLibraryFile (tempFile ("reset"));

    for (int i = 0; i < 5; ++i) { svc.rate (recipeFor (1, -1, 100 + i, (std::uint32_t) i + 1), 1); svc.rate (recipeFor (5, -1, 100 + i, (std::uint32_t) i + 40), -1); }

    GF_EXPECT (svc.ready());
    svc.resetProfile (false);
    GF_EXPECT (! svc.ready() && svc.projectProfile().likes == 0);
    GF_EXPECT (! svc.store().entries().empty());                  // the library survives

    svc.rebuildFromLibrary();                                     // a new instance rebuilds from the library
    GF_EXPECT (svc.ready());

    svc.resetProfile (true);
    GF_EXPECT (svc.store().entries().empty() && ! svc.ready());
}

GF_TEST (Taste, project_subtree_roundtrip_bad_subtree_dropped_and_old_state_off)
{
    GapFillAudioProcessor a;
    a.getContextState().tasteService().useLibraryFile (tempFile ("project"));
    GF_EXPECT (! serialiseTasteTree (a).isValid());              // nothing learned: nothing written

    auto& svc = a.getContextState().tasteService();

    for (int i = 0; i < 6; ++i) { svc.rate (recipeFor (0, -1, 100 + i, (std::uint32_t) i + 1), 1); svc.rate (recipeFor (13, -1, 100 + i, (std::uint32_t) i + 40), -1); }

    const auto tree = serialiseTasteTree (a);
    GF_EXPECT (tree.isValid() && validateTasteTree (tree));
    GF_EXPECT (tree.toXmlString().getNumBytesAsUTF8() <= 160);

    GapFillAudioProcessor b;
    applyTasteTree (b, tree);
    const auto p = b.getContextState().tasteService().projectProfile();
    GF_EXPECT (p.ready && p.likes == 6 && p.skips == 6);

    // The same saved profile gives the same multipliers on another machine.
    const auto ba = a.getContextState().tasteService().currentBias();
    const auto bb = b.getContextState().tasteService().currentBias();

    for (int i = 0; i < 15; ++i)
        GF_EXPECT (ba.algo[i] == bb.algo[i]);

    auto bad = tree.createCopy();
    bad.setProperty ("q", "1,2,3", nullptr);
    GF_EXPECT (! validateTasteTree (bad));
    bad = tree.createCopy(); bad.setProperty ("q", "99,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0", nullptr);
    GF_EXPECT (! validateTasteTree (bad));
    bad = tree.createCopy(); bad.setProperty ("likes", "70000", nullptr);
    GF_EXPECT (! validateTasteTree (bad));
    GF_EXPECT (! validateTasteTree (juce::ValueTree ("Other")));

    // Old states: the feature is off and nothing is learned.
    GapFillAudioProcessor fresh;
    GF_EXPECT (pv (fresh.getValueTreeState().getRawParameterValue ("fx05_on")) == 0.0f);
    GF_EXPECT_NEAR (pv (fresh.getValueTreeState().getRawParameterValue ("fx05_strength")), 50.0, 1.0e-6);
    GF_EXPECT (! fresh.getContextState().tasteService().ready());
    GF_EXPECT (! randomisableParameterIDs().contains ("fx05_on") && performanceParameterIDs().contains ("fx05_on"));
}

GF_TEST (Taste, off_and_not_ready_are_bit_identical_to_no_feature)
{
    auto render = [] (std::initializer_list<std::pair<const char*, float>> p)
    {
        GapFillAudioProcessor proc;
        proc.getContextState().tasteService().useLibraryFile (tempFile ("render"));
        setParams (proc, p);
        TestPlayHead ph;
        ph.bpm = goldenBpm;
        RenderOptions o;
        o.playHead = &ph;
        return renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, goldenBars), 512, goldenSampleRate, o).out;
    };

    const auto base = render ({ { "fill", 100.0f }, { "density", 100.0f } });
    const auto offStrong = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "fx05_on", 0.0f }, { "fx05_strength", 100.0f } });
    const auto onNotReady = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "fx05_on", 1.0f }, { "fx05_strength", 100.0f } });
    GF_EXPECT (buffersIdentical (base, offStrong));
    GF_EXPECT (buffersIdentical (base, onNotReady));
}

GF_TEST (Taste, a_ready_profile_changes_auto_choices_deterministically)
{
    auto render = [] (const ProjectProfile& profile)
    {
        GapFillAudioProcessor proc;
        proc.getContextState().tasteService().useLibraryFile (tempFile ("determinism"));
        proc.getContextState().tasteService().setProjectProfile (profile);
        setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "fx05_on", 1.0f } });
        TestPlayHead ph;
        ph.bpm = goldenBpm;
        RenderOptions o;
        o.playHead = &ph;
        return renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, goldenBars), 512, goldenSampleRate, o).out;
    };

    ProjectProfile p;
    p.ready = true; p.likes = 10; p.skips = 10;
    p.q[(int) Algo::Stutter] = 44;
    p.q[(int) Algo::Granular] = -44;

    const auto a = render (p);
    const auto b = render (p);
    GF_EXPECT (buffersIdentical (a, b));                          // same saved profile, same render

    const auto neutral = render ({});
    GF_EXPECT (! buffersIdentical (a, neutral));
}

GF_TEST (Taste, no_network_symbols_in_the_taste_sources)
{
    const auto dir = testDataDir().getParentDirectory().getChildFile ("Source").getChildFile ("features").getChildFile ("taste");
    GF_EXPECT (dir.isDirectory());

    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.*"))
    {
        const auto text = f.loadFileAsString();

        for (const char* banned : { "juce::URL", "WebInputStream", "StreamingSocket", "DatagramSocket", "curl", "http://", "https://", "NamedPipe" })
            GF_EXPECT_MSG (! text.contains (banned), f.getFileName() + " contains " + banned);
    }
}

GF_TEST (Taste, audio_thread_does_not_allocate_with_a_ready_profile)
{
    GapFillAudioProcessor proc;
    proc.getContextState().tasteService().useLibraryFile (tempFile ("alloc"));
    ProjectProfile p; p.ready = true; p.likes = 9; p.skips = 9; p.q[0] = 20; p.q[5] = -20;
    proc.getContextState().tasteService().setProjectProfile (p);
    setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "fx05_on", 1.0f } });

    TestPlayHead ph;
    RenderOptions o;
    o.playHead = &ph;
    renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, goldenBars), 512, goldenSampleRate, o);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block (2, 512);
    block.clear();

    AllocGuard guard;

    for (int i = 0; i < 400; ++i)
        proc.processBlock (block, midi);

    GF_EXPECT (guard.count() == 0);
}
