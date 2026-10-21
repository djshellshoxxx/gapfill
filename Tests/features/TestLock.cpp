// C-5 Fill Lock and C-11 More Like That
// (docs/dev/04-SPEC-CREATIVE.md C-5, C-11).

#include "../support/RecipeRigs.h"

#include "features/history/RecipeCodec.h"
#include "features/lock/LikeBias.h"
#include "features/lock/LockCommands.h"
#include "features/lock/LockStage.h"

#include <set>

using namespace gapfill;
using namespace gftest;
using namespace recipetest;

namespace
{
    const juce::AudioBuffer<float>& loop8()
    {
        static const auto loop = drumLoop (kSr, 150.0, 8);
        return loop;
    }

    FillRecipe stutterRecipe()
    {
        FillRecipe r;
        r.algo = (std::uint8_t) Algo::Stutter;
        r.band = -1;
        r.lengthMs = 200;
        r.lengthClass = 1;
        r.modifierMask = 0x03;
        r.seed = 1234;
        return r;
    }

    std::set<int> algosOf (const std::vector<FillRecipe>& v)
    {
        std::set<int> a;

        for (const auto& r : v)
            a.insert (r.algo);

        return a;
    }

    constexpr int kBlocks = 700;      // about 7.5 s of the 150 bpm loop
}

//==============================================================================
// C-5

GF_TEST (Lock, fill_lock_parameter_is_registered_neutral_and_a_performance_id)
{
    GapFillAudioProcessor proc;
    auto* p = proc.getValueTreeState().getParameter ("fl01_lock");
    GF_EXPECT (p != nullptr);
    GF_EXPECT (p != nullptr && p->getValue() == 0.0f && p->getNumSteps() == 2);
    GF_EXPECT (performanceParameterIDs().contains ("fl01_lock"));
    GF_EXPECT (! randomisableParameterIDs().contains ("fl01_lock"));
    GF_EXPECT (! creativeParameterIDs().contains ("fl01_lock"));
}

GF_TEST (Lock, lock_pins_character_across_gaps)
{
    // Control: without Lock the same material produces a variety of algorithms.
    EngineRig free;
    makeBusy (free.settings);
    const auto freeFills = runCollectingFills (free, loop8(), kBlocks);
    GF_EXPECT (freeFills.size() >= 8);
    GF_EXPECT_MSG (algosOf (freeFills).size() >= 3, "control run varies the algorithm");

    EngineRig locked;
    makeBusy (locked.settings);
    locked.settings.perf.lockOn = true;
    const auto fills = runCollectingFills (locked, loop8(), kBlocks);

    GF_EXPECT (fills.size() >= 8);

    for (size_t i = 1; i < fills.size(); ++i)
    {
        GF_EXPECT_MSG (fills[i].algo == fills[0].algo, "algo identical on fill " + juce::String ((int) i));
        GF_EXPECT (fills[i].band == fills[0].band);
        GF_EXPECT (fills[i].modifierMask == fills[0].modifierMask);
        GF_EXPECT (fills[i].pitchSemis == fills[0].pitchSemis);
        GF_EXPECT ((fills[i].flags & FillRecipe::flagFromLock) != 0);
    }
}

GF_TEST (Lock, lock_does_not_force_fill_each_gap)
{
    auto fillsWith = [] (float density, float fill)
    {
        EngineRig rig;
        makeBusy (rig.settings);
        rig.settings.density01 = density;
        rig.settings.fill01 = fill;
        rig.settings.perf.lockOn = true;
        rig.engine.lockRecipe (stutterRecipe());
        const auto fills = runCollectingFills (rig, loop8(), kBlocks);
        return std::make_pair ((int) fills.size(), rig.engine.getRejectCount (RejectReason::DensityRoll));
    };

    const auto busy = fillsWith (1.0f, 1.0f);
    const auto sparse = fillsWith (0.0f, 0.4f);
    GF_EXPECT (busy.first > 3);
    GF_EXPECT_MSG (sparse.second > 0, "the probability roll still applies under Lock");
    GF_EXPECT_MSG (sparse.first < busy.first, "Lock does not fill every gap");
}

