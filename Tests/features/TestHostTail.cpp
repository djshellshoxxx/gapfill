// P3 TAIL acceptance (docs/progress/DETAILED-REMAINDER-AUDIT-20261031.md item 1):
// the host tail reported by the real processor must cover the audio that the real
// processor actually rings after its input stops. Covers every legacy bus card,
// the all-cards cascade, mono, supported sample rates, off ramps and concurrent
// host queries during processing.

#include "../TestFramework.h"
#include "../TestSignals.h"
#include "../support/TestRig.h"
#include "../support/ScopedTestThread.h"

#include "PluginProcessor.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <memory>

using namespace gapfill;
using namespace gftest;

namespace
{
    // -96 dBFS: below 16-bit resolution, i.e. inaudible once the host stops rendering the tail.
    constexpr float kSilence = 1.0f / 65536.0f;

    struct Ring
    {
        double reportedSeconds = 0;  ///< getTailLengthSeconds() after the input ended
        double ringSeconds = 0;      ///< last sample above kSilence, measured from the end of input
        double renderedSeconds = 0;  ///< silence rendered after the input
        bool finite = true;
        bool hadWet = false;         ///< the cards actually produced output while input played
    };

    /** Drum loop for `inputSeconds`, then silence for reported + `marginSeconds`. */
    Ring measure (std::initializer_list<std::pair<const char*, float>> values, double rate, bool mono,
                  double inputSeconds = 6.0, double marginSeconds = 4.0)
    {
        constexpr int block = 512;
        auto proc = std::make_unique<GapFillAudioProcessor>();
        prepareProcessor (*proc, rate, block, false, mono);
        setParams (*proc, { { "fill", 100.0f }, { "density", 100.0f } });
        setParams (*proc, values);

        TestPlayHead head;
        head.bpm = 120.0;
        head.playing = true;
        proc->setPlayHead (&head);

        const int channels = mono ? 1 : 2;
        const auto loop = drumLoop (rate, 120.0, 4);
        juce::AudioBuffer<float> buffer (channels, block);
        juce::MidiBuffer midi;
        const auto inputSamples = (std::int64_t) (inputSeconds * rate);
        const auto latency = proc->getLatencySamples();
        Ring ring;
        std::int64_t position = 0, lastLoud = -1;
        double plainEnergy = 0;

        // Input phase, then the reported tail (queried once input ends) plus a margin.
        std::int64_t end = inputSamples + (std::int64_t) (60.0 * rate);
        bool queried = false;

        while (position < end)
        {
            if (! queried && position >= inputSamples)
            {
                queried = true;
                ring.reportedSeconds = proc->getTailLengthSeconds();
                end = inputSamples + latency + (std::int64_t) ((ring.reportedSeconds + marginSeconds) * rate);
            }

            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const auto at = position + i;
                    buffer.setSample (ch, i, at < inputSamples
                        ? loop.getSample (juce::jmin (ch, loop.getNumChannels() - 1), (int) (at % loop.getNumSamples()))
                        : 0.0f);
                }

            midi.clear();
            proc->processBlock (buffer, midi);
            head.advance (block, rate);

            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const auto value = buffer.getSample (ch, i);
                    ring.finite = ring.finite && std::isfinite (value);
                    if (std::abs (value) > kSilence)
                        lastLoud = std::max (lastLoud, position + i);
                    if (position + i < inputSamples)
                        plainEnergy += (double) value * value;
                }

            position += block;
        }

        ring.hadWet = plainEnergy > 0;
        // The host's tail starts after the delayed (latency-compensated) end of input.
        ring.ringSeconds = (double) std::max<std::int64_t> (0, lastLoud + 1 - inputSamples - latency) / rate;
        ring.renderedSeconds = (double) (position - inputSamples - latency) / rate;
        proc->setPlayHead (nullptr);
        proc->releaseResources();
        return ring;
    }

    void expectCovered (const Ring& ring, const juce::String& context)
    {
        std::cout << "  " << context << ": rings " << ring.ringSeconds << " s, reports " << ring.reportedSeconds << " s"
                  << std::endl;
        GF_EXPECT_MSG (ring.finite && ring.hadWet, context + " produced finite audio");
        GF_EXPECT_MSG (ring.ringSeconds <= ring.reportedSeconds,
                       context + " rings " + juce::String (ring.ringSeconds) + " s beyond its reported "
                           + juce::String (ring.reportedSeconds) + " s tail");
        // The measurement window must extend past the reported tail, or the check above is vacuous.
        GF_EXPECT (ring.renderedSeconds > ring.reportedSeconds);
    }
}

