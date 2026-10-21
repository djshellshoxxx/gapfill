// F-S06 A/B compare with level matching (docs/dev/12-SPEC-BACKLOG-AND-NEW.md F-S06).

#include "../support/RecipeRigs.h"

#include "features/abcompare/MatchLevels.h"
#include "features/abcompare/RecipeAB.h"

using namespace gapfill;
using namespace gftest;
using namespace recipetest;
using namespace gapfill::abcompare;

namespace
{
    /** Feeds `seconds` of a steady stereo sine of `amplitude` through `meter` in 512-sample chunks. */
    void feedSine (MatchLevels& meter, double seconds, float amplitude, double sr = kSr)
    {
        std::vector<float> block ((size_t) kBlock);
        static double phase = 0.0;

        for (int done = 0; done < (int) (seconds * sr); done += kBlock)
        {
            for (auto& v : block)
            {
                v = amplitude * (float) std::sin (phase);
                phase += 2.0 * juce::MathConstants<double>::pi * 440.0 / sr;
            }

            meter.measure (block.data(), block.data(), kBlock);
        }
    }

    FillRecipe recipeOf (Algo algo, int lengthMs)
    {
        FillRecipe r;
        r.algo = (std::uint8_t) algo;
        r.band = -1;
        r.lengthMs = (std::uint16_t) lengthMs;
        r.lengthClass = 1;
        return r;
    }
}

GF_TEST (AbCompare, match_off_identity)
{
    const auto input = drumLoop (kSr, 150.0, 4);

    auto render = [&] (bool matchOn)
    {
        ProcRun run ({ { "fill", 90.0f }, { "density", 90.0f } });
        run.proc.getRecipes().match.setEnabled (matchOn);       // only ever measures side A: no trim exists
        juce::AudioBuffer<float> all (2, input.getNumSamples());
        all.clear();

        for (int i = 0; i * kBlock + kBlock <= input.getNumSamples(); ++i)
        {
            const auto& out = run.step (input);
            all.copyFrom (0, i * kBlock, out, 0, 0, kBlock);
            all.copyFrom (1, i * kBlock, out, 1, 0, kBlock);
        }

        GF_EXPECT (run.proc.getRecipes().match.getTrimDb() == 0.0f);
        return all;
    };

    const auto off = render (false), on = render (true);
    GF_EXPECT_MSG (compareBuffers (off, on, 0.0).firstBad < 0, "no second side measured: the output is bit-identical");
}

GF_TEST (AbCompare, trim_bounded_6dB)
{
    GF_EXPECT_NEAR (trimDbFor (1.0e-3f * 100.0f, 10.0, 1.0e-3f, 10.0), 6.0, 1.0e-6);       // +20 dB wanted
    GF_EXPECT_NEAR (trimDbFor (1.0e-3f / 100.0f, 10.0, 1.0e-3f, 10.0), -6.0, 1.0e-6);
    GF_EXPECT_NEAR (trimDbFor (1.38e-3f, 10.0, 1.0e-3f, 10.0), 1.4, 0.01);
    GF_EXPECT_NEAR (trimDbFor (1.0e-3f, 10.0, 1.0e-3f, 10.0), 0.0, 1.0e-6);
    GF_EXPECT (trimText (1.4f) == "+1.4 dB match");
    GF_EXPECT (trimText (-2.25f) == "-2.3 dB match" || trimText (-2.25f) == "-2.2 dB match");
    GF_EXPECT (trimText (0.0f).isEmpty());

    // Through the meter: B is 12 dB quieter than A, the trim saturates at +6 dB and stays there.
    MatchLevels meter;
    meter.prepare (kSr);
    meter.setEnabled (true);
    meter.setActiveSide (0);
    feedSine (meter, 4.0, 0.5f);
    meter.setActiveSide (1);
    feedSine (meter, 4.0, 0.125f);

    GF_EXPECT_NEAR (meter.getTrimDb(), 6.0, 1.0e-3);
    GF_EXPECT (meter.getSideSeconds (0) >= kMinDataSeconds && meter.getSideSeconds (1) >= kMinDataSeconds);

    // And the other way round: switching back to A asks for a cut of the same size.
    meter.setActiveSide (0);
    feedSine (meter, 2.0, 0.5f);
    GF_EXPECT_NEAR (meter.getTrimDb(), -6.0, 1.0e-3);

    // In range: 4 dB apart gives about 4 dB.
    MatchLevels near;
    near.prepare (kSr);
    near.setEnabled (true);
    near.setActiveSide (0);
    feedSine (near, 4.0, 0.4f);
    near.setActiveSide (1);
    feedSine (near, 4.0, 0.4f * std::pow (10.0f, -4.0f / 20.0f));
    GF_EXPECT_NEAR (near.getTrimDb(), 4.0, 0.1);
}

