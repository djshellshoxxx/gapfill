#include "../TestFramework.h"
#include "../support/AllocGuard.h"
#include "features/modulation/Sources.h"

#include <cmath>
#include <limits>

using namespace gapfill::modulation;

GF_TEST (Modulation, source_depth_is_additive_bounded_and_knob_ignores_depth)
{
    GF_EXPECT_NEAR (macroValue (MacroSource::Knob, .2f, 1, 1, 1, 1), .2f, 1e-7f);
    GF_EXPECT_NEAR (macroValue (MacroSource::LfoA, .5f, -.25f, 1, 0, 0), .25f, 1e-7f);
    GF_EXPECT_NEAR (macroValue (MacroSource::LfoB, .9f, .5f, 0, 1, 0), 1, 1e-7f);
    GF_EXPECT_NEAR (macroValue (MacroSource::Energy, .5f, .5f, 0, 0, 1), 1, 1e-7f);
    GF_EXPECT_NEAR (macroValue (MacroSource::Energy, .5f, .5f, 0, 0, 0), 0, 1e-7f);
}

GF_TEST (Modulation, permanent_divisions_observe_host_meter_and_free_clock_uses_output_sample)
{
    GF_EXPECT_NEAR (divisionBeats (1, 4, 4), 1.0 / 3, 1e-12);
    GF_EXPECT_NEAR (divisionBeats (8, 7, 8), 3.5, 1e-12);
    GF_EXPECT_NEAR (divisionBeats (11, 7, 8), 28, 1e-12);
    LfoSettings lfo;
    DecisionInputs at;
    at.transportValid = true;
    at.ppq = 1;
    GF_EXPECT_NEAR (lfoValue (lfo, at, 0), 1, 1e-6f);
    at.transportValid = false;
    at.outputPos = 24000;
    GF_EXPECT_NEAR (lfoValue (lfo, at, 0), 1, 1e-6f);
    at.outputPos = 0;
    GF_EXPECT_NEAR (lfoValue (lfo, at, 0), 0, 1e-6f);
}

GF_TEST (Modulation, random_access_lfos_are_seek_safe_independent_and_glide_is_continuous)
{
    LfoSettings lfo;
    DecisionInputs at;
    at.transportValid = true;
    at.ppq = 2;
    lfo.shape = LfoShape::RandomStep;
    const auto original = lfoValue (lfo, at, 0);
    GF_EXPECT (original != lfoValue (lfo, at, 1));
    at.ppq = -1000;
    (void) lfoValue (lfo, at, 0);
    at.ppq = 2;
    GF_EXPECT_NEAR (lfoValue (lfo, at, 0), original, 0);
    lfo.shape = LfoShape::RandomGlide;
    at.ppq = 4 - 1e-8;
    const auto before = lfoValue (lfo, at, 0);
    at.ppq = 4 + 1e-8;
    GF_EXPECT_NEAR (lfoValue (lfo, at, 0), before, 1e-6f);
}

GF_TEST (Modulation, every_shape_stays_finite_and_bounded_at_hostile_positions)
{
    LfoSettings lfo;
    DecisionInputs at;
    at.transportValid = true;
    for (int shape = 0; shape < 6; ++shape)
    {
        lfo.shape = (LfoShape) shape;
        for (int sample = -10000; sample < 10000; ++sample)
        {
            at.ppq = sample * .017;
            const auto value = lfoValue (lfo, at, 0);
            GF_EXPECT (std::isfinite (value) && value >= -1 && value <= 1);
        }
    }
    at.ppq = 1e300;
    GF_EXPECT (std::isfinite (lfoValue (lfo, at, 0)));
    at.ppq = std::numeric_limits<double>::quiet_NaN();
    at.sampleRate = std::numeric_limits<double>::infinity();
    lfo.freeHz = std::numeric_limits<float>::quiet_NaN();
    GF_EXPECT (std::isfinite (lfoValue (lfo, at, 0)));
}

GF_TEST (Modulation, source_evaluation_performs_no_audio_thread_allocation)
{
    DecisionInputs at;
    at.transportValid = true;
    LfoSettings lfo;
    lfo.shape = LfoShape::RandomGlide;
    float sum = 0;
    gftest::AllocGuard guard;
    for (int sample = 0; sample < 4096; ++sample)
    {
        at.ppq = sample / 24000.0;
        sum += macroValue (MacroSource::LfoA, .5f, .5f, lfoValue (lfo, at, 0), 0, .4f);
    }
    GF_EXPECT (std::isfinite (sum) && guard.count() == 0);
}