GF_TEST (HostTail, each_legacy_card_rings_within_its_reported_tail)
{
    for (const auto* id : { "fmtAmount", "resAmount", "fsdAmount", "dubAmount", "cvAmount", "sfzAmount" })
        expectCovered (measure ({ { id, 100.0f } }, 48000.0, false), id);
}

GF_TEST (HostTail, maximum_dub_feedback_and_every_card_cascaded_stay_within_the_reported_tail)
{
    expectCovered (measure ({ { "dubAmount", 100.0f }, { "dubFeedback", 100.0f } }, 48000.0, false), "dub max feedback");
    expectCovered (measure ({ { "fmtAmount", 100.0f }, { "resAmount", 100.0f }, { "fsdAmount", 100.0f },
                              { "dubAmount", 100.0f }, { "dubFeedback", 100.0f }, { "cvAmount", 100.0f },
                              { "sfzAmount", 100.0f } }, 48000.0, false), "all cards");
}

GF_TEST (HostTail, mono_and_supported_rates_stay_within_the_reported_tail)
{
    expectCovered (measure ({ { "dubAmount", 100.0f }, { "dubFeedback", 100.0f } }, 48000.0, true), "dub mono");

    for (const auto rate : { 44100.0, 96000.0, 192000.0 })
        expectCovered (measure ({ { "dubAmount", 100.0f }, { "dubFeedback", 100.0f }, { "sfzAmount", 100.0f } },
                                rate, false, 3.0, 3.0), "dub+freeze @" + juce::String ((int) rate));
}

GF_TEST (HostTail, turning_every_card_off_returns_the_released_tail_after_the_ramp)
{
    auto proc = std::make_unique<GapFillAudioProcessor>();
    prepareProcessor (*proc, 48000.0, 512, false);
    setParams (*proc, { { "dubAmount", 100.0f }, { "sfzAmount", 100.0f } });
    GF_EXPECT (proc->getTailLengthSeconds() > 2.5 + 16.0);
    setParams (*proc, { { "dubAmount", 0.0f }, { "sfzAmount", 0.0f } });
    GF_EXPECT (proc->getTailLengthSeconds() == 2.5);
}

GF_TEST (HostTail, host_queries_during_processing_and_automation_are_finite_and_bounded)
{
    auto proc = std::make_unique<GapFillAudioProcessor>();
    prepareProcessor (*proc, 48000.0, 256, false);
    auto* dub = proc->getValueTreeState().getParameter ("dubAmount");
    GF_EXPECT (dub != nullptr);
    if (dub == nullptr)
        return;

    std::atomic<int> bad { 0 }, queries { 0 };
    {
        ScopedTestThread host ([&] (ScopedTestThread::StopToken stop)
        {
            while (! stop.stop_requested())
            {
                const auto seconds = proc->getTailLengthSeconds();
                // Either the released baseline or the baseline plus the prepared Dub bound.
                if (! std::isfinite (seconds) || seconds < 2.5 || seconds > 2.5 + 17.0)
                    bad.fetch_add (1);
                queries.fetch_add (1);
            }
        });
        ScopedTestThread automation ([&] (ScopedTestThread::StopToken stop)
        {
            for (int i = 0; ! stop.stop_requested(); ++i)
                dub->setValueNotifyingHost ((i & 1) != 0 ? 1.0f : 0.0f);
        });

        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        const auto loop = drumLoop (48000.0, 120.0, 1);
        for (int b = 0; b < 2000; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, 0, loop, ch, (b * 256) % (loop.getNumSamples() - 256), 256);
            proc->processBlock (buffer, midi);
            midi.clear();
        }
    }
    GF_EXPECT (queries.load() > 0);
    GF_EXPECT (bad.load() == 0);
}