GF_TEST (AbCompare, silent_side_no_trim)
{
    GF_EXPECT (trimDbFor (0.0f, 10.0, 1.0e-3f, 10.0) == 0.0f);          // the other side measured nothing
    GF_EXPECT (trimDbFor (1.0e-3f, 10.0, 0.0f, 10.0) == 0.0f);
    GF_EXPECT (trimDbFor (1.0e-3f, 2.9, 1.0e-3f, 10.0) == 0.0f);        // under 3 s of data
    GF_EXPECT (trimDbFor (1.0e-3f, 10.0, 1.0e-3f, 2.9) == 0.0f);
    GF_EXPECT (trimDbFor (std::numeric_limits<float>::infinity(), 10.0, 1.0e-3f, 10.0) == 0.0f
               || trimDbFor (std::numeric_limits<float>::infinity(), 10.0, 1.0e-3f, 10.0) == 6.0f);

    // Windows below -60 dBFS are not measurements: a silent side stays unmeasured forever.
    MatchLevels meter;
    meter.prepare (kSr);
    meter.setEnabled (true);
    meter.setActiveSide (0);
    feedSine (meter, 5.0, 0.4f);
    meter.setActiveSide (1);
    feedSine (meter, 5.0, 0.0005f);                    // about -66 dBFS peak: below the gate
    GF_EXPECT (meter.getSideSeconds (1) == 0.0);
    GF_EXPECT (meter.getTrimDb() == 0.0f);
    GF_EXPECT (meter.getSideSeconds (0) > 3.0);

    // The same through the processor: fills on side A, nothing on side B.
    ProcRun run ({ { "fill", 100.0f }, { "density", 100.0f } });
    run.proc.getRecipes().match.setEnabled (true);
    const auto input = drumLoop (kSr, 150.0, 8);

    for (int i = 0; i * kBlock + kBlock <= input.getNumSamples() / 2; ++i)
        run.step (input);

    run.proc.getRecipes().noteHeaderAbSide (true);
    setParams (run.proc, { { "fill", 0.0f } });

    for (int i = 0; i < 400; ++i)
        run.step (input);

    GF_EXPECT_MSG (run.proc.getRecipes().match.getTrimDb() == 0.0f, "side B produced no wet signal: no trim is invented");
}

GF_TEST (AbCompare, trim_smoothed_click_free)
{
    MatchLevels meter;
    meter.prepare (kSr);
    meter.setEnabled (true);
    meter.setActiveSide (0);
    feedSine (meter, 4.0, 0.5f);
    meter.setActiveSide (1);

    // Measure side B in one go to move the target, then watch the gain glide.
    std::vector<float> block ((size_t) kBlock, 0.0f);
    float previous = meter.nextGain();
    float worstStep = 0.0f;
    int moving = 0;

    for (int chunk = 0; chunk < 400; ++chunk)
    {
        for (int i = 0; i < kBlock; ++i)
            block[(size_t) i] = 0.125f * (float) std::sin (0.0575 * (double) (chunk * kBlock + i));

        meter.measure (block.data(), block.data(), kBlock);

        for (int i = 0; i < kBlock; ++i)
        {
            const auto g = meter.nextGain();
            worstStep = juce::jmax (worstStep, std::abs (g - previous));
            moving += std::abs (g - previous) > 1.0e-7f ? 1 : 0;
            previous = g;
        }
    }

    GF_EXPECT_MSG (worstStep < 0.0006f, "no jump in the trim: worst step " + juce::String (worstStep));
    GF_EXPECT (moving > 100);                                           // it really glided
    GF_EXPECT_NEAR (previous, std::pow (10.0, 6.0 / 20.0), 0.01);

    // Switching the feature off ramps back to exactly 1.0.
    meter.setEnabled (false);
    meter.measure (block.data(), block.data(), kBlock);
    float last = 0.0f;

    for (int i = 0; i < (int) (0.08 * kSr); ++i)
        last = meter.nextGain();

    GF_EXPECT (last == 1.0f);
    GF_EXPECT (meter.getTrimDb() == 0.0f);
}