GF_TEST (Lock, lock_uses_fresh_seed)
{
    EngineRig rig;
    makeBusy (rig.settings);
    rig.settings.perf.lockOn = true;
    rig.engine.lockRecipe (stutterRecipe());

    const auto fills = runCollectingFills (rig, loop8(), kBlocks);
    GF_EXPECT (fills.size() >= 6);

    std::set<std::uint32_t> seeds;

    for (const auto& r : fills)
        seeds.insert (r.seed);

    GF_EXPECT_MSG (seeds.size() == fills.size(), "every locked fill gets its own seed (no literal loops)");
}

GF_TEST (Lock, lock_adapts_length_to_short_gap)
{
    LockState lock;
    lock.setExplicit (stutterRecipe());          // 200 ms
    lock.engaged = true;

    EngineSettings s;
    s.perf.lockOn = true;
    GapContext ctx;
    ctx.key = 99;
    ctx.usable = (std::int64_t) (kSr * 0.100);       // only 100 ms of room

    FillPlan plan;
    GF_EXPECT (lockstage::apply (lock, plan, ctx, s, kSr, 40.0f) == RejectReason::None);
    GF_EXPECT (plan.lengthSamples == ctx.usable);
    GF_EXPECT (plan.recipe.lengthMs == 100);
    GF_EXPECT (plan.recipe.lengthClass == lengthClassFor (100.0f, s.maxFillMs));
    GF_EXPECT (plan.algoIndex == (int) Algo::Stutter);

    ctx.usable = (std::int64_t) (kSr * 1.0);         // plenty: the locked length is kept
    FillPlan roomy;
    GF_EXPECT (lockstage::apply (lock, roomy, ctx, s, kSr, 40.0f) == RejectReason::None);
    GF_EXPECT (roomy.recipe.lengthMs == 200);
}

GF_TEST (Lock, lock_skips_unfit_gap_with_reason)
{
    LockState lock;
    auto r = stutterRecipe();
    r.algo = (std::uint8_t) Algo::Spectral;
    r.lengthMs = 700;
    lock.setExplicit (r);
    lock.engaged = true;

    EngineSettings s;
    GapContext ctx;
    ctx.usable = (std::int64_t) (kSr * 0.060);       // 60 ms < 0.6 x 180 ms

    FillPlan plan;
    GF_EXPECT (lockstage::apply (lock, plan, ctx, s, kSr, 180.0f) == RejectReason::LockSkipped);
    GF_EXPECT_NEAR (plan.neededMs, 108.0f, 0.01);

    // Reachable from audio: short Max Fill + a locked Spectral recipe skip every gap.
    EngineRig rig;
    makeBusy (rig.settings);
    rig.settings.maxFillMs = 60.0f;
    rig.settings.minGapMs = 20.0f;
    rig.settings.perf.lockOn = true;
    rig.engine.lockRecipe (r);

    const auto fills = runCollectingFills (rig, loop8(), kBlocks);
    GF_EXPECT (fills.empty());
    GF_EXPECT_MSG (rig.engine.getRejectCount (RejectReason::LockSkipped) > 0, "LockSkipped is counted");
}

GF_TEST (Lock, lock_band_fallback_and_skip)
{
    LockState lock;
    auto r = stutterRecipe();
    r.band = 1;
    lock.setExplicit (r);
    lock.engaged = true;

    EngineSettings s;
    s.mode = DetMode::Spectral;
    GapContext ctx;
    ctx.usable = (std::int64_t) (kSr * 0.4);

    auto run = [&] (FillPlan& plan) { return lockstage::apply (lock, plan, ctx, s, kSr, 40.0f); };

    FillPlan a;
    a.band = 2;
    GF_EXPECT (run (a) == RejectReason::None && a.band == 1 && a.recipe.band == 1);        // locked band is usable

    s.bandOn[1] = false;                                                                  // locked band disabled
    FillPlan b;
    GF_EXPECT (run (b) == RejectReason::None && b.band == 0);                              // nearest: ties go low

    s.bandProb[0] = 0.0f;                                                                 // probability zero
    FillPlan c;
    GF_EXPECT (run (c) == RejectReason::None && c.band == 2);

    s.bandOn[2] = false;
    FillPlan d;
    GF_EXPECT (run (d) == RejectReason::NoBand);

    // Outside spectral mode the lane is dropped.
    s.mode = DetMode::Transient;
    FillPlan e;
    GF_EXPECT (run (e) == RejectReason::None && e.band == -1 && e.recipe.band == -1);
}

