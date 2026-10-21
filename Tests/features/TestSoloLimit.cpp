// Band Solo monitor limiter (QA report 2026-10-21): identity at normal levels, bounded at +6 dBFS.

#include "../support/TestRig.h"

#include "dsp/SoloLimit.h"

#include <cmath>
#include <limits>

using namespace gapfill;
using namespace gftest;

GF_TEST (SoloLimit, identity_up_to_full_scale_and_bounded_above)
{
    // Normal levels pass unchanged (identity branch; this is an exact-by-construction property, so a tight
    // tolerance is used rather than bit equality of two expressions).
    for (float x = -1.0f; x <= 1.0f; x += 0.01f)
        GF_EXPECT_NEAR (dsp::soloLimit (x), x, 1.0e-7f);

    float previous = 0.0f;

    for (float x = 1.0f; x < 1000.0f; x *= 1.07f)
    {
        const auto y = dsp::soloLimit (x);
        GF_EXPECT (y >= previous - 1.0e-6f);                    // monotonic
        GF_EXPECT (y >= 1.0f - 1.0e-6f && y <= 2.0f);           // never above +6 dBFS
        GF_EXPECT_NEAR (dsp::soloLimit (-x), -y, 1.0e-6f);      // odd symmetry
        previous = y;
    }

    // Smooth at the knee: no step.
    GF_EXPECT_NEAR (dsp::soloLimit (1.0001f), 1.0001f, 1.0e-4f);
    GF_EXPECT (dsp::soloLimit (6.7f) < 2.0f);
    GF_EXPECT (std::isnan (dsp::soloLimit (std::numeric_limits<float>::quiet_NaN())));
}

GF_TEST (SoloLimit, band_solo_stays_below_6dbfs_on_hostile_input_while_the_crossover_moves)
{
    constexpr double sr = 22050.0;

    for (const int solo : { 1, 2, 3 })
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "bandSolo", (float) solo }, { "dry", 100.0f } });

        juce::AudioBuffer<float> in (2, 22050);

        for (int i = 0; i < in.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                in.setSample (ch, i, (i & 1) != 0 ? 1.0f : -1.0f);       // full-scale Nyquist square

        RenderOptions o;
        o.onBlock = [&] (int block, int)
        {
            setParams (proc, { { "xoverLM", block % 2 == 0 ? 70.0f : 950.0f }, { "xoverMH", block % 3 == 0 ? 950.0f : 11000.0f } });
        };

        const auto r = renderOffline (proc, in, 16, sr, o);
        float peak = 0.0f;

        for (int ch = 0; ch < r.out.getNumChannels(); ++ch)
            peak = juce::jmax (peak, r.out.getMagnitude (ch, 0, r.out.getNumSamples()));

        GF_EXPECT_MSG (std::isfinite (peak) && peak <= 2.0f, "solo " + juce::String (solo) + " peak " + juce::String (peak));
    }
}