GF_TEST (AbCompare, not_saved_in_state)
{
    ProcRun a ({ { "fill", 100.0f }, { "density", 100.0f } });
    a.proc.getRecipes().match.setEnabled (true);
    const auto input = drumLoop (kSr, 150.0, 8);
    const auto half = input.getNumSamples() / 2;

    for (int i = 0; i * kBlock + kBlock <= half; ++i)
        a.step (input);

    a.proc.getRecipes().noteHeaderAbSide (true);
    setParams (a.proc, { { "wet", 40.0f } });

    for (int i = half / kBlock; i * kBlock + kBlock <= input.getNumSamples(); ++i)
        a.step (input);

    GF_EXPECT_MSG (a.proc.getRecipes().match.getTrimDb() != 0.0f, "a trim exists to be (not) saved");

    juce::MemoryBlock blob;
    a.proc.getStateInformation (blob);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
    GF_EXPECT (xml != nullptr);

    const auto* perf = xml->getChildByName ("Perf");
    GF_EXPECT (perf != nullptr && perf->getIntAttribute ("match") == 1);

    for (int i = 0; perf != nullptr && i < perf->getNumAttributes(); ++i)
        GF_EXPECT_MSG (! perf->getAttributeName (i).containsIgnoreCase ("trim"), "no trim attribute in state");

    GapFillAudioProcessor b;
    b.setStateInformation (blob.getData(), (int) blob.getSize());
    GF_EXPECT (b.getRecipes().match.isEnabled());
    GF_EXPECT (b.getRecipes().match.getTrimDb() == 0.0f);
    GF_EXPECT (b.getRecipes().match.getSideSeconds (0) == 0.0 && b.getRecipes().match.getSideSeconds (1) == 0.0);

    // Default: nothing is written at all.
    GapFillAudioProcessor c;
    juce::MemoryBlock plain;
    c.getStateInformation (plain);
    const auto plainXml = juce::AudioProcessor::getXmlFromBinary (plain.getData(), (int) plain.getSize());
    GF_EXPECT (plainXml->getChildByName ("Perf") == nullptr);

    b.setStateInformation (plain.getData(), (int) plain.getSize());
    GF_EXPECT_MSG (! b.getRecipes().match.isEnabled(), "a state without <Perf> switches matching off");
}

GF_TEST (AbCompare, preview_measures_its_own_level)
{
    EngineRig rig;
    const auto input = steadyNoise();

    for (int i = 0; i < 40; ++i)
        rig.step (input, i);

    GF_EXPECT (rig.engine.getPreviewDoneCount() == 0);
    rig.engine.requestPreviewRecipe (recipeOf (Algo::Stutter, 250), 100);

    for (int i = 40; i < 80; ++i)
        rig.step (input, i);

    GF_EXPECT (rig.engine.getPreviewDoneCount() == 1);
    GF_EXPECT (rig.engine.getLastPreviewRms() > 0.001f && rig.engine.getLastPreviewRms() < 1.0f);

    // A trim of -6 dB lowers the measured level by about that much.
    EngineRig quiet;

    for (int i = 0; i < 40; ++i)
        quiet.step (input, i);

    quiet.engine.requestPreviewRecipe (recipeOf (Algo::Stutter, 250), 100, -600);

    for (int i = 40; i < 80; ++i)
        quiet.step (input, i);

    GF_EXPECT_NEAR (quiet.engine.getLastPreviewRms() / rig.engine.getLastPreviewRms(), std::pow (10.0, -6.0 / 20.0), 0.02);
}