GF_TEST (Lock, lock_armed_captures_next_fill)
{
    EngineRig rig;
    makeBusy (rig.settings);
    rig.settings.perf.lockOn = true;

    std::vector<FillEvent> events;
    const auto fills = runCollectingFills (rig, loop8(), kBlocks, &events);
    GF_EXPECT (fills.size() >= 3);

    const auto& lock = rig.engine.getLockState();
    GF_EXPECT (lock.valid && ! lock.armed);
    GF_EXPECT (lock.recipe.algo == fills[0].algo);

    int lockSets = 0;

    for (const auto& e : events)
        if (e.type == FillEventType::LockSet)
        {
            ++lockSets;
            GF_EXPECT (e.recipe.algo == fills[0].algo);
        }

    GF_EXPECT_MSG (lockSets == 1, "one LockSet event is published for the captured fill");
}

GF_TEST (Lock, lock_engage_uses_the_latest_natural_fill)
{
    EngineRig rig;
    makeBusy (rig.settings);

    const auto fills = runCollectingFills (rig, loop8(), 330);
    GF_EXPECT (fills.size() >= 2);
    const auto latest = rig.engine.getLockState().lastNatural;
    GF_EXPECT (latest.algo == fills.back().algo && latest.seed == fills.back().seed);

    rig.settings.perf.lockOn = true;
    rig.step (loop8(), 330);
    GF_EXPECT (rig.engine.getLockState().valid);
    GF_EXPECT (rig.engine.getLockState().recipe.algo == fills.back().algo);
    GF_EXPECT (! rig.engine.getLockState().armed);
}

GF_TEST (Lock, lock_ignores_preview_recipes)
{
    EngineRig rig;
    rig.settings.fill01 = 0.0f;
    const auto input = steadyNoise();

    for (int i = 0; i < 40; ++i)
        rig.step (input, i);

    rig.engine.requestPreview (100);
    rig.step (input, 40);
    GF_EXPECT (rig.engine.isPreviewActive());
    GF_EXPECT_MSG (! rig.engine.getLockState().haveLast, "a preview is not a natural fill");

    rig.settings.perf.lockOn = true;
    rig.step (input, 41);
    GF_EXPECT (! rig.engine.getLockState().valid && rig.engine.getLockState().armed);
}

GF_TEST (Lock, unlock_returns_to_selection)
{
    EngineRig rig;
    makeBusy (rig.settings);
    rig.settings.perf.lockOn = true;
    rig.engine.lockRecipe (stutterRecipe());

    const auto locked = runCollectingFills (rig, loop8(), 350);
    GF_EXPECT (locked.size() >= 3);
    GF_EXPECT (algosOf (locked).size() == 1);

    rig.settings.perf.lockOn = false;
    std::vector<FillRecipe> freed;

    for (int i = 350; i < kBlocks; ++i)
    {
        rig.step (loop8(), i);

        for (const auto& e : drainEvents (rig.events))
            if (e.type == FillEventType::Started)
                freed.push_back (e.recipe);
    }

    GF_EXPECT (freed.size() >= 3);
    GF_EXPECT_MSG (algosOf (freed).size() >= 2, "normal selection returns immediately");
    GF_EXPECT_MSG (rig.engine.getLockState().valid, "the recipe is kept while disengaged");
    GF_EXPECT (! rig.engine.getLockState().substituting());

    // Re-engaging uses the LATEST fill, not the old lock.
    const auto latest = rig.engine.getLockState().lastNatural;
    rig.settings.perf.lockOn = true;
    rig.step (loop8(), kBlocks);
    GF_EXPECT (rig.engine.getLockState().recipe.algo == latest.algo);
}

GF_TEST (Lock, unlock_command_forgets_and_rearms)
{
    EngineRig rig;
    makeBusy (rig.settings);
    rig.settings.perf.lockOn = true;
    rig.engine.lockRecipe (stutterRecipe());

    for (int i = 0; i < 6; ++i)                    // the first blocks only fill the lookahead
        rig.step (loop8(), i);

    GF_EXPECT (rig.engine.getLockState().valid && rig.engine.getLockState().engaged);

    rig.engine.unlockRecipe();
    GF_EXPECT (! rig.engine.getLockState().valid && rig.engine.getLockState().armed);

    std::vector<FillEvent> events;
    runCollectingFills (rig, loop8(), 200, &events);

    bool cleared = false;

    for (const auto& e : events)
        cleared = cleared || e.type == FillEventType::LockCleared;

    GF_EXPECT (cleared);
}

