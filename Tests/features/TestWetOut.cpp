// F-M17 Wet-only output mode (docs/dev/12 F-M17).

#include "../support/LiveRig.h"

#include "features/wetout/WetOut.h"

#include <cmath>
#include <cstring>

using namespace gapfill;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;

    LiveRender render (std::initializer_list<std::pair<const char*, float>> params, int block = 512, int bars = 3,
                       const std::function<void (int, int, juce::MidiBuffer&)>& onBlock = {},
                       GapFillAudioProcessor* external = nullptr)
    {
        GapFillAudioProcessor local;
        auto& proc = external != nullptr ? *external : local;
        setParams (proc, params);
        TestPlayHead ph;
        ph.bpm = 150.0;
        return renderLive (proc, drumLoop (kSr, 150.0, bars), block, kSr, ph, onBlock);
    }

    bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
            return false;

        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * (size_t) a.getNumSamples()) != 0)
                return false;

        return true;
    }

    const std::initializer_list<std::pair<const char*, float>> kBusy { { "fill", 100.0f }, { "density", 100.0f } };
}

GF_TEST (WetOut, targets_are_neutral_when_off_and_fixed_when_on)
{
    const auto off = wetout::mixTargets (false, 0.37f, 0.81f);
    GF_EXPECT (off.dry == 0.37f && off.wet == 0.81f);

    const auto on = wetout::mixTargets (true, 0.37f, 0.81f);
    GF_EXPECT (on.dry == 0.0f && on.wet == 1.0f);
}

GF_TEST (WetOut, wetsolo_is_a_routing_parameter_off_by_default_and_never_randomised)
{
    GapFillAudioProcessor proc;
    auto* p = proc.getValueTreeState().getParameter ("wetSolo");
    GF_EXPECT (p != nullptr && p->getDefaultValue() < 0.5f);
    GF_EXPECT (performanceParameterIDs().contains ("wetSolo"));
    GF_EXPECT (! randomisableParameterIDs().contains ("wetSolo"));
    GF_EXPECT (allParameterIDs().indexOf ("wetSolo") > allParameterIDs().indexOf ("capture"));       // appended
}

GF_TEST (WetOut, off_identity)
{
    // wetSolo explicitly off renders exactly what the untouched default renders.
    const auto a = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "dry", 70.0f }, { "wet", 55.0f } });
    const auto b = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "dry", 70.0f }, { "wet", 55.0f }, { "wetSolo", 0.0f } });
    GF_EXPECT (identical (a.out, b.out));
    GF_EXPECT (a.fills > 0);
}

GF_TEST (WetOut, solo_equals_dry0_wet100_out0dB_render_bit_exact)
{
    // The knobs are set to arbitrary values: solo must ignore them (Output is the one trim that still applies).
    const auto solo = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "dry", 100.0f }, { "wet", 10.0f },
                                { "output", 0.0f }, { "wetSolo", 1.0f } });
    const auto ref  = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "dry", 0.0f }, { "wet", 100.0f },
                                { "output", 0.0f }, { "wetSolo", 0.0f } });
    GF_EXPECT (identical (solo.out, ref.out));
    GF_EXPECT (solo.fills > 0);

    float peak = 0.0f;
    for (int i = 0; i < solo.out.getNumSamples(); ++i)
        peak = std::max (peak, std::abs (solo.out.getSample (0, i)));

    GF_EXPECT (peak > 0.01f);                         // the wet signal is really there
}

GF_TEST (WetOut, solo_holds_for_every_block_size)
{
    for (int block : { 32, 128, 512, 4096 })
    {
        const auto solo = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "dry", 100.0f }, { "wet", 0.0f }, { "wetSolo", 1.0f } }, block);
        const auto ref  = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "dry", 0.0f }, { "wet", 100.0f } }, block);
        GF_EXPECT_MSG (identical (solo.out, ref.out), "block " + juce::String (block));
    }
}

GF_TEST (WetOut, dry_is_absent_when_no_fill_plays)
{
    // Fill = 0: the engine makes no wet material, so a wet-only output is exactly silent.
    const auto r = render ({ { "fill", 0.0f }, { "wetSolo", 1.0f } });

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < r.out.getNumSamples(); ++i)
            GF_EXPECT (r.out.getSample (ch, i) == 0.0f);
}

GF_TEST (WetOut, output_trim_still_applies)
{
    const auto solo  = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "wetSolo", 1.0f }, { "output", -6.0f } });
    const auto unity = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "wetSolo", 1.0f }, { "output", 0.0f } });
    const auto ratio = std::pow (10.0, -6.0 / 20.0);

    for (int i = 0; i < solo.out.getNumSamples(); i += 7)
        GF_EXPECT_NEAR (solo.out.getSample (0, i), unity.out.getSample (0, i) * ratio, 2.0e-5);
}

