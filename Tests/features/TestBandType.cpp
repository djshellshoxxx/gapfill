// C-9 Per-band fill type (docs/dev/04-SPEC-CREATIVE.md C-9).

#include "../support/TestRig.h"

#include <iostream>

using namespace gapfill;
using namespace gftest;

namespace
{
    struct Fill { int algo; int band; };

    struct Run
    {
        std::vector<Fill> fills;
        std::vector<std::int64_t> starts;
        int noAlgoFits = 0;
        juce::AudioBuffer<float> out;
    };

    Run render (std::initializer_list<std::pair<const char*, float>> params, int bars = 8)
    {
        GapFillAudioProcessor proc;
        setParams (proc, params);
        TestPlayHead ph;
        ph.bpm = goldenBpm;
        RenderOptions o;
        o.playHead = &ph;
        Run r;
        auto result = renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, bars), 512, goldenSampleRate, o);
        r.out = std::move (result.out);
        proc.drainFillEvents();
        r.noAlgoFits = proc.getFillEngine().getRejectCount (RejectReason::NoAlgoFits);

        for (int i = proc.getGapOutcomeLog().size() - 1; i >= 0; --i)
        {
            const auto& e = proc.getGapOutcomeLog().get (i);

            if (e.type == FillEventType::Started)
            {
                r.fills.push_back ({ e.recipe.algo, e.recipe.band });
                r.starts.push_back (e.pos);
            }
        }

        return r;
    }

    constexpr float kSpectral = 2.0f;                       // detMode choice index
    constexpr float typeOf (Algo a) { return (float) ((int) a + 1); }   // Auto = 0
}

GF_TEST (BandType, auto_is_identical_to_the_baseline)
{
    const auto base = render ({ { "fill", 90.0f }, { "detMode", kSpectral } });
    const auto autoTypes = render ({ { "fill", 90.0f }, { "detMode", kSpectral }, { "bandLowType", 0.0f },
                                      { "bandMidType", 0.0f }, { "bandHighType", 0.0f } });
    GF_EXPECT (! base.fills.empty());
    GF_EXPECT (base.starts == autoTypes.starts);
    GF_EXPECT (compareBuffers (base.out, autoTypes.out, 0.0).maxAbs == 0.0);
}

GF_TEST (BandType, forced_type_is_used_in_that_band_only)
{
    for (int band = 0; band < 3; ++band)
    {
        const char* ids[3] { "bandLowType", "bandMidType", "bandHighType" };
        const auto r = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "detMode", kSpectral }, { ids[band], typeOf (Algo::Stutter) } });
        GF_EXPECT_MSG (! r.fills.empty(), "no fills for band " + juce::String (band));

        int inBand = 0, otherBands = 0, otherNotStutter = 0;

        for (const auto& f : r.fills)
        {
            if (f.band == band)
            {
                ++inBand;
                GF_EXPECT_MSG (f.algo == (int) Algo::Stutter, "band " + juce::String (band) + " got algo " + juce::String (f.algo));
            }
            else
            {
                ++otherBands;
                otherNotStutter += f.algo != (int) Algo::Stutter ? 1 : 0;
            }
        }

        GF_EXPECT_MSG (inBand > 0, "band " + juce::String (band) + " never filled");
        GF_EXPECT_MSG (otherBands == 0 || otherNotStutter > 0, "other bands were forced too");
    }
}

GF_TEST (BandType, global_fill_type_wins_over_band_types)
{
    const auto r = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "detMode", kSpectral },
                              { "fillType", typeOf (Algo::Distort) }, { "bandLowType", typeOf (Algo::Stutter) },
                              { "bandMidType", typeOf (Algo::Stutter) }, { "bandHighType", typeOf (Algo::Stutter) } });
    GF_EXPECT (! r.fills.empty());

    for (const auto& f : r.fills)
        GF_EXPECT (f.algo == (int) Algo::Distort);
}

GF_TEST (BandType, too_short_reports_NoAlgoFits_without_substituting)
{
    // Spectral needs 0.6 * 180 = 108 ms; a 100 ms Max Fill can never hold it.
    const auto r = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "detMode", kSpectral }, { "maxFill", 100.0f }, { "minGap", 20.0f },
                              { "bandLowType", typeOf (Algo::Spectral) }, { "bandMidType", typeOf (Algo::Spectral) },
                              { "bandHighType", typeOf (Algo::Spectral) } });
    GF_EXPECT (r.noAlgoFits > 0);

    for (const auto& f : r.fills)
        GF_EXPECT (f.algo != (int) Algo::Spectral);

    GF_EXPECT (r.fills.empty());     // every lane is forced to an algorithm that cannot fit
}

GF_TEST (BandType, non_spectral_mode_ignores_the_band_types)
{
    for (float mode : { 0.0f, 1.0f })
    {
        const auto base = render ({ { "fill", 90.0f }, { "detMode", mode } });
        const auto types = render ({ { "fill", 90.0f }, { "detMode", mode }, { "bandLowType", typeOf (Algo::Crush) },
                                      { "bandMidType", typeOf (Algo::Crush) }, { "bandHighType", typeOf (Algo::Crush) } });
        GF_EXPECT (! base.fills.empty());
        GF_EXPECT (base.starts == types.starts);
        GF_EXPECT (compareBuffers (base.out, types.out, 0.0).maxAbs == 0.0);
    }
}

GF_TEST (BandType, not_randomised_and_state_round_trips)
{
    const auto perf = performanceParameterIDs();
    const auto random = randomisableParameterIDs();

    for (const char* id : { "bandLowType", "bandMidType", "bandHighType" })
    {
        GF_EXPECT (perf.contains (id));
        GF_EXPECT (! random.contains (id));
    }

    GapFillAudioProcessor a;
    setParams (a, { { "bandLowType", 5.0f }, { "bandMidType", 9.0f }, { "bandHighType", 15.0f } });
    juce::MemoryBlock block;
    a.getStateInformation (block);

    GapFillAudioProcessor b;
    b.setStateInformation (block.getData(), (int) block.getSize());
    GF_EXPECT (pi (b.getValueTreeState().getRawParameterValue ("bandLowType")) == 5);
    GF_EXPECT (pi (b.getValueTreeState().getRawParameterValue ("bandMidType")) == 9);
    GF_EXPECT (pi (b.getValueTreeState().getRawParameterValue ("bandHighType")) == 15);
}