GF_TEST (Lock, fill_type_forced_overrides_lock)
{
    EngineRig rig;
    makeBusy (rig.settings);
    rig.settings.perf.lockOn = true;
    rig.settings.fillTypeIndex = (int) Algo::Crush + 1;
    rig.engine.lockRecipe (stutterRecipe());

    GF_EXPECT (! lockstage::isActive (rig.engine.getLockState(), rig.settings));

    const auto fills = runCollectingFills (rig, loop8(), kBlocks);
    GF_EXPECT (fills.size() >= 4);

    for (const auto& r : fills)
        GF_EXPECT (r.algo == (std::uint8_t) Algo::Crush);
}

GF_TEST (Lock, lock_plays_a_zero_weight_algorithm)
{
    EngineRig rig;
    makeBusy (rig.settings);
    rig.settings.perf.lockOn = true;
    rig.settings.weights[(int) Algo::Stutter] = 0.0f;
    rig.engine.lockRecipe (stutterRecipe());

    const auto fills = runCollectingFills (rig, loop8(), kBlocks);
    GF_EXPECT (fills.size() >= 4);

    for (const auto& r : fills)
        GF_EXPECT (r.algo == (std::uint8_t) Algo::Stutter);
}

GF_TEST (Lock, lock_with_protection_never_overruns_next_event)
{
    // Whatever the locked length, apply never plans more than the room the scheduler measured.
    LockState lock;
    auto r = stutterRecipe();
    r.lengthMs = 1500;
    lock.setExplicit (r);
    lock.engaged = true;

    EngineSettings s;
    juce::Random rng (7);

    for (int i = 0; i < 500; ++i)
    {
        GapContext ctx;
        ctx.key = (std::uint64_t) i;
        ctx.usable = rng.nextInt (juce::Range<int> (1, (int) (kSr * 2.0)));

        FillPlan plan;
        const auto reason = lockstage::apply (lock, plan, ctx, s, kSr, 40.0f);

        if (reason == RejectReason::None)
            GF_EXPECT (plan.lengthSamples <= ctx.usable && plan.lengthSamples > 0);
        else
            GF_EXPECT (reason == RejectReason::LockSkipped);
    }

    // End to end: the wet tail of a locked fill never reaches the next protected event's hold.
    EngineRig rig;
    makeBusy (rig.settings);
    rig.settings.perf.lockOn = true;
    rig.engine.lockRecipe (r);
    auto fills = runCollectingFills (rig, loop8(), kBlocks);
    GF_EXPECT (! fills.empty());

    for (const auto& f : fills)
        GF_EXPECT (f.lengthMs <= (std::uint16_t) rig.settings.maxFillMs + 1);
}

GF_TEST (Lock, lock_state_roundtrip)
{
    const auto input = drumLoop (kSr, 150.0, 4);
    ProcRun a ({ { "fill", 100.0f }, { "density", 100.0f }, { "fl01_lock", 1.0f } });

    for (int i = 0; i * kBlock + kBlock <= input.getNumSamples(); ++i)
        a.step (input);

    a.proc.drainFillEvents();
    GF_EXPECT (a.proc.getRecipes().lockMirror.valid);
    const auto expected = a.proc.getRecipes().lockMirror.recipe;

    juce::MemoryBlock blob;
    a.proc.getStateInformation (blob);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
    GF_EXPECT (xml != nullptr && xml->getChildByName ("Lock") != nullptr);
    GF_EXPECT (xml->getChildByName ("Lock")->getStringAttribute ("recipe").length() == 24);

    ProcRun b ({});
    b.proc.setStateInformation (blob.getData(), (int) blob.getSize());
    GF_EXPECT (b.proc.getRecipes().lockMirror.valid);
    b.step (steadyNoise());                       // the LockRecipe command is drained at the next block

    const auto& lock = b.proc.getFillEngine().getLockState();
    GF_EXPECT (lock.valid && lock.explicitSet);
    GF_EXPECT (lock.recipe.algo == expected.algo && lock.recipe.band == expected.band
               && lock.recipe.modifierMask == expected.modifierMask && lock.recipe.lengthMs == expected.lengthMs);
}

