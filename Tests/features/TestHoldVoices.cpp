// F-M10 hold voices, parameters and F-S07 Stutter Storm sound (docs/dev/features/F-M10-momentary-effects-and-latch.md).

#include "../support/AllocGuard.h"
#include "../support/LiveRig.h"

#include "features/momentary/HoldController.h"

#include <cmath>
#include <functional>

using namespace gapfill;
using namespace gapfill::momentary;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;

    constexpr std::uint8_t bit (Effect e) { return (std::uint8_t) (1u << (int) e); }

    void press (GapFillAudioProcessor& p, CommandType t, bool down)
    {
        Command c;
        c.type = t;
        c.velocity = down ? 127 : 0;
        p.postCommand (c);
    }

    double rms (const juce::AudioBuffer<float>& b, int from, int to)
    {
        double e = 0.0;

        for (int i = from; i < to; ++i)
            e += (double) b.getSample (0, i) * b.getSample (0, i) + (double) b.getSample (1, i) * b.getSample (1, i);

        return std::sqrt (e / (2.0 * (to - from)));
    }

    juce::AudioBuffer<float> runHold (int block, std::function<void (GapFillAudioProcessor&, int)> command, int bars = 2,
                                      std::initializer_list<std::pair<const char*, float>> params = {})
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 0.0f }, { "dry", 0.0f } });
        setParams (proc, params);
        TestPlayHead ph;
        ph.tsNum = 4;
        ph.tsDen = 4;
        const auto input = drumLoop (kSr, 120.0, bars);
        return renderLive (proc, input, block, kSr, ph, [&] (int b, int, juce::MidiBuffer&) { command (proc, b); }).out;
    }
}

GF_TEST (HoldVoices, parameters_exist_with_the_documented_defaults_and_are_performance_state)
{
    GapFillAudioProcessor proc;
    auto val = [&] (const char* id) { return pv (proc.getValueTreeState().getRawParameterValue (id)); };
    GF_EXPECT (val ("fm10_stutter") < 0.5f && val ("fm10_freeze") < 0.5f && val ("fm10_reverse") < 0.5f && val ("fm10_tape") < 0.5f);
    GF_EXPECT (val ("fm10_mode") < 0.5f);
    GF_EXPECT_NEAR (val ("fm10_level"), 100.0, 1e-4);
    GF_EXPECT_NEAR (val ("fm10_dryDuck"), 0.0, 1e-4);
    GF_EXPECT (val ("fm10_protect") > 0.5f);                       // DECISIONS.md 7: Respect protection is ON by default
    GF_EXPECT_NEAR (val ("fm10_timeout"), 15.0, 1e-4);

    const auto perf = performanceParameterIDs();
    const auto random = randomisableParameterIDs();

    for (const char* id : { "fm10_stutter", "fm10_freeze", "fm10_reverse", "fm10_tape", "fm10_mode", "fm10_level", "fm10_dryDuck",
                            "fm10_protect", "fm10_timeout" })
    {
        GF_EXPECT_MSG (perf.contains (id), id);
        GF_EXPECT_MSG (! random.contains (id), id);
    }
}

GF_TEST (HoldVoices, automation_parameter_engages_a_hold_and_latch_mode_toggles_on_the_rising_edge)
{
    GapFillAudioProcessor proc;
    prepareProcessor (proc, 48000.0, 256, false);
    auto& live = proc.getLiveControl();
    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    work.clear();

    setParams (proc, { { "fm10_freeze", 1.0f } });
    proc.processBlock (work, midi);
    GF_EXPECT (live.getHoldEngagedMask() == bit (Effect::Freeze));
    setParams (proc, { { "fm10_freeze", 0.0f } });
    proc.processBlock (work, midi);
    GF_EXPECT (live.getHoldEngagedMask() == 0);

    setParams (proc, { { "fm10_mode", 1.0f } });                          // Latch
    setParams (proc, { { "fm10_reverse", 1.0f } });
    proc.processBlock (work, midi);
    setParams (proc, { { "fm10_reverse", 0.0f } });
    proc.processBlock (work, midi);
    GF_EXPECT (live.getHoldEngagedMask() == bit (Effect::Reverse));        // latched on after the release
    setParams (proc, { { "fm10_reverse", 1.0f } });
    proc.processBlock (work, midi);
    GF_EXPECT (live.getHoldEngagedMask() == 0);                            // second rising edge toggles off
}

