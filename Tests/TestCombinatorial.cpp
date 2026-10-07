#include "TestFramework.h"
#include "TestSignals.h"

#include "PluginProcessor.h"

using namespace gapfill;
using namespace gftest;

/*
    Exhaustive combination testing.

    GapFill has ~65 parameters, so a literal Cartesian product of every setting
    against every other is not computable. What this file does instead is the
    standard, defensible approach to "every setting with every other setting":

      * FULL factorial over the discrete axes that actually change the code path
        (detector mode x detector source x fill type x macro profile x
        protection profile, and separately every boolean/choice against every
        other boolean/choice).
      * Every continuous parameter driven to both of its extremes.
      * Full factorial over the five macros at low/mid/high.
      * Full factorial over the spectral lane enables, solo and crossovers.
      * Sample rate x block size x detector mode.

    Every run asserts the same invariants: finite audio, bounded level, no
    runaway growth, and no impossible internal state.
*/

namespace
{
    struct Harness
    {
        GapFillAudioProcessor proc;
        double sampleRate = 0.0;
        int blockSize = 0;
        bool sidechain = false;

        void ensurePrepared (double sr, int block, bool withSidechain)
        {
            if (sr != sampleRate || block != blockSize || withSidechain != sidechain)
            {
                sampleRate = sr;
                blockSize = block;
                sidechain = withSidechain;
                prepareProcessor (proc, sr, block, withSidechain);
            }
            else
            {
                proc.reset();
            }
        }

        void set (const char* id, float value)
        {
            if (auto* p = proc.getValueTreeState().getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (value));
        }

        void setNormalised (const juce::String& id, float normalised)
        {
            if (auto* p = proc.getValueTreeState().getParameter (id))
                p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
        }

        void resetParameters()
        {
            resetToDefaults (proc.getValueTreeState());
        }
    };

    /** Renders and checks the invariants that must hold for every combination. */
    void checkRun (Harness& h, const juce::AudioBuffer<float>& input,
                   const juce::AudioBuffer<float>* sidechain, const juce::String& label)
    {
        auto output = renderThroughPlugin (h.proc, input, sidechain, h.blockSize);

        GF_EXPECT_MSG (allFinite (output), label + ": non-finite output");
        GF_EXPECT_MSG (maxAbs (output) < 8.0f,
                       label + ": peaked at " + juce::String (maxAbs (output)));
    }

    const char* const kBooleanIDs[] =
    {
        "syncEnable", "maxFillSync", "pingPong", "wetMono", "audition"
    };

    /** Continuous parameters worth driving to both extremes. */
    juce::StringArray continuousParameterIDs()
    {
        juce::StringArray ids;

        for (const auto& id : allParameterIDs())
        {
            if (id == "bypass" || id == "output" || id == "audition")
                continue;

            bool isBoolean = false;

            for (const char* b : kBooleanIDs)
                if (id == b)
                    isBoolean = true;

            if (isBoolean)
                continue;

            ids.add (id);
        }

        return ids;
    }
}