GF_TEST (AbCompare, recipe_ab_alternates_and_matches)
{
    RecipeAB ab;
    Command c;
    GF_EXPECT (! ab.next (c, 1));                               // nothing set yet

    ab.setSlot (0, recipeOf (Algo::Stutter, 300));
    GF_EXPECT (! ab.next (c, 1) && ! ab.isReady());
    ab.setSlot (1, recipeOf (Algo::Distort, 300));
    ab.setSlot (1, [] { FillRecipe bad; bad.algo = 99; return bad; }());      // invalid: ignored
    GF_EXPECT (ab.isReady() && ab.getSlot (1).algo == (std::uint8_t) Algo::Distort);

    ProcRun run ({ { "fill", 0.0f } });
    const auto input = steadyNoise (6.0);

    for (int i = 0; i < 40; ++i)
        run.step (input);

    auto& services = run.proc.getRecipes();
    services.recipeAB.setSlot (0, recipeOf (Algo::Stutter, 300));
    services.recipeAB.setSlot (1, recipeOf (Algo::Distort, 300));
    std::vector<int> played;
    std::vector<float> trims, measured;

    auto press = [&]
    {
        Command cmd;
        GF_EXPECT (services.recipeAB.next (cmd, 0));
        trims.push_back ((float) cmd.index / 100.0f);
        played.push_back (cmd.recipe.algo);
        GF_EXPECT (run.proc.postCommand (cmd));

        for (int i = 0; i < 60; ++i)
        {
            run.step (input);
            services.tick();                                    // the processor timer does this at 15 Hz
        }

        run.proc.drainFillEvents();
        GF_EXPECT (services.lastPreviewRecipe.algo == cmd.recipe.algo);
        measured.push_back (services.previewRms.load());
    };

    for (int i = 0; i < 4; ++i)
        press();

    GF_EXPECT_MSG (played == std::vector<int> ({ (int) Algo::Stutter, (int) Algo::Distort, (int) Algo::Stutter, (int) Algo::Distort }),
                   "A/B alternates the two recipes");
    GF_EXPECT (trims[0] == 0.0f && trims[1] == 0.0f);                    // nothing is known before both were heard
    GF_EXPECT (measured[0] > 0.0f && measured[1] > 0.0f);

    // From the third press on, each recipe is brought to the other's level (within the 6 dB clamp).
    const auto rawA = services.recipeAB.getRawRms (0), rawB = services.recipeAB.getRawRms (1);
    GF_EXPECT (rawA > 0.0f && rawB > 0.0f);

    const auto wantedTrimA = juce::jlimit (-6.0f, 6.0f, 20.0f * std::log10 (measured[1] / measured[0]));
    GF_EXPECT_NEAR (trims[2], wantedTrimA, 0.02);
    GF_EXPECT (std::abs (trims[2]) <= 6.0f && std::abs (trims[3]) <= 6.0f);

    const auto before = std::abs (20.0 * std::log10 ((double) measured[0] / (double) measured[1]));
    const auto after = std::abs (20.0 * std::log10 ((double) measured[2] / (double) measured[1]));
    GF_EXPECT_MSG (after <= before + 0.5 || std::abs (trims[2]) >= 5.9f,
                   "the trimmed press is no further from the other recipe's level: " + juce::String (after) + " vs " + juce::String (before) + " dB");
}

GF_TEST (AbCompare, swap_does_not_mutate_automation)
{
    ProcRun run ({ { "fill", 80.0f }, { "density", 80.0f } });
    auto& state = run.proc.getValueTreeState();

    std::vector<float> before;

    for (const auto& id : allParameterIDs())
        before.push_back (state.getRawParameterValue (id)->load());

    const auto undoDepth = run.proc.getUndoDepth();
    run.proc.getRecipes().match.setEnabled (true);
    const auto input = drumLoop (kSr, 150.0, 3);

    for (int i = 0; i * kBlock + kBlock <= input.getNumSamples(); ++i)
    {
        if (i % 50 == 0)
            run.proc.getRecipes().noteHeaderAbSide ((i / 50) % 2 != 0);

        run.step (input);
    }

    int k = 0;

    for (const auto& id : allParameterIDs())
        GF_EXPECT_MSG (state.getRawParameterValue (id)->load() == before[(size_t) k++], "parameter " + id + " must not change on an A/B swap");

    GF_EXPECT_MSG (run.proc.getUndoDepth() == undoDepth, "swapping sides pushes no undo entry");
}
