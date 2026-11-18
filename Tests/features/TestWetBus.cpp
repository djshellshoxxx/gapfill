// F-L12 Optional "Wet" output bus (docs/dev/12 F-L12).

#include "../support/TestRig.h"
#include "../support/AllocGuard.h"

#include "features/wetbus/WetBus.h"

#include <cmath>
#include <cstring>

using namespace gapfill;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;

    struct BusRender
    {
        juce::AudioBuffer<float> main;      // 2 ch
        juce::AudioBuffer<float> wet;       // mono/stereo (empty when the bus is disabled)
        int latency = 0;
    };

    /** Renders `in` through `proc` with the host-style in-place buffer: main in 0-1, sidechain in 2-3,
        wet bus out 2-3 (aliasing the sidechain, exactly as JUCE hands it to a plug-in). */
    BusRender renderBus (GapFillAudioProcessor& proc, const juce::AudioBuffer<float>& in, int block,
                         bool wetBus, const juce::AudioBuffer<float>* sidechain, TestPlayHead* ph = nullptr,
                         const std::function<void (int)>& onBlock = {}, int wetChannels = 2)
    {
        prepareProcessor (proc, kSr, block, sidechain != nullptr, false, wetBus);
        if (wetBus && wetChannels == 1)
        {
            proc.releaseResources();
            auto layout = proc.getBusesLayout();
            layout.outputBuses.set (wetbus::kWetBusIndex, juce::AudioChannelSet::mono());
            GF_EXPECT (proc.setBusesLayout (layout));
            proc.prepareToPlay (kSr, block);
        }
        TestPlayHead local;
        local.bpm = 150.0;
        proc.setPlayHead (ph != nullptr ? ph : &local);

        const auto total = juce::jmax (proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels());
        BusRender r;
        r.main.setSize (2, in.getNumSamples());
        r.wet.setSize (wetBus ? wetChannels : 0, in.getNumSamples());
        r.main.clear();
        r.wet.clear();

        juce::AudioBuffer<float> work (total, block);
        juce::MidiBuffer midi;
        int idx = 0;

        for (int pos = 0; pos < in.getNumSamples(); pos += block, ++idx)
        {
            const auto n = juce::jmin (block, in.getNumSamples() - pos);

            if (onBlock)
                onBlock (idx);

            work.clear();

            for (int ch = 0; ch < 2; ++ch)
                work.copyFrom (ch, 0, in, ch, pos, n);

            if (sidechain != nullptr)
                for (int ch = 0; ch < 2; ++ch)
                    work.copyFrom (2 + ch, 0, *sidechain, ch, pos, n);

            juce::AudioBuffer<float> slice (work.getArrayOfWritePointers(), total, n);
            proc.processBlock (slice, midi);

            for (int ch = 0; ch < 2; ++ch)
            {
                r.main.copyFrom (ch, pos, work, ch, 0, n);

                if (wetBus && ch < wetChannels)
                    r.wet.copyFrom (ch, pos, work, 2 + ch, 0, n);
            }

            if (ph == nullptr)
                local.advance (n, kSr);
            else
                ph->advance (n, kSr);
        }

        proc.flushLatencyReportForTests();
        r.latency = proc.getLatencySamples();
        proc.setPlayHead (nullptr);
        return r;
    }

    bool sameValues (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
            return false;

        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                if (a.getSample (ch, i) != b.getSample (ch, i))    // value equality: +0 == -0
                    return false;

        return true;
    }

    float peak (const juce::AudioBuffer<float>& b)
    {
        float p = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            p = juce::jmax (p, b.getMagnitude (ch, 0, b.getNumSamples()));
        return p;
    }

    const std::initializer_list<std::pair<const char*, float>> kBusy { { "fill", 100.0f }, { "density", 100.0f } };

    juce::AudioProcessor::BusesLayout layoutOf (juce::AudioChannelSet in, juce::AudioChannelSet out,
                                                juce::AudioChannelSet side, juce::AudioChannelSet wet)
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (in);
        l.inputBuses.add (side);
        l.outputBuses.add (out);
        l.outputBuses.add (wet);
        return l;
    }
}