GF_TEST (WetOut, switch_is_click_free)
{
    // A steady tone with no fills: the dry signal fades to silence over the 20 ms smoother instead of cutting.
    const int total = (int) (kSr * 1.0);
    juce::AudioBuffer<float> tone (2, total);

    for (int i = 0; i < total; ++i)
    {
        const auto v = 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * (float) i / (float) kSr);
        tone.setSample (0, i, v);
        tone.setSample (1, i, v);
    }

    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 0.0f } });
    TestPlayHead ph;
    const int block = 256;
    const auto r = renderLive (proc, tone, block, kSr, ph, [&] (int blockIndex, int, juce::MidiBuffer&)
    {
        if (blockIndex == 80)
            setParams (proc, { { "wetSolo", 1.0f } });
    });

    float steadyDelta = 0.0f, worstDelta = 0.0f;
    const int latency = proc.getActiveLatencySamples();

    for (int i = 4000 + latency; i < 80 * block; ++i)
        steadyDelta = std::max (steadyDelta, std::abs (r.out.getSample (0, i) - r.out.getSample (0, i - 1)));

    for (int i = 80 * block; i < total; ++i)
        worstDelta = std::max (worstDelta, std::abs (r.out.getSample (0, i) - r.out.getSample (0, i - 1)));

    GF_EXPECT (steadyDelta > 0.005f);
    GF_EXPECT_MSG (worstDelta <= steadyDelta * 1.5f, "worst " + juce::String (worstDelta) + " steady " + juce::String (steadyDelta));
    GF_EXPECT (std::abs (r.out.getSample (0, total - 1)) == 0.0f);        // the dry is gone at the end
}

GF_TEST (WetOut, bypass_wins_and_is_transparent)
{
    const auto bypassed = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "bypass", 1.0f }, { "wetSolo", 1.0f } });
    const auto reference = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "bypass", 1.0f }, { "wetSolo", 0.0f } });
    GF_EXPECT (identical (bypassed.out, reference.out));

    // and the bypassed output really is the (delayed) input
    float peak = 0.0f;
    for (int i = 0; i < bypassed.out.getNumSamples(); ++i)
        peak = std::max (peak, std::abs (bypassed.out.getSample (0, i)));

    GF_EXPECT (peak > 0.1f);
}

GF_TEST (WetOut, host_bypass_path_wins_too)
{
    auto run = [] (bool solo)
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "wetSolo", solo ? 1.0f : 0.0f } });
        prepareProcessor (proc, kSr, 512, false);
        const auto in = drumLoop (kSr, 150.0, 2);
        juce::AudioBuffer<float> out (2, in.getNumSamples());
        juce::AudioBuffer<float> work (2, 512);
        juce::MidiBuffer midi;

        for (int pos = 0; pos + 512 <= in.getNumSamples(); pos += 512)
        {
            for (int ch = 0; ch < 2; ++ch)
                work.copyFrom (ch, 0, in, ch, pos, 512);

            proc.processBlockBypassed (work, midi);

            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, work, ch, 0, 512);
        }

        return out;
    };

    GF_EXPECT (identical (run (true), run (false)));
}

GF_TEST (WetOut, panic_silences_the_wet_only_output)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "wetSolo", 1.0f } });
    TestPlayHead ph;
    ph.bpm = 150.0;
    const auto in = drumLoop (kSr, 150.0, 3);
    const int block = 512;
    const auto r = renderLive (proc, in, block, kSr, ph, [&] (int blockIndex, int, juce::MidiBuffer&)
    {
        if (blockIndex == 100)
        {
            Command c;
            c.type = CommandType::Panic;
            proc.postCommand (c);
        }
    });

    float peakBefore = 0.0f, peakAfter = 0.0f;

    for (int i = 0; i < 100 * block; ++i)
        peakBefore = std::max (peakBefore, std::abs (r.out.getSample (0, i)));

    for (int i = 102 * block; i < r.out.getNumSamples(); ++i)
        peakAfter = std::max (peakAfter, std::abs (r.out.getSample (0, i)));

    GF_EXPECT (peakBefore > 0.01f);
    GF_EXPECT_MSG (peakAfter == 0.0f, "after panic " + juce::String (peakAfter));
}

GF_TEST (WetOut, nan_input_never_reaches_the_output)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 100.0f }, { "wetSolo", 1.0f } });
    auto in = drumLoop (kSr, 150.0, 1);
    in.setSample (0, 2000, std::numeric_limits<float>::quiet_NaN());
    in.setSample (1, 2001, std::numeric_limits<float>::infinity());
    TestPlayHead ph;
    const auto r = renderLive (proc, in, 512, kSr, ph, {});

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < r.out.getNumSamples(); ++i)
            GF_EXPECT (std::isfinite (r.out.getSample (ch, i)));
}

GF_TEST (WetOut, latency_is_unchanged_by_the_switch)
{
    GapFillAudioProcessor a, b;
    setParams (b, { { "wetSolo", 1.0f } });
    prepareProcessor (a, kSr, 512, false);
    prepareProcessor (b, kSr, 512, false);
    a.flushLatencyReportForTests();
    b.flushLatencyReportForTests();
    GF_EXPECT (a.getLatencySamples() == b.getLatencySamples());
}

GF_TEST (WetOut, state_roundtrip_and_old_state_loads_off)
{
    GapFillAudioProcessor a;
    setParams (a, { { "wetSolo", 1.0f } });
    juce::MemoryBlock state;
    a.getStateInformation (state);

    GapFillAudioProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    GF_EXPECT (b.getValueTreeState().getRawParameterValue ("wetSolo")->load() > 0.5f);

    // A state saved by a build that did not know the parameter leaves it off.
    GapFillAudioProcessor c;
    auto tree = c.getValueTreeState().copyState();
    auto child = tree.getChildWithProperty ("id", "wetSolo");
    GF_EXPECT (child.isValid());
    tree.removeChild (child, nullptr);

    GapFillAudioProcessor d;
    GF_EXPECT (d.applyParameterState (tree));
    GF_EXPECT (d.getValueTreeState().getRawParameterValue ("wetSolo")->load() < 0.5f);
}