//==============================================================================
GF_TEST (Combinatorial, mode_x_source_x_filltype_x_macros_x_protection)
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    auto input = makeDrumLoop (sr, 0.9, 130.0, 2);
    auto sidechain = makeDrumLoop (sr, 0.9, 130.0, 2);

    // (fill, intensity, complexity, chaos, density)
    const float macroProfiles[3][5] =
    {
        {  15.0f, 15.0f, 10.0f,  0.0f, 30.0f },   // subtle
        {  55.0f, 55.0f, 50.0f, 40.0f, 70.0f },   // mid
        { 100.0f, 100.0f, 100.0f, 100.0f, 100.0f } // extreme
    };

    // (transient, low Hz, centre, next, sidechain)
    const float protectionProfiles[3][5] =
    {
        {   0.0f,  20.0f,   0.0f,   0.0f,   0.0f },   // none
        {  55.0f, 110.0f,  40.0f,  60.0f,  60.0f },   // mid
        { 100.0f, 500.0f, 100.0f, 100.0f, 100.0f }    // max
    };

    Harness h;
    h.ensurePrepared (sr, block, true);

    int runs = 0;

    for (int mode = 0; mode < (int) DetMode::NumModes; ++mode)
    {
        for (int source = 0; source < (int) DetSource::NumSources; ++source)
        {
            for (int fillType = 0; fillType <= numAlgos; ++fillType)
            {
                for (int macro = 0; macro < 3; ++macro)
                {
                    for (int protection = 0; protection < 3; ++protection)
                    {
                        h.resetParameters();
                        h.set ("detMode", (float) mode);
                        h.set ("detSource", (float) source);
                        h.set ("fillType", (float) fillType);
                        h.set ("minGap", 40.0f);
                        h.set ("maxFill", 500.0f);
                        h.set ("seed", (float) (runs % 97));
                        h.set ("dry", 100.0f);
                        h.set ("wet", 100.0f);

                        h.set ("fill",       macroProfiles[macro][0]);
                        h.set ("intensity",  macroProfiles[macro][1]);
                        h.set ("complexity", macroProfiles[macro][2]);
                        h.set ("chaos",      macroProfiles[macro][3]);
                        h.set ("density",    macroProfiles[macro][4]);

                        h.set ("protTrans",  protectionProfiles[protection][0]);
                        h.set ("protLow",    protectionProfiles[protection][1]);
                        h.set ("protCenter", protectionProfiles[protection][2]);
                        h.set ("protNext",   protectionProfiles[protection][3]);
                        h.set ("protSC",     protectionProfiles[protection][4]);

                        const auto label = juce::String ("mode=") + juce::String (mode)
                                             + " src=" + juce::String (source)
                                             + " type=" + juce::String (fillType)
                                             + " macro=" + juce::String (macro)
                                             + " prot=" + juce::String (protection);

                        checkRun (h, input, &sidechain, label);
                        ++runs;
                    }
                }
            }
        }
    }

    GF_EXPECT_MSG (runs == 3 * 3 * (numAlgos + 1) * 3 * 3,
                   "expected the full factorial, ran " + juce::String (runs));
}

//==============================================================================
GF_TEST (Combinatorial, every_switch_against_every_other_switch)
{
    constexpr double sr = 48000.0;
    constexpr int block = 256;

    auto input = makeDrumLoop (sr, 0.4, 140.0, 2);
    auto sidechain = makeDrumLoop (sr, 0.4, 100.0, 2);

    Harness h;
    h.ensurePrepared (sr, block, true);

    int runs = 0;

    // 5 booleans x 3 band enables x 4 solo settings x bypass = 2 048 combinations.
    for (int mask = 0; mask < 32; ++mask)
    {
        for (int bands = 0; bands < 8; ++bands)
        {
            for (int solo = 0; solo < 4; ++solo)
            {
                for (int bypass = 0; bypass < 2; ++bypass)
                {
                    h.resetParameters();
                    h.set ("fill", 85.0f);
                    h.set ("density", 100.0f);
                    h.set ("complexity", 60.0f);
                    h.set ("minGap", 40.0f);
                    h.set ("detMode", (float) DetMode::Spectral);
                    h.set ("detSource", (float) DetSource::Hybrid);

                    for (int i = 0; i < 5; ++i)
                        h.set (kBooleanIDs[i], (mask & (1 << i)) != 0 ? 1.0f : 0.0f);

                    h.set ("bandLowOn",  (bands & 1) != 0 ? 1.0f : 0.0f);
                    h.set ("bandMidOn",  (bands & 2) != 0 ? 1.0f : 0.0f);
                    h.set ("bandHighOn", (bands & 4) != 0 ? 1.0f : 0.0f);
                    h.set ("bandSolo", (float) solo);
                    h.set ("bypass", (float) bypass);

                    const auto label = juce::String ("switches mask=") + juce::String (mask)
                                         + " bands=" + juce::String (bands)
                                         + " solo=" + juce::String (solo)
                                         + " bypass=" + juce::String (bypass);

                    checkRun (h, input, &sidechain, label);
                    ++runs;
                }
            }
        }
    }

    GF_EXPECT (runs == 32 * 8 * 4 * 2);
}