GF_TEST (WetBus, the_wet_bus_is_declared_after_the_main_output_and_disabled_by_default)
{
    GapFillAudioProcessor proc;

   #if GAPFILL_WET_BUS
    GF_EXPECT (proc.getBusCount (false) == 2);
    GF_EXPECT (proc.getBus (false, 0)->getName() == "Output");
    GF_EXPECT (proc.getBus (false, 1)->getName() == "Wet");
    GF_EXPECT (! proc.getBus (false, 1)->isEnabledByDefault());
    GF_EXPECT (! proc.getBus (false, 1)->isEnabled());
   #else
    GF_EXPECT (proc.getBusCount (false) == 1);
   #endif

    // The inputs and their order are exactly what they were: Input, Sidechain (off by default).
    GF_EXPECT (proc.getBusCount (true) == 2);
    GF_EXPECT (proc.getBus (true, 0)->getName() == "Input");
    GF_EXPECT (proc.getBus (true, 1)->getName() == "Sidechain");
    GF_EXPECT (! proc.getBus (true, 1)->isEnabled());
    GF_EXPECT (proc.getTotalNumInputChannels() == 2 && proc.getTotalNumOutputChannels() == 2);
}

GF_TEST (WetBus, layout_matrix)
{
    GapFillAudioProcessor proc;
    const auto st = juce::AudioChannelSet::stereo();
    const auto mo = juce::AudioChannelSet::mono();
    const auto off = juce::AudioChannelSet::disabled();

    // accepted: main mono / stereo, sidechain off / mono / stereo, wet off / mono / stereo
    for (auto main : { mo, st })
        for (auto side : { off, mo, st })
            for (auto wet : { off, mo, st })
                GF_EXPECT (proc.checkBusesLayoutSupported (layoutOf (main, main, side, wet)));

    // refused: surround wet bus, main in != main out, surround main, surround sidechain
    GF_EXPECT (! proc.checkBusesLayoutSupported (layoutOf (st, st, off, juce::AudioChannelSet::create5point1())));
    GF_EXPECT (! proc.checkBusesLayoutSupported (layoutOf (st, mo, off, off)));
    GF_EXPECT (! proc.checkBusesLayoutSupported (layoutOf (juce::AudioChannelSet::create5point1(), juce::AudioChannelSet::create5point1(), off, off)));
    GF_EXPECT (! proc.checkBusesLayoutSupported (layoutOf (st, st, juce::AudioChannelSet::create5point1(), off)));

    // a third output bus is never accepted
    auto three = layoutOf (st, st, off, st);
    three.outputBuses.add (st);
    GF_EXPECT (! wetbus::isLayoutSupported (three));
}

GF_TEST (WetBus, bus_disabled_main_output_is_identical_to_enabled_main_output)
{
    // The Wet bus never changes what the main bus carries.
    const auto in = drumLoop (kSr, 150.0, 3);
    GapFillAudioProcessor a, b;
    setParams (a, kBusy);
    setParams (b, kBusy);
    const auto off = renderBus (a, in, 512, false, nullptr);
    const auto on  = renderBus (b, in, 512, true, nullptr);
    GF_EXPECT (sameValues (off.main, on.main));
    GF_EXPECT (a.getFillEngine().getFillsStarted() > 0);
}

GF_TEST (WetBus, wet_bus_equals_the_wetSolo_signal)
{
    const auto in = drumLoop (kSr, 150.0, 3);

    GapFillAudioProcessor solo;
    setParams (solo, { { "fill", 100.0f }, { "density", 100.0f }, { "wetSolo", 1.0f } });
    const auto reference = renderBus (solo, in, 512, false, nullptr);

    GapFillAudioProcessor bus;
    setParams (bus, { { "fill", 100.0f }, { "density", 100.0f }, { "dry", 100.0f }, { "wet", 30.0f }, { "output", -9.0f } });
    const auto r = renderBus (bus, in, 512, true, nullptr);

    GF_EXPECT (peak (reference.main) > 0.01f);

    // Unity, untouched by Dry / Wet / Output. The two paths are different expressions of the same product
    // (main: (dry*d + wet*w)*o; bus: wet*g), so the compiler may contract them differently on another CPU / toolchain:
    // equal to within 1e-6 (-120 dBFS) rather than to the last bit (the F-M17 test holds the bit-exact claim).
    const auto diff = compareBuffers (reference.main, r.wet, 1.0e-6);
    GF_EXPECT_MSG (! diff.shapeMismatch && diff.firstBad < 0, "max abs diff " + juce::String (diff.maxAbs, 9)
                                                              + " first bad " + juce::String (diff.firstBad));
}