GF_TEST (Lock, lock_corrupt_recipe_does_not_block_load)
{
    GapFillAudioProcessor src;
    setParams (src, { { "fill", 61.0f }, { "fl01_lock", 1.0f } });

    juce::MemoryBlock blob;
    src.getStateInformation (blob);
    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
    GF_EXPECT (xml != nullptr);

    auto* lockXml = xml->createNewChildElement ("Lock");
    lockXml->setAttribute ("valid", 1);
    lockXml->setAttribute ("recipe", "AAAAAAAAAAAAAAAAAAAAAAAA");          // right length, wrong checksum

    juce::MemoryBlock corrupted;
    juce::AudioProcessor::copyXmlToBinary (*xml, corrupted);

    GapFillAudioProcessor dst;
    dst.setStateInformation (corrupted.getData(), (int) corrupted.getSize());
    GF_EXPECT_NEAR (dst.getValueTreeState().getRawParameterValue ("fill")->load(), 61.0, 0.01);
    GF_EXPECT_NEAR (dst.getValueTreeState().getRawParameterValue ("fl01_lock")->load(), 1.0, 0.01);
    GF_EXPECT_MSG (! dst.getRecipes().lockMirror.valid, "the bad subtree is dropped, parameters still load");

    // Out-of-range fields behind a correct checksum are rejected too.
    FillRecipe bad;
    bad.algo = 99;
    std::uint8_t bytes[FillRecipe::serialisedBytes];
    toBytes (bad, bytes);
    juce::MemoryOutputStream raw;
    raw.write (bytes, sizeof (bytes));
    raw.writeByte ((char) recipecodec::checksum (bytes));
    FillRecipe out;
    GF_EXPECT (! recipecodec::decode (juce::Base64::toBase64 (raw.getData(), raw.getDataSize()), out));

    FillRecipe good = stutterRecipe();
    GF_EXPECT (recipecodec::decode (recipecodec::encode (good), out));
    GF_EXPECT (std::memcmp (&good, &out, sizeof (good)) == 0);
    GF_EXPECT (! recipecodec::decode ("", out) && ! recipecodec::decode ("AAAA", out));
}

GF_TEST (Lock, lock_restore_plays_nothing)
{
    GapFillAudioProcessor src;
    setParams (src, { { "fl01_lock", 1.0f } });
    src.getRecipes().lockMirror = { true, stutterRecipe() };
    juce::MemoryBlock blob;
    src.getStateInformation (blob);

    ProcRun run ({});
    run.proc.setStateInformation (blob.getData(), (int) blob.getSize());
    setParams (run.proc, { { "dry", 0.0f }, { "wet", 100.0f } });          // listen to the wet bus only
    const auto input = steadyNoise();                      // continuous audio: no gap, so no natural fill

    double energy = 0.0;

    for (int i = 0; i < 120; ++i)
        energy += regionEnergy (run.step (input), 0, kBlock);

    GF_EXPECT_MSG (energy < 1.0e-9, "restoring a lock does not play anything");
    GF_EXPECT (run.proc.getFillEngine().getLockState().valid);
}

GF_TEST (Lock, lock_off_changes_nothing_in_the_engine)
{
    // With Lock off the stage is the identity: the same run with and without a stored recipe is identical.
    EngineRig a, b;
    makeBusy (a.settings);
    makeBusy (b.settings);
    b.engine.lockRecipe (stutterRecipe());

    const auto fa = runCollectingFills (a, loop8(), kBlocks);
    const auto fb = runCollectingFills (b, loop8(), kBlocks);
    GF_EXPECT (fa.size() == fb.size());

    for (size_t i = 0; i < fa.size() && i < fb.size(); ++i)
        GF_EXPECT (std::memcmp (&fa[i], &fb[i], sizeof (FillRecipe)) == 0);
}

GF_TEST (Lock, natural_recipes_record_what_the_voice_decided)
{
    EngineRig rig;
    makeBusy (rig.settings);
    rig.settings.complexity01 = 0.9f;
    const auto fills = runCollectingFills (rig, loop8(), kBlocks);
    GF_EXPECT (fills.size() >= 6);

    bool anyModifier = false;

    for (const auto& r : fills)
    {
        GF_EXPECT (isValid (r));
        anyModifier = anyModifier || r.modifierMask != 0;

        if (r.algo != (std::uint8_t) Algo::PitchRise && r.algo != (std::uint8_t) Algo::PitchFall)
            GF_EXPECT (r.pitchSemis == 0);
    }

    GF_EXPECT_MSG (anyModifier, "high complexity stacks modifiers and the recipe records them");
}