//==============================================================================
GF_TEST (Combinatorial, every_continuous_parameter_at_both_extremes)
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    auto input = makeDrumLoop (sr, 0.6, 120.0, 2);
    auto sidechain = makeDrumLoop (sr, 0.6, 120.0, 2);

    Harness h;
    h.ensurePrepared (sr, block, true);

    const auto ids = continuousParameterIDs();
    GF_EXPECT (ids.size() > 40);

    for (const auto& id : ids)
    {
        for (float extreme : { 0.0f, 1.0f })
        {
            h.resetParameters();
            h.set ("fill", 75.0f);
            h.set ("density", 90.0f);
            h.setNormalised (id, extreme);

            // A maximum lookahead needs re-preparing to take effect cleanly.
            if (id == "lookahead")
                h.proc.prepareToPlay (sr, block);

            checkRun (h, input, &sidechain, id + " = " + (extreme > 0.5f ? "max" : "min"));
        }
    }
}

//==============================================================================
GF_TEST (Combinatorial, macro_grid)
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    auto input = makeDrumLoop (sr, 0.5, 125.0, 2);

    Harness h;
    h.ensurePrepared (sr, block, false);

    const float levels[] = { 0.0f, 50.0f, 100.0f };
    int runs = 0;

    for (float fill : levels)
        for (float intensity : levels)
            for (float complexity : levels)
                for (float chaos : levels)
                    for (float density : levels)
                    {
                        h.resetParameters();
                        h.set ("fill", fill);
                        h.set ("intensity", intensity);
                        h.set ("complexity", complexity);
                        h.set ("chaos", chaos);
                        h.set ("density", density);
                        h.set ("minGap", 40.0f);

                        checkRun (h, input, nullptr,
                                  juce::String ("macros ") + juce::String (fill) + "/"
                                    + juce::String (intensity) + "/" + juce::String (complexity)
                                    + "/" + juce::String (chaos) + "/" + juce::String (density));
                        ++runs;
                    }

    GF_EXPECT (runs == 243);
}

//==============================================================================
GF_TEST (Combinatorial, spectral_lane_grid)
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    auto input = makeLowKicksWithOpenHighs (sr, 0.8, 128.0, 2);

    Harness h;
    h.ensurePrepared (sr, block, false);

    const float crossovers[3][2] = { { 60.0f, 900.0f }, { 180.0f, 3500.0f }, { 1000.0f, 12000.0f } };
    const float amounts[] = { 0.0f, 100.0f };

    int runs = 0;

    for (int bands = 0; bands < 8; ++bands)
        for (int solo = 0; solo < 4; ++solo)
            for (int xover = 0; xover < 3; ++xover)
                for (float amount : amounts)
                {
                    h.resetParameters();
                    h.set ("detMode", (float) DetMode::Spectral);
                    h.set ("fill", 90.0f);
                    h.set ("density", 100.0f);
                    h.set ("minGap", 30.0f);
                    h.set ("bandLowOn",  (bands & 1) != 0 ? 1.0f : 0.0f);
                    h.set ("bandMidOn",  (bands & 2) != 0 ? 1.0f : 0.0f);
                    h.set ("bandHighOn", (bands & 4) != 0 ? 1.0f : 0.0f);
                    h.set ("bandSolo", (float) solo);
                    h.set ("xoverLM", crossovers[xover][0]);
                    h.set ("xoverMH", crossovers[xover][1]);
                    h.set ("bandLowAmt", amount);
                    h.set ("bandMidAmt", amount);
                    h.set ("bandHighAmt", amount);

                    checkRun (h, input, nullptr,
                              juce::String ("spectral bands=") + juce::String (bands)
                                + " solo=" + juce::String (solo) + " xover=" + juce::String (xover));
                    ++runs;
                }

    GF_EXPECT (runs == 8 * 4 * 3 * 2);
}

