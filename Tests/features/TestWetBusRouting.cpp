#include "../TestFramework.h"
#include "../support/AllocGuard.h"

#include "features/wetbus/OutputRouting.h"

#include <bit>
#include <limits>

using namespace gapfill;

GF_TEST (WetBusRouting, neutral_and_bus_disabled_preserve_the_existing_targets_bit_for_bit)
{
    for (const bool enabled : { false, true })
        for (const int mode : { 0, 1 })
            for (const bool solo : { false, true })
                for (const float dry : { -0.0f, 0.37f, 1.0f })
                    for (const float wet : { -0.0f, 0.61f, 1.0f })
                    {
                        if (enabled && mode == 1 && ! solo)
                            continue;
                        const auto original = wetout::mixTargets (solo, dry, wet);
                        const auto actual = wetbus::mainMixTargets (enabled, mode, solo, dry, wet);
                        GF_EXPECT (std::bit_cast<std::uint32_t> (original.dry) == std::bit_cast<std::uint32_t> (actual.dry));
                        GF_EXPECT (std::bit_cast<std::uint32_t> (original.wet) == std::bit_cast<std::uint32_t> (actual.wet));
                    }
}

GF_TEST (WetBusRouting, dry_only_requires_an_enabled_bus_and_wetSolo_takes_precedence)
{
    const auto dry = wetbus::mainMixTargets (true, 1, false, 0.4f, 0.7f);
    GF_EXPECT (dry.dry == 0.4f && dry.wet == 0.0f);
    const auto solo = wetbus::mainMixTargets (true, 1, true, 0.4f, 0.7f);
    GF_EXPECT (solo.dry == 0.0f && solo.wet == 1.0f);
    const auto invalidMode = wetbus::mainMixTargets (true, 9, false, 0.4f, 0.7f);
    GF_EXPECT (invalidMode.dry == 0.4f && invalidMode.wet == 0.7f);
}

GF_TEST (WetBusRouting, level_reaches_each_target_in_twenty_ms_without_overshoot)
{
    for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        wetbus::LevelSmoother level;
        level.prepare (rate);
        for (const float db : { 12.0f, -24.0f, 0.0f })
        {
            const auto initial = level.currentGain();
            level.setTargetDecibels (db);
            const auto target = level.targetGain();
            auto previous = initial;
            const int samples = (int) (rate * 0.020);
            for (int i = 0; i < samples; ++i)
            {
                const auto gain = level.nextGain();
                GF_EXPECT (gain >= juce::jmin (initial, target) - 1.0e-5f);
                GF_EXPECT (gain <= juce::jmax (initial, target) + 1.0e-5f);
                GF_EXPECT (target >= initial ? gain >= previous - 1.0e-6f : gain <= previous + 1.0e-6f);
                previous = gain;
                GF_EXPECT (level.isSmoothing() == (i < samples - 1));
            }
            GF_EXPECT (level.currentGain() == target);
        }
    }
}

GF_TEST (WetBusRouting, repeating_a_target_does_not_restart_the_ramp_and_reset_finishes_it)
{
    wetbus::LevelSmoother level;
    level.prepare (48000.0);
    for (int i = 0; i < 960; ++i)
    {
        level.setTargetDecibels (-12.0f);
        level.nextGain();
    }
    GF_EXPECT (! level.isSmoothing());
    GF_EXPECT_NEAR (level.currentGain(), std::pow (10.0f, -12.0f / 20.0f), 1.0e-6);
    level.setTargetDecibels (12.0f);
    const auto first = level.nextGain();
    GF_EXPECT (first > 0.0f && first < level.targetGain());
    level.reset();
    GF_EXPECT (! level.isSmoothing() && level.currentGain() == level.targetGain());
}

GF_TEST (WetBusRouting, unity_is_exact_and_invalid_level_inputs_are_bounded)
{
    wetbus::LevelSmoother level;
    level.prepare (48000.0);
    GF_EXPECT (level.isNeutral());
    for (int i = 0; i < 2000; ++i)
        GF_EXPECT (level.nextGain() == 1.0f);
    level.setTargetDecibels (std::numeric_limits<float>::quiet_NaN());
    GF_EXPECT (level.isNeutral());
    level.setTargetDecibels (1000.0f);
    GF_EXPECT_NEAR (level.targetGain(), std::pow (10.0f, 12.0f / 20.0f), 1.0e-6);
    level.setTargetDecibels (-1000.0f);
    GF_EXPECT_NEAR (level.targetGain(), std::pow (10.0f, -24.0f / 20.0f), 1.0e-6);
}

GF_TEST (WetBusRouting, mono_is_the_half_sum_of_the_stereo_wet_tap)
{
    GF_EXPECT (wetbus::monoSample (0.5f, 0.5f) == 0.5f);
    GF_EXPECT (wetbus::monoSample (0.7f, -0.7f) == 0.0f);
    GF_EXPECT (wetbus::monoSample (0.75f, 0.25f) == 0.5f);
    GF_EXPECT (wetbus::monoSample (-0.75f, -0.25f) == -0.5f);
}

GF_TEST (WetBusRouting, per_sample_routing_and_target_changes_allocate_nothing)
{
    wetbus::LevelSmoother level;
    level.prepare (48000.0);
    float sum = 0.0f;
    gftest::AllocGuard guard;
    for (int i = 0; i < 48000; ++i)
    {
        if (i % 240 == 0)
            level.setTargetDecibels ((float) (i % 37) - 24.0f);
        const auto mix = wetbus::mainMixTargets (true, i % 2, false, 1.0f, 0.5f);
        sum += wetbus::monoSample (0.1f, 0.2f) * level.nextGain() + mix.wet;
    }
    GF_EXPECT (guard.count() == 0);
    GF_EXPECT (std::isfinite (sum) && sum > 0.0f);
}