GF_TEST (HoldVoices, state_restore_keeps_hold_values_but_no_hold_sounds_until_re_armed)
{
    GapFillAudioProcessor a;
    setParams (a, { { "fm10_stutter", 1.0f }, { "fm10_freeze", 1.0f }, { "fm10_reverse", 1.0f }, { "fm10_tape", 1.0f }, { "fm10_dryDuck", 40.0f } });
    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    GapFillAudioProcessor b;
    prepareProcessor (b, 48000.0, 256, false);
    b.setStateInformation (blob.getData(), (int) blob.getSize());

    // Hosts (and pluginval) require restored values to equal the saved ones.
    for (const char* id : { "fm10_stutter", "fm10_freeze", "fm10_reverse", "fm10_tape" })
        GF_EXPECT_MSG (pv (b.getValueTreeState().getRawParameterValue (id)) > 0.5f, id);
    GF_EXPECT_NEAR (pv (b.getValueTreeState().getRawParameterValue ("fm10_dryDuck")), 40.0, 0.01);

    // ...but the engine does not engage a restored ON hold.
    auto& live = b.getLiveControl();
    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    work.clear();

    for (int i = 0; i < 8; ++i)
        b.processBlock (work, midi);

    GF_EXPECT (live.getHoldEngagedMask() == 0);

    // The next off -> on transition re-arms the hold.
    setParams (b, { { "fm10_freeze", 0.0f } });
    b.processBlock (work, midi);
    setParams (b, { { "fm10_freeze", 1.0f } });
    b.processBlock (work, midi);
    GF_EXPECT (live.getHoldEngagedMask() == bit (Effect::Freeze));
}

GF_TEST (HoldVoices, each_hold_effect_makes_finite_wet_sound_while_held_and_stops_after_release)
{
    for (const auto type : { CommandType::MomentaryStutter, CommandType::MomentaryFreeze, CommandType::MomentaryReverse,
                             CommandType::MomentaryTape, CommandType::StutterStorm })
    {
        const auto out = runHold (512, [&] (GapFillAudioProcessor& p, int b)
        {
            if (b == 150) press (p, type, true);
            if (b == 250) press (p, type, false);
        }, 4);

        const auto held = rms (out, 160 * 512, 240 * 512);
        const auto before = rms (out, 20 * 512, 140 * 512);
        const auto after = rms (out, 262 * 512, 280 * 512);

        GF_EXPECT_MSG (before < 1.0e-6, "wet is silent before the press, effect " + juce::String ((int) type));
        GF_EXPECT_MSG (held > 1.0e-3, "wet while held, effect " + juce::String ((int) type) + " rms " + juce::String (held));
        GF_EXPECT_MSG (after < 1.0e-4, "wet stops after release, effect " + juce::String ((int) type) + " rms " + juce::String (after));

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < out.getNumSamples(); ++i)
            {
                const auto v = out.getSample (ch, i);
                GF_EXPECT (std::isfinite (v) && std::abs (v) < 1.5f);
            }
    }
}

GF_TEST (HoldVoices, holds_are_not_natural_fills_not_in_history_and_leave_the_scheduler_alone)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 0.0f }, { "dry", 0.0f } });
    TestPlayHead ph;
    const auto input = drumLoop (kSr, 120.0, 3);
    renderLive (proc, input, 512, kSr, ph, [&] (int b, int, juce::MidiBuffer&)
    {
        if (b == 100) press (proc, CommandType::MomentaryStutter, true);
        if (b == 200) press (proc, CommandType::MomentaryStutter, false);
        proc.drainFillEvents();
    });

    GF_EXPECT (proc.getFillEngine().getFillsStarted() == 0);
    GF_EXPECT (proc.getRecipeHistory().size() == 0);
    GF_EXPECT (proc.getFillEngine().getHoldVoices().getSegmentsStarted() > 0);
}

GF_TEST (HoldVoices, tape_stop_is_one_shot_and_a_long_stutter_chains_seamless_segments)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 0.0f }, { "dry", 0.0f } });
    TestPlayHead ph;
    const auto input = drumLoop (kSr, 120.0, 4);
    int tapeSegments = 0, stutterSegments = 0;

    renderLive (proc, input, 512, kSr, ph, [&] (int b, int, juce::MidiBuffer&)
    {
        if (b == 100) press (proc, CommandType::MomentaryTape, true);
        if (b == 160) tapeSegments = proc.getFillEngine().getHoldVoices().getSegmentsStarted();
        if (b == 200) press (proc, CommandType::MomentaryTape, false);
        if (b == 210) press (proc, CommandType::MomentaryStutter, true);
        if (b == 330) stutterSegments = proc.getFillEngine().getHoldVoices().getSegmentsStarted() - tapeSegments;
    });

    GF_EXPECT (tapeSegments == 1);                                         // decelerates once, then silent while held
    GF_EXPECT (stutterSegments >= 4);                                      // 1.3 s of stutter = chained 250 ms segments
}

GF_TEST (HoldVoices, dry_duck_lowers_the_dry_only_while_held_and_zero_is_neutral)
{
    auto render = [] (float duck, bool hold)
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 0.0f }, { "fm10_dryDuck", duck }, { "fm10_level", 0.0f } });   // level 0: the dry is all we measure
        TestPlayHead ph;
        const auto input = drumLoop (kSr, 120.0, 3);
        return renderLive (proc, input, 512, kSr, ph, [&] (int b, int, juce::MidiBuffer&)
        {
            if (hold && b == 100) press (proc, CommandType::MomentaryFreeze, true);
            if (hold && b == 200) press (proc, CommandType::MomentaryFreeze, false);
        }).out;
    };

    const auto base = render (0.0f, false);
    const auto zero = render (0.0f, true);
    const auto ducked = render (100.0f, true);

    GF_EXPECT (compareBuffers (base, ducked, 0.0).firstBad < 0 || compareBuffers (base, ducked, 0.0).firstBad >= 100 * 512 - 1);

    const auto dryHold = rms (zero, 140 * 512, 190 * 512);
    const auto duckHold = rms (ducked, 140 * 512, 190 * 512);
    GF_EXPECT_MSG (duckHold < dryHold * 0.7, "ducked " + juce::String (duckHold) + " vs " + juce::String (dryHold));
    GF_EXPECT_NEAR (rms (ducked, 280 * 512, 300 * 512), rms (base, 280 * 512, 300 * 512), 1.0e-3);   // restored after the release
}