//==============================================================================
// C-11

namespace
{
    /** First started fill's algorithm for a given seed, with or without a Like of Stutter. */
    int firstAlgo (int seed, bool liked, int liveWeightsOnly = -1)
    {
        EngineRig rig;
        makeBusy (rig.settings);
        rig.settings.seed = seed;
        rig.settings.chaos01 = 0.8f;

        if (liveWeightsOnly >= 0)
            for (int i = 0; i < numAlgos; ++i)
                rig.settings.weights[i] = i == liveWeightsOnly ? 0.0f : 0.7f;

        if (liked)
            rig.engine.likeRecipe (stutterRecipe());

        const auto fills = runCollectingFills (rig, loop8(), 260);
        return fills.empty() ? -1 : fills.front().algo;
    }
}

GF_TEST (Like, family_table_complete)
{
    int counts[4] {};

    for (int i = 0; i < numAlgos; ++i)
        ++counts[(int) kAlgoFamilies[(size_t) i]];

    GF_EXPECT (counts[(int) AlgoFamily::Rhythmic] == 3);
    GF_EXPECT (counts[(int) AlgoFamily::Textural] == 5);
    GF_EXPECT (counts[(int) AlgoFamily::PitchTape] == 4);
    GF_EXPECT (counts[(int) AlgoFamily::Destructive] == 3);
    GF_EXPECT (kAlgoFamilies[(size_t) Algo::Delay] == kAlgoFamilies[(size_t) Algo::Stutter]);
    GF_EXPECT (kAlgoFamilies[(size_t) Algo::Comb] == AlgoFamily::Textural);
    GF_EXPECT (kAlgoFamilies[(size_t) Algo::Filter] == AlgoFamily::PitchTape);
    GF_EXPECT (kAlgoFamilies[(size_t) Algo::Reverse] == AlgoFamily::Destructive);

    LikeState like;
    like.start (stutterRecipe());
    GF_EXPECT (like.algoFactor ((int) Algo::Stutter) == LikeState::sameAlgo);
    GF_EXPECT (like.algoFactor ((int) Algo::Scramble) == LikeState::sameFamily);
    GF_EXPECT (like.algoFactor ((int) Algo::Crush) == 1.0f);
    GF_EXPECT (LikeState {}.algoFactor ((int) Algo::Stutter) == 1.0f);
}

GF_TEST (Like, like_biases_but_not_forces)
{
    int plain = 0, liked = 0, total = 0;

    for (int seed = 1; seed <= 60; ++seed)
    {
        const auto a = firstAlgo (seed, false);
        const auto b = firstAlgo (seed, true);

        if (a < 0 || b < 0)
            continue;

        ++total;
        plain += a == (int) Algo::Stutter ? 1 : 0;
        liked += b == (int) Algo::Stutter ? 1 : 0;
    }

    GF_EXPECT (total >= 50);
    GF_EXPECT_MSG (liked > plain, "Stutter share rises with a Like: " + juce::String (plain) + " -> " + juce::String (liked));
    GF_EXPECT_MSG (liked < total, "a Like biases, it never forces");
}

GF_TEST (Like, like_expires_after_four_started)
{
    EngineRig rig;
    makeBusy (rig.settings);
    rig.engine.likeRecipe (stutterRecipe());
    GF_EXPECT (rig.engine.getLikeState().remaining == 4);

    int started = 0;

    for (int i = 0; i < kBlocks && started < 6; ++i)
    {
        rig.step (loop8(), i);

        for (const auto& e : drainEvents (rig.events))
            if (e.type == FillEventType::Started)
            {
                ++started;
                GF_EXPECT_MSG (rig.engine.getLikeState().remaining == juce::jmax (0, 4 - started),
                               "remaining after started fill " + juce::String (started));

                const bool biased = (e.recipe.flags & FillRecipe::flagBiased) != 0;
                GF_EXPECT (biased == (started <= 4));
            }
    }

    GF_EXPECT (started >= 5);
}

GF_TEST (Like, rejected_gaps_dont_count)
{
    EngineRig rig;
    makeBusy (rig.settings);
    rig.settings.fill01 = 0.0f;                 // every gap is rejected (FillOff)
    rig.engine.likeRecipe (stutterRecipe());

    runCollectingFills (rig, loop8(), 400);
    GF_EXPECT (rig.engine.getRejectCount (RejectReason::FillOff) > 3);
    GF_EXPECT (rig.engine.getLikeState().remaining == 4);
}