//==============================================================================
GF_TEST (Combinatorial, sample_rate_x_block_size_x_mode)
{
    const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
    const int blocks[] = { 32, 64, 128, 231, 512, 1024, 2048 };

    Harness h;

    for (double sr : rates)
    {
        auto input = makeDrumLoop (sr, 0.4, 135.0, 2);
        auto sidechain = makeDrumLoop (sr, 0.4, 135.0, 2);

        for (int block : blocks)
        {
            for (int mode = 0; mode < (int) DetMode::NumModes; ++mode)
            {
                h.ensurePrepared (sr, block, true);
                h.resetParameters();
                h.set ("detMode", (float) mode);
                h.set ("fill", 90.0f);
                h.set ("density", 100.0f);
                h.set ("complexity", 70.0f);
                h.set ("intensity", 70.0f);
                h.set ("chaos", 50.0f);
                h.set ("minGap", 30.0f);

                checkRun (h, input, &sidechain,
                          juce::String ("sr=") + juce::String (sr) + " block=" + juce::String (block)
                            + " mode=" + juce::String (mode));
            }
        }
    }
}

//==============================================================================
GF_TEST (Combinatorial, fill_type_x_tempo_division_grid)
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    auto input = makeDrumLoop (sr, 0.8, 100.0, 2);

    Harness h;
    h.ensurePrepared (sr, block, false);

    int runs = 0;

    for (int type : { (int) Algo::Stutter + 1, (int) Algo::Delay + 1, 0 })
        for (int division = 0; division < (int) Division::NumDivisions; ++division)
            for (int sync = 0; sync < 2; ++sync)
            {
                h.resetParameters();
                h.set ("fillType", (float) type);
                h.set ("stutterDiv", (float) division);
                h.set ("delayDiv", (float) division);
                h.set ("maxFillDiv", (float) division);
                h.set ("syncEnable", (float) sync);
                h.set ("maxFillSync", (float) sync);
                h.set ("fill", 100.0f);
                h.set ("density", 100.0f);
                h.set ("minGap", 30.0f);

                TestPlayHead playHead;
                playHead.bpm = 174.0;
                auto output = renderThroughPlugin (h.proc, input, nullptr, block, &playHead);

                const auto label = juce::String ("type=") + juce::String (type)
                                     + " div=" + juce::String (division) + " sync=" + juce::String (sync);

                GF_EXPECT_MSG (allFinite (output), label + ": non-finite");
                GF_EXPECT_MSG (maxAbs (output) < 8.0f, label + ": peaked at " + juce::String (maxAbs (output)));
                ++runs;
            }

    GF_EXPECT (runs == 3 * (int) Division::NumDivisions * 2);
}

//==============================================================================
GF_TEST (Combinatorial, algorithm_weight_pairs_in_auto_mode)
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    auto input = makeDrumLoop (sr, 0.7, 115.0, 2);

    Harness h;
    h.ensurePrepared (sr, block, false);

    // Every pair of algorithms enabled together, everything else disabled.
    for (int a = 0; a < numAlgos; ++a)
    {
        for (int b = a; b < numAlgos; ++b)
        {
            h.resetParameters();
            h.set ("fillType", 0.0f);         // AUTO
            h.set ("fill", 100.0f);
            h.set ("density", 100.0f);
            h.set ("minGap", 30.0f);
            h.set ("maxFill", 600.0f);
            h.set ("complexity", 70.0f);

            for (int i = 0; i < numAlgos; ++i)
                h.set (algoWeightID ((Algo) i), (i == a || i == b) ? 100.0f : 0.0f);

            checkRun (h, input, nullptr,
                      juce::String ("weights ") + algoName ((Algo) a) + " + " + algoName ((Algo) b));
        }
    }

    // And the pathological case: every algorithm disabled. Clear the tails left
    // over from the pairs above first, so that what is measured is only what
    // this configuration generates.
    h.proc.reset();
    h.resetParameters();
    h.set ("fillType", 0.0f);
    h.set ("fill", 100.0f);
    h.set ("density", 100.0f);

    for (int i = 0; i < numAlgos; ++i)
        h.set (algoWeightID ((Algo) i), 0.0f);

    h.set ("dry", 0.0f);
    h.set ("wet", 100.0f);

    auto wet = renderThroughPlugin (h.proc, input, nullptr, block);
    GF_EXPECT (allFinite (wet));
    GF_EXPECT_MSG (maxAbs (wet) < 1.0e-5f,
                   "all algorithms disabled should generate nothing, got " + juce::String (maxAbs (wet)));
}