GF_TEST (WetBus, wet_bus_contains_no_dry)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 0.0f } });
    const auto r = renderBus (proc, drumLoop (kSr, 150.0, 2), 512, true, nullptr);
    GF_EXPECT (peak (r.main) > 0.1f);                        // the main output has the (delayed) dry
    GF_EXPECT (peak (r.wet) == 0.0f);
}

GF_TEST (WetBus, wet_bus_overwrites_the_sidechain_aliased_channels)
{
    // The Wet bus channels share storage with the sidechain input. With fills off the Wet bus must be exactly
    // silent although the sidechain carries a loud signal, and the sidechain must still have been used.
    auto side = makeNoise (kSr, 6.0, 0.8f);
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 0.0f }, { "detSource", 1.0f } });
    const auto r = renderBus (proc, drumLoop (kSr, 150.0, 3), 512, true, &side);
    GF_EXPECT (peak (r.wet) == 0.0f);
    GF_EXPECT (proc.getMeterState().sidechainConnected.load());
}

GF_TEST (WetBus, bypass_silences_the_wet_bus)
{
    for (const int channels : { 1, 2 })
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "bypass", 1.0f },
                          { "wetBusMainMode", 1.0f }, { "wetBusLevel", 12.0f } });
        const auto r = renderBus (proc, drumLoop (kSr, 150.0, 3), 512, true, nullptr, nullptr, {}, channels);
        GF_EXPECT (peak (r.wet) == 0.0f);
        GF_EXPECT (peak (r.main) > 0.1f);                    // the dry still passes
    }
}

GF_TEST (WetBus, panic_silences_the_wet_bus)
{
    for (const int channels : { 1, 2 })
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "wetBusLevel", 12.0f } });
        const int block = 512;
        const auto r = renderBus (proc, drumLoop (kSr, 150.0, 4), block, true, nullptr, nullptr, [&] (int idx)
        {
            if (idx == 100)
            {
                Command c;
                c.type = CommandType::Panic;
                proc.postCommand (c);
            }
        }, channels);

        juce::AudioBuffer<float> before (channels, 100 * block), after (channels, r.wet.getNumSamples() - 102 * block);

        for (int ch = 0; ch < channels; ++ch)
        {
            before.copyFrom (ch, 0, r.wet, ch, 0, before.getNumSamples());
            after.copyFrom (ch, 0, r.wet, ch, 102 * block, after.getNumSamples());
        }

        GF_EXPECT (peak (before) > 0.01f);
        GF_EXPECT (peak (after) == 0.0f);
    }
}

GF_TEST (WetBus, latency_is_aligned_and_unchanged)
{
    GapFillAudioProcessor a, b;
    const auto in = drumLoop (kSr, 150.0, 1);
    const auto off = renderBus (a, in, 512, false, nullptr);
    const auto on  = renderBus (b, in, 512, true, nullptr);
    GF_EXPECT (off.latency == on.latency);
    GF_EXPECT (off.latency > 0);
}

GF_TEST (WetBus, block_size_invariant_with_the_bus)
{
    const auto in = drumLoop (kSr, 150.0, 3);
    GapFillAudioProcessor a, b;
    setParams (a, kBusy);
    setParams (b, kBusy);
    const auto x = renderBus (a, in, 64, true, nullptr);
    const auto y = renderBus (b, in, 480, true, nullptr);
    GF_EXPECT (peak (x.wet) > 0.01f);
    GF_EXPECT (sameValues (x.wet, y.wet));
}

