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
        juce::AudioBuffer<float> wet;       // 2 ch (empty when the bus is disabled)
        int latency = 0;
    };

    /** Renders `in` through `proc` with the host-style in-place buffer: main in 0-1, sidechain in 2-3,
        wet bus out 2-3 (aliasing the sidechain, exactly as JUCE hands it to a plug-in). */
    BusRender renderBus (GapFillAudioProcessor& proc, const juce::AudioBuffer<float>& in, int block,
                         bool wetBus, const juce::AudioBuffer<float>* sidechain, TestPlayHead* ph = nullptr,
                         const std::function<void (int)>& onBlock = {})
    {
        prepareProcessor (proc, kSr, block, sidechain != nullptr, false, wetBus);
        TestPlayHead local;
        local.bpm = 150.0;
        proc.setPlayHead (ph != nullptr ? ph : &local);

        const auto total = juce::jmax (proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels());
        BusRender r;
        r.main.setSize (2, in.getNumSamples());
        r.wet.setSize (wetBus ? 2 : 0, in.getNumSamples());
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

                if (wetBus)
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

    // accepted: main mono / stereo, sidechain off / mono / stereo, wet off / stereo
    for (auto main : { mo, st })
        for (auto side : { off, mo, st })
            for (auto wet : { off, st })
                GF_EXPECT (proc.checkBusesLayoutSupported (layoutOf (main, main, side, wet)));

    // refused: wet bus mono or surround, main in != main out, surround main, surround sidechain
    GF_EXPECT (! proc.checkBusesLayoutSupported (layoutOf (st, st, off, mo)));
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
    GF_EXPECT (sameValues (reference.main, r.wet));          // unity, untouched by Dry / Wet / Output
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
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "bypass", 1.0f } });
    const auto r = renderBus (proc, drumLoop (kSr, 150.0, 3), 512, true, nullptr);
    GF_EXPECT (peak (r.wet) == 0.0f);
    GF_EXPECT (peak (r.main) > 0.1f);                        // the dry still passes
}

GF_TEST (WetBus, panic_silences_the_wet_bus)
{
    GapFillAudioProcessor proc;
    setParams (proc, kBusy);
    const int block = 512;
    const auto r = renderBus (proc, drumLoop (kSr, 150.0, 4), block, true, nullptr, nullptr, [&] (int idx)
    {
        if (idx == 100)
        {
            Command c;
            c.type = CommandType::Panic;
            proc.postCommand (c);
        }
    });

    juce::AudioBuffer<float> before (2, 100 * block), after (2, r.wet.getNumSamples() - 102 * block);

    for (int ch = 0; ch < 2; ++ch)
    {
        before.copyFrom (ch, 0, r.wet, ch, 0, before.getNumSamples());
        after.copyFrom (ch, 0, r.wet, ch, 102 * block, after.getNumSamples());
    }

    GF_EXPECT (peak (before) > 0.01f);
    GF_EXPECT (peak (after) == 0.0f);
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