GF_TEST (HoldVoices, respect_protection_option_ducks_the_hold_like_a_fill)
{
    auto level = [] (float protect)
    {
        const auto out = runHold (512, [&] (GapFillAudioProcessor& p, int b)
        {
            if (b == 100) press (p, CommandType::MomentaryStutter, true);
        }, 3, { { "fm10_protect", protect }, { "protTrans", 100.0f }, { "protNext", 100.0f } });
        return rms (out, 120 * 512, 250 * 512);
    };

    GF_EXPECT (level (1.0f) < level (0.0f));
}

GF_TEST (HoldVoices, panic_silences_holds_within_50_ms)
{
    const auto out = runHold (256, [&] (GapFillAudioProcessor& p, int b)
    {
        if (b == 300) press (p, CommandType::MomentaryStutter, true);       // ~1.6 s in, like the other hold tests
        if (b == 500) p.getLiveControl().panic().requestPanic();
    }, 4);

    const int panicSample = 500 * 256;
    GF_EXPECT_MSG (rms (out, panicSample - 24000, panicSample) > 1.0e-4,
                   "rms before " + juce::String (rms (out, panicSample - 24000, panicSample)) + " at hold start " + juce::String (rms (out, 120 * 256, 160 * 256)));
    GF_EXPECT (rms (out, panicSample + 2400 + 256, panicSample + 2400 + 2000) < 3.0e-5);      // below -90 dBFS 50 ms after the request
}

GF_TEST (HoldVoices, seeded_fuzz_through_the_processor_leaves_nothing_stuck_after_panic)
{
    juce::Random rnd (2026);
    const CommandType types[] = { CommandType::MomentaryStutter, CommandType::MomentaryFreeze, CommandType::MomentaryReverse,
                                  CommandType::MomentaryTape, CommandType::StutterStorm };

    for (int seq = 0; seq < 30; ++seq)
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 0.0f }, { "dry", 0.0f } });
        TestPlayHead ph;
        const auto input = drumLoop (kSr, 120.0, 3);
        const int panicBlock = 150;

        const auto r = renderLive (proc, input, 256, kSr, ph, [&] (int b, int, juce::MidiBuffer&)
        {
            if (b < panicBlock && rnd.nextInt (6) == 0)
                press (proc, types[rnd.nextInt (5)], rnd.nextBool());

            if (b == 40 && rnd.nextBool())
                proc.getLiveControl().panic().requestPanic();

            if (b == 41)
                proc.getLiveControl().panic().requestRecover();

            if (b == 80 && rnd.nextBool())
                setParams (proc, { { "bypass", 1.0f } });

            if (b == 90)
                setParams (proc, { { "bypass", 0.0f } });

            if (b == panicBlock)
                proc.getLiveControl().panic().requestPanic();
        });

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < r.out.getNumSamples(); ++i)
            {
                const auto v = r.out.getSample (ch, i);
                GF_EXPECT (std::isfinite (v) && std::abs (v) < 1.5f);
            }

        const int from = panicBlock * 256 + 2400 + 256;
        GF_EXPECT_MSG (rms (r.out, from, from + 4000) < 3.0e-5, "sequence " + juce::String (seq));
        GF_EXPECT (proc.getLiveControl().getHoldEngagedMask() == 0);
    }
}

GF_TEST (HoldVoices, hold_path_does_not_allocate)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 40.0f }, { "fm10_dryDuck", 50.0f } });
    prepareProcessor (proc, kSr, 256, false);
    TestPlayHead ph;
    proc.setPlayHead (&ph);
    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    const auto input = drumLoop (kSr, 120.0, 6);
    long allocations = 0;
    const CommandType types[] = { CommandType::MomentaryStutter, CommandType::MomentaryFreeze, CommandType::MomentaryReverse,
                                  CommandType::MomentaryTape, CommandType::StutterStorm };

    for (int block = 0; block < 600; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, input, ch, (block * 256) % (input.getNumSamples() - 256), 256);

        if (block % 40 == 0)
            press (proc, types[(block / 40) % 5], true);

        if (block % 40 == 30)
            press (proc, types[(block / 40) % 5], false);

        AllocGuard guard;
        proc.processBlock (work, midi);

        if (block > 100)
            allocations += guard.count();

        ph.advance (256, kSr);
    }

    proc.setPlayHead (nullptr);
    GF_EXPECT (allocations == 0);
}