GF_TEST (WetBus, state_is_unaffected_by_enabling_the_bus)
{
    GapFillAudioProcessor a, b;
    setParams (a, { { "fill", 61.0f } });
    setParams (b, { { "fill", 61.0f } });
    prepareProcessor (a, kSr, 512, false, false, false);
    prepareProcessor (b, kSr, 512, false, false, true);
    juce::MemoryBlock sa, sb;
    a.getStateInformation (sa);
    b.getStateInformation (sb);
    GF_EXPECT (sa.getSize() == sb.getSize() && std::memcmp (sa.getData(), sb.getData(), sa.getSize()) == 0);
}

GF_TEST (WetBus, the_audio_path_does_not_allocate_with_the_bus_enabled)
{
    GapFillAudioProcessor proc;
    setParams (proc, kBusy);
    prepareProcessor (proc, kSr, 512, false, false, true);
    TestPlayHead ph;
    proc.setPlayHead (&ph);
    juce::AudioBuffer<float> work (4, 512);
    juce::MidiBuffer midi;
    const auto in = drumLoop (kSr, 150.0, 1);

    for (int i = 0; i < 20; ++i)                                 // warm up JUCE internals
    {
        work.clear();
        work.copyFrom (0, 0, in, 0, i * 512, 512);
        work.copyFrom (1, 0, in, 1, i * 512, 512);
        proc.processBlock (work, midi);
    }

    AllocGuard guard;

    for (int i = 20; i < 60; ++i)
    {
        work.clear();
        work.copyFrom (0, 0, in, 0, i * 512, 512);
        work.copyFrom (1, 0, in, 1, i * 512, 512);
        proc.processBlock (work, midi);
    }

    GF_EXPECT (guard.count() == 0);
    proc.setPlayHead (nullptr);
}

GF_TEST (WetBus, dry_only_matches_the_dry_path_over_sixty_seconds)
{
    auto in = drumLoop (kSr, 150.0, 38);
    in.setSize (2, (int) (60.0 * kSr), true);
    GapFillAudioProcessor dry, routed;
    setParams (dry, { { "fill", 0.0f }, { "dry", 73.0f }, { "output", -3.0f } });
    setParams (routed, { { "fill", 100.0f }, { "density", 100.0f }, { "dry", 73.0f },
                        { "output", -3.0f }, { "wetBusMainMode", 1.0f } });
    const auto reference = renderBus (dry, in, 512, true, nullptr);
    const auto actual = renderBus (routed, in, 512, true, nullptr);
    const auto diff = compareBuffers (reference.main, actual.main, 1.0e-7);
    GF_EXPECT_MSG (! diff.shapeMismatch && diff.firstBad < 0, "Dry-only max error " + juce::String (diff.maxAbs, 9));
    GF_EXPECT (peak (actual.wet) > 0.01f);
    GF_EXPECT (reference.latency == actual.latency);
}

GF_TEST (WetBus, disabled_bus_keeps_fills_in_main_even_with_dry_only_selected)
{
    const auto in = drumLoop (kSr, 150.0, 3);
    GapFillAudioProcessor baseline, routed;
    setParams (baseline, kBusy);
    setParams (routed, { { "fill", 100.0f }, { "density", 100.0f }, { "wetBusMainMode", 1.0f }, { "wetBusLevel", 12.0f } });
    const auto before = renderBus (baseline, in, 512, false, nullptr);
    const auto after = renderBus (routed, in, 512, false, nullptr);
    GF_EXPECT (sameValues (before.main, after.main));
    GF_EXPECT (routed.getFillEngine().getFillsStarted() > 0);
}

GF_TEST (WetBus, level_changes_only_the_wet_bus)
{
    const auto in = drumLoop (kSr, 150.0, 3);
    GapFillAudioProcessor baseline;
    setParams (baseline, kBusy);
    const auto reference = renderBus (baseline, in, 512, true, nullptr);
    GF_EXPECT (peak (reference.wet) > 0.01f);
    for (const float db : { -24.0f, -6.0f, 12.0f })
    {
        GapFillAudioProcessor routed;
        setParams (routed, { { "fill", 100.0f }, { "density", 100.0f }, { "wetBusLevel", db } });
        const auto actual = renderBus (routed, in, 512, true, nullptr);
        GF_EXPECT (sameValues (reference.main, actual.main));
        const auto gain = juce::Decibels::decibelsToGain (db);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < in.getNumSamples(); ++i)
                GF_EXPECT_NEAR (actual.wet.getSample (ch, i), reference.wet.getSample (ch, i) * gain, 1.0e-6);
        GF_EXPECT (reference.latency == actual.latency);
    }
}