GF_TEST (Like, preview_doesnt_count)
{
    EngineRig rig;
    rig.settings.fill01 = 0.0f;
    const auto input = steadyNoise();
    rig.engine.likeRecipe (stutterRecipe());

    for (int i = 0; i < 40; ++i)
        rig.step (input, i);

    rig.engine.requestPreview (100);
    rig.step (input, 40);
    GF_EXPECT (rig.engine.isPreviewActive());
    GF_EXPECT (rig.engine.getLikeState().remaining == 4);
}

GF_TEST (Like, transport_jump_resets)
{
    FillEngine engine;
    engine.prepare (kSr, kBlock, 2);
    engine.likeRecipe (stutterRecipe());
    GF_EXPECT (engine.getLikeState().remaining == 4);

    engine.noteTransportJump();
    GF_EXPECT (engine.getLikeState().remaining == 0);

    engine.likeRecipe (stutterRecipe());          // a new Like restarts the count
    engine.likeRecipe (stutterRecipe());
    GF_EXPECT (engine.getLikeState().remaining == 4);
}

GF_TEST (Like, zero_weight_stays_zero)
{
    for (int seed = 1; seed <= 25; ++seed)
        GF_EXPECT_MSG (firstAlgo (seed, true, (int) Algo::Stutter) != (int) Algo::Stutter,
                       "a liked algorithm with weight 0 is never played (seed " + juce::String (seed) + ")");
}

GF_TEST (Like, like_ignored_when_locked)
{
    auto run = [] (bool liked)
    {
        EngineRig rig;
        makeBusy (rig.settings);
        rig.settings.perf.lockOn = true;
        rig.engine.lockRecipe (stutterRecipe());

        if (liked)
            rig.engine.likeRecipe (stutterRecipe());

        return runCollectingFills (rig, loop8(), kBlocks);
    };

    const auto a = run (false), b = run (true);
    GF_EXPECT (a.size() == b.size() && ! a.empty());

    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
    {
        GF_EXPECT (std::memcmp (&a[i], &b[i], sizeof (FillRecipe)) == 0);
        GF_EXPECT ((b[i].flags & FillRecipe::flagBiased) == 0);
    }
}

GF_TEST (Like, draw_count_unchanged)
{
    // The bias only scales weights, so the draws that precede the algorithm (and the
    // gap decisions keyed by position) are identical: the first fill has the same length class.
    int compared = 0;

    for (int seed = 1; seed <= 20; ++seed)
    {
        FillRecipe first[2];
        bool ok[2] = { false, false };

        for (int liked = 0; liked < 2; ++liked)
        {
            EngineRig rig;
            makeBusy (rig.settings);
            rig.settings.seed = seed;

            if (liked != 0)
                rig.engine.likeRecipe (stutterRecipe());

            const auto fills = runCollectingFills (rig, loop8(), 260);

            if (! fills.empty())
            {
                first[liked] = fills.front();
                ok[liked] = true;
            }
        }

        if (ok[0] && ok[1])
        {
            ++compared;
            GF_EXPECT_MSG (first[0].lengthMs == first[1].lengthMs || first[0].algo != first[1].algo,
                           "same gap, same length draw (seed " + juce::String (seed) + ")");
        }
    }

    GF_EXPECT (compared >= 10);
}

GF_TEST (Like, like_commands_reach_the_engine)
{
    FillEngine engine;
    engine.prepare (kSr, kBlock, 2);

    Command c;
    c.type = CommandType::LikeRecipe;
    c.recipe = stutterRecipe();
    GF_EXPECT (lock::handleCommand (engine, c));
    GF_EXPECT (engine.getLikeState().remaining == 4);

    c.type = CommandType::LockRecipe;
    GF_EXPECT (lock::handleCommand (engine, c));
    GF_EXPECT (engine.getLockState().valid && engine.getLockState().explicitSet);

    c.type = CommandType::Unlock;
    GF_EXPECT (lock::handleCommand (engine, c));
    GF_EXPECT (! engine.getLockState().valid);

    c.type = CommandType::Preview;
    GF_EXPECT (! lock::handleCommand (engine, c));
}