GF_TEST (WetBus, mono_is_the_stereo_half_sum_and_overwrites_aliased_sidechain_storage)
{
    const auto in = drumLoop (kSr, 150.0, 3);
    auto sidechain = makeNoise (kSr, 6.0, 0.8f);
    GapFillAudioProcessor stereo, mono, silent;
    setParams (stereo, kBusy);
    setParams (mono, kBusy);
    setParams (silent, { { "fill", 0.0f }, { "detSource", 1.0f } });
    const auto reference = renderBus (stereo, in, 512, true, &sidechain);
    const auto actual = renderBus (mono, in, 512, true, &sidechain, nullptr, {}, 1);
    const auto noFill = renderBus (silent, in, 512, true, &sidechain, nullptr, {}, 1);
    GF_EXPECT (actual.wet.getNumChannels() == 1);
    GF_EXPECT (sameValues (reference.main, actual.main));
    GF_EXPECT (peak (actual.wet) > 0.001f);
    for (int i = 0; i < in.getNumSamples(); ++i)
        GF_EXPECT_NEAR (actual.wet.getSample (0, i), 0.5f * (reference.wet.getSample (0, i) + reference.wet.getSample (1, i)), 1.0e-7);
    GF_EXPECT (peak (noFill.wet) == 0.0f);
    GF_EXPECT (silent.getMeterState().sidechainConnected.load());
    GF_EXPECT (reference.latency == actual.latency);
}

GF_TEST (WetBus, wetSolo_overrides_dry_only_main_mode)
{
    const auto in = drumLoop (kSr, 150.0, 3);
    GapFillAudioProcessor solo, routed;
    setParams (solo, { { "fill", 100.0f }, { "density", 100.0f }, { "wetSolo", 1.0f } });
    setParams (routed, { { "fill", 100.0f }, { "density", 100.0f }, { "wetSolo", 1.0f }, { "wetBusMainMode", 1.0f } });
    const auto reference = renderBus (solo, in, 512, true, nullptr);
    const auto actual = renderBus (routed, in, 512, true, nullptr);
    GF_EXPECT (sameValues (reference.main, actual.main));
    GF_EXPECT (peak (actual.main) > 0.01f);
}

GF_TEST (WetBus, routing_parameters_are_project_performance_state_and_old_states_default_to_neutral)
{
    GapFillAudioProcessor proc;
    for (const auto* id : { "wetBusMainMode", "wetBusLevel" })
    {
        GF_EXPECT (proc.getValueTreeState().getParameter (id) != nullptr);
        GF_EXPECT (performanceParameterIDs().contains (id));
        GF_EXPECT (! randomisableParameterIDs().contains (id));
    }
    setParams (proc, { { "wetBusMainMode", 1.0f }, { "wetBusLevel", -12.0f } });
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    GapFillAudioProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    GF_EXPECT (restored.getValueTreeState().getRawParameterValue ("wetBusMainMode")->load() == 1.0f);
    GF_EXPECT (restored.getValueTreeState().getRawParameterValue ("wetBusLevel")->load() == -12.0f);
    auto old = proc.getValueTreeState().copyState();
    for (const auto* id : { "wetBusMainMode", "wetBusLevel" })
    {
        const auto child = old.getChildWithProperty ("id", id);
        GF_EXPECT (child.isValid());
        old.removeChild (child, nullptr);
    }
    GapFillAudioProcessor legacy;
    GF_EXPECT (legacy.applyParameterState (old));
    GF_EXPECT (legacy.getValueTreeState().getRawParameterValue ("wetBusMainMode")->load() == 0.0f);
    GF_EXPECT (legacy.getValueTreeState().getRawParameterValue ("wetBusLevel")->load() == 0.0f);
}
