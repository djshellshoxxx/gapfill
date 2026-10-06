#include "TestFramework.h"
#include "TestSignals.h"

#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    /** Sets parameters by id using real (not normalised) values. */
    void configure (GapFillAudioProcessor& proc,
                    std::initializer_list<std::pair<const char*, float>> values)
    {
        auto& state = proc.getValueTreeState();

        for (const auto& v : values)
        {
            auto* p = state.getParameter (v.first);
            GF_EXPECT_MSG (p != nullptr, juce::String ("unknown parameter id: ") + v.first);

            if (p != nullptr)
                p->setValueNotifyingHost (p->convertTo0to1 (v.second));
        }
    }

    /** A wet-only rendering of `input`, so measurements are not masked by the dry path. */
    juce::AudioBuffer<float> renderWetOnly (GapFillAudioProcessor& proc,
                                            const juce::AudioBuffer<float>& input,
                                            int blockSize,
                                            TestPlayHead* playHead = nullptr)
    {
        configure (proc, { { "dry", 0.0f }, { "wet", 100.0f } });
        return renderThroughPlugin (proc, input, nullptr, blockSize, playHead);
    }

    constexpr double kAllSampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
    constexpr int kAllBlockSizes[] = { 32, 64, 128, 231, 512, 1024, 2048 };
}

//==============================================================================
GF_TEST (Plugin, initialises_and_processes_at_every_supported_sample_rate)
{
    for (double sr : kAllSampleRates)
    {
        GapFillAudioProcessor proc;
        prepareProcessor (proc, sr, 512, true);
        configure (proc, { { "fill", 70.0f }, { "complexity", 60.0f }, { "intensity", 70.0f } });

        auto input = makeDrumLoop (sr, 1.5, 128.0, 2);
        auto output = renderThroughPlugin (proc, input, nullptr, 512);

        GF_EXPECT_MSG (allFinite (output), "non-finite output at " + juce::String (sr) + " Hz");
        GF_EXPECT_MSG (maxAbs (output) < 8.0f, "excessive level at " + juce::String (sr) + " Hz: "
                                                 + juce::String (maxAbs (output)));
        proc.releaseResources();
    }
}

GF_TEST (Plugin, handles_every_common_block_size)
{
    constexpr double sr = 48000.0;
    auto input = makeDrumLoop (sr, 1.5, 140.0, 2);

    for (int block : kAllBlockSizes)
    {
        GapFillAudioProcessor proc;
        prepareProcessor (proc, sr, block, true);
        configure (proc, { { "fill", 80.0f }, { "complexity", 70.0f } });

        auto output = renderThroughPlugin (proc, input, nullptr, block);

        GF_EXPECT_MSG (allFinite (output), "non-finite output at block size " + juce::String (block));
        GF_EXPECT_MSG (maxAbs (output) < 8.0f, "excessive level at block size " + juce::String (block));
    }
}

GF_TEST (Plugin, larger_block_than_prepared_is_handled)
{
    constexpr double sr = 48000.0;
    GapFillAudioProcessor proc;
    prepareProcessor (proc, sr, 256, true);
    configure (proc, { { "fill", 80.0f } });

    auto input = makeDrumLoop (sr, 1.0, 120.0, 2);

    // Deliberately feed blocks four times larger than prepareToPlay was told.
    auto output = renderThroughPlugin (proc, input, nullptr, 1024);

    GF_EXPECT (allFinite (output));
    GF_EXPECT (maxAbs (output) < 8.0f);
}

GF_TEST (Plugin, silence_in_stays_finite_and_silent)
{
    constexpr double sr = 48000.0;
    GapFillAudioProcessor proc;
    prepareProcessor (proc, sr, 512, true);
    configure (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "sensitivity", 100.0f },
                       { "gapThresh", 100.0f }, { "intensity", 100.0f }, { "complexity", 100.0f },
                       { "chaos", 100.0f } });

    auto input = makeSilence (2, (int) (sr * 3.0));
    auto output = renderThroughPlugin (proc, input, nullptr, 512);

    GF_EXPECT (allFinite (output));
    GF_EXPECT_MSG (maxAbs (output) < 1.0e-5f,
                   "silence produced output at " + juce::String (maxAbs (output)));
}

//==============================================================================
GF_TEST (Plugin, reported_latency_matches_the_actual_dry_delay)
{
    constexpr double sr = 48000.0;

    for (float lookaheadMs : { 0.0f, 10.0f, 40.0f, 120.0f, 250.0f })
    {
        GapFillAudioProcessor proc;
        prepareProcessor (proc, sr, 512, false);
        configure (proc, { { "lookahead", lookaheadMs }, { "fill", 0.0f },
                           { "dry", 100.0f }, { "wet", 0.0f }, { "output", 0.0f } });

        // prepareToPlay picks up the lookahead, so re-prepare after setting it.
        proc.prepareToPlay (sr, 512);

        const auto impulsePos = 4096;
        auto input = makeImpulse (2, (int) (sr * 1.0), impulsePos);
        auto output = renderThroughPlugin (proc, input, nullptr, 512);

        GF_EXPECT (allFinite (output));

        // Find the loudest sample: that is where the impulse came out.
        int peakIndex = 0;
        float peak = 0.0f;
        const auto* d = output.getReadPointer (0);

        for (int i = 0; i < output.getNumSamples(); ++i)
        {
            if (std::abs (d[i]) > peak)
            {
                peak = std::abs (d[i]);
                peakIndex = i;
            }
        }

        const auto reported = proc.getLatencySamples();
        const auto measured = peakIndex - impulsePos;

        GF_EXPECT_MSG (peak > 0.5f, "impulse did not survive the dry path at lookahead "
                                      + juce::String (lookaheadMs));
        GF_EXPECT_MSG (std::abs (measured - reported) <= 1,
                       "lookahead " + juce::String (lookaheadMs) + " ms: reported latency "
                         + juce::String (reported) + " but measured " + juce::String (measured));

        const auto expected = (int) std::round (lookaheadMs * 0.001 * sr);
        GF_EXPECT_MSG (std::abs (reported - expected) <= 1,
                       "reported latency " + juce::String (reported) + " != expected "
                         + juce::String (expected));
    }
}

GF_TEST (Plugin, changing_lookahead_while_running_stays_safe)
{
    constexpr double sr = 48000.0;
    GapFillAudioProcessor proc;
    prepareProcessor (proc, sr, 256, false);
    configure (proc, { { "fill", 60.0f } });

    auto input = makeDrumLoop (sr, 2.0, 130.0, 2);
    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> output (2, input.getNumSamples());
    output.clear();

    auto* lookahead = proc.getValueTreeState().getParameter ("lookahead");

    for (int pos = 0; pos + 256 <= input.getNumSamples(); pos += 256)
    {
        // Sweep the lookahead across its whole range every few blocks.
        const auto t = (float) (pos % 24000) / 24000.0f;
        lookahead->setValueNotifyingHost (t);

        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, input, ch, pos, 256);

        proc.processBlock (work, midi);

        for (int ch = 0; ch < 2; ++ch)
            output.copyFrom (ch, pos, work, ch, 0, 256);
    }

    GF_EXPECT (allFinite (output));
    GF_EXPECT_MSG (maxAbs (output) < 8.0f, "level grew to " + juce::String (maxAbs (output)));
}

//==============================================================================
GF_TEST (Plugin, state_round_trips_through_save_and_restore)
{
    GapFillAudioProcessor a, b;
    prepareProcessor (a, 48000.0, 512, true);
    prepareProcessor (b, 48000.0, 512, true);

    juce::Random rng (4242);

    for (const auto& id : allParameterIDs())
        if (auto* p = a.getValueTreeState().getParameter (id))
            p->setValueNotifyingHost (rng.nextFloat());

    juce::MemoryBlock block;
    a.getStateInformation (block);
    GF_EXPECT (block.getSize() > 0);

    b.setStateInformation (block.getData(), (int) block.getSize());

    for (const auto& id : allParameterIDs())
    {
        auto* pa = a.getValueTreeState().getParameter (id);
        auto* pb = b.getValueTreeState().getParameter (id);
        GF_EXPECT (pa != nullptr && pb != nullptr);

        // Compare what the DSP actually reads. A bool parameter keeps whatever
        // raw normalised float the host last pushed at it, so two parameters
        // that both mean "on" can hold different values for getValue().
        const auto va = pa->convertFrom0to1 (pa->getValue());
        const auto vb = pb->convertFrom0to1 (pb->getValue());

        GF_EXPECT_MSG (std::abs (va - vb) < 1.0e-4f * juce::jmax (1.0f, std::abs (va)),
                       "parameter " + id + " did not restore (" + juce::String (va)
                         + " vs " + juce::String (vb) + ")");
    }
}

GF_TEST (Plugin, state_with_unknown_and_missing_parameters_is_tolerated)
{
    GapFillAudioProcessor proc;
    prepareProcessor (proc, 48000.0, 512, false);

    configure (proc, { { "fill", 77.0f }, { "seed", 321.0f } });

    juce::MemoryBlock block;
    proc.getStateInformation (block);

    auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
    GF_EXPECT (xml != nullptr);

    if (xml != nullptr)
    {
        auto root = juce::ValueTree::fromXml (*xml);
        auto paramTree = root.getChild (0);
        GF_EXPECT (paramTree.isValid());

        // Remove a known parameter and add an unknown one.
        for (int i = paramTree.getNumChildren(); --i >= 0;)
            if (paramTree.getChild (i).getProperty ("id").toString() == "chaos")
                paramTree.removeChild (i, nullptr);

        juce::ValueTree unknown ("PARAM");
        unknown.setProperty ("id", "notARealParameter", nullptr);
        unknown.setProperty ("value", 0.5f, nullptr);
        paramTree.appendChild (unknown, nullptr);

        juce::MemoryBlock modified;
        auto modifiedXml = root.createXml();
        juce::AudioProcessor::copyXmlToBinary (*modifiedXml, modified);

        GapFillAudioProcessor other;
        prepareProcessor (other, 48000.0, 512, false);
        other.setStateInformation (modified.getData(), (int) modified.getSize());

        // The parameters that were present must have restored.
        auto* fillParam = other.getValueTreeState().getParameter ("fill");
        GF_EXPECT_NEAR (fillParam->convertFrom0to1 (fillParam->getValue()), 77.0f, 0.5f);

        // And the plugin still runs.
        auto input = makeDrumLoop (48000.0, 0.5, 120.0, 2);
        auto output = renderThroughPlugin (other, input, nullptr, 512);
        GF_EXPECT (allFinite (output));
    }
}

GF_TEST (Plugin, invalid_saved_program_index_restores_as_custom)
{
    GapFillAudioProcessor source;
    juce::MemoryBlock block;
    source.getStateInformation (block);

    auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
    GF_EXPECT (xml != nullptr);

    if (xml != nullptr)
    {
        auto root = juce::ValueTree::fromXml (*xml);
        root.setProperty ("program", 9999, nullptr);

        juce::MemoryBlock modified;
        if (auto rewritten = root.createXml())
            juce::AudioProcessor::copyXmlToBinary (*rewritten, modified);

        GapFillAudioProcessor restored;
        restored.setStateInformation (modified.getData(), (int) modified.getSize());

        GF_EXPECT_MSG (restored.getCurrentPresetName() == "Custom",
                       "invalid saved program should restore as Custom, got "
                         + restored.getCurrentPresetName());
        GF_EXPECT_MSG (restored.getCurrentProgram() >= 0
                         && restored.getCurrentProgram() < restored.getNumPrograms(),
                       "host-visible program index escaped the valid program range");
    }
}

GF_TEST (Plugin, editing_or_randomising_a_factory_preset_marks_it_custom)
{
    GapFillAudioProcessor proc;
    prepareProcessor (proc, 48000.0, 512, false);

    proc.selectPreset (juce::jmin (1, proc.getNumPrograms() - 1));
    GF_EXPECT (proc.getCurrentPresetName() != "Custom");

    configure (proc, { { "fill", 73.0f } });
    GF_EXPECT_MSG (proc.getCurrentPresetName() == "Custom",
                   "editing a factory preset did not mark the state Custom");

    proc.selectPreset (0);
    GF_EXPECT (proc.getCurrentPresetName() != "Custom");

    proc.randomise (false);
    GF_EXPECT_MSG (proc.getCurrentPresetName() == "Custom",
                   "randomising a factory preset did not mark the state Custom");
}

GF_TEST (Plugin, factory_presets_all_load_and_produce_valid_audio)
{
    constexpr double sr = 48000.0;
    auto input = makeDrumLoop (sr, 1.2, 128.0, 2);

    GapFillAudioProcessor proc;
    prepareProcessor (proc, sr, 512, true);

    GF_EXPECT (proc.getNumPrograms() >= 15);

    auto sidechain = makeDrumLoop (sr, 1.2, 128.0, 2);

    for (int i = 0; i < proc.getNumPrograms(); ++i)
    {
        proc.setCurrentProgram (i);
        proc.prepareToPlay (sr, 512);

        auto output = renderThroughPlugin (proc, input, &sidechain, 512);

        const auto name = proc.getProgramName (i);
        GF_EXPECT_MSG (allFinite (output), "preset '" + name + "' produced non-finite audio");
        GF_EXPECT_MSG (maxAbs (output) < 8.0f,
                       "preset '" + name + "' peaked at " + juce::String (maxAbs (output)));
        GF_EXPECT_MSG (name.isNotEmpty(), "preset " + juce::String (i) + " has no name");
    }
}

//==============================================================================
GF_TEST (Plugin, bypass_is_safe_and_transparent)
{
    constexpr double sr = 48000.0;
    GapFillAudioProcessor proc;
    prepareProcessor (proc, sr, 512, false);
    configure (proc, { { "fill", 100.0f }, { "lookahead", 0.0f } });
    proc.prepareToPlay (sr, 512);
    configure (proc, { { "bypass", 1.0f } });

    auto input = makeDrumLoop (sr, 1.0, 120.0, 2);
    auto output = renderThroughPlugin (proc, input, nullptr, 512);

    GF_EXPECT (allFinite (output));

    // With zero lookahead and bypass engaged the output should match the input
    // once the bypass ramp has settled.
    const auto start = (int) (sr * 0.1);
    float worst = 0.0f;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = start; i < input.getNumSamples(); ++i)
            worst = juce::jmax (worst, std::abs (output.getSample (ch, i) - input.getSample (ch, i)));

    GF_EXPECT_MSG (worst < 1.0e-4f, "bypassed output differed from input by " + juce::String (worst));
}

GF_TEST (Plugin, processBlockBypassed_preserves_latency_alignment)
{
    constexpr double sr = 48000.0;
    GapFillAudioProcessor proc;
    prepareProcessor (proc, sr, 512, false);
    configure (proc, { { "lookahead", 40.0f } });
    proc.prepareToPlay (sr, 512);

    const auto impulsePos = 2048;
    auto input = makeImpulse (2, (int) (sr * 0.6), impulsePos);

    juce::AudioBuffer<float> work (2, 512);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> output (2, input.getNumSamples());
    output.clear();

    for (int pos = 0; pos + 512 <= input.getNumSamples(); pos += 512)
    {
        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, input, ch, pos, 512);

        proc.processBlockBypassed (work, midi);

        for (int ch = 0; ch < 2; ++ch)
            output.copyFrom (ch, pos, work, ch, 0, 512);
    }

    int peakIndex = 0;
    float peak = 0.0f;

    for (int i = 0; i < output.getNumSamples(); ++i)
        if (std::abs (output.getSample (0, i)) > peak)
        {
            peak = std::abs (output.getSample (0, i));
            peakIndex = i;
        }

    GF_EXPECT (peak > 0.5f);
    GF_EXPECT_MSG (std::abs ((peakIndex - impulsePos) - proc.getLatencySamples()) <= 1,
                   "bypassed path delay " + juce::String (peakIndex - impulsePos)
                     + " != reported latency " + juce::String (proc.getLatencySamples()));
}

//==============================================================================
GF_TEST (Plugin, mono_processing_is_valid)
{
    constexpr double sr = 48000.0;
    GapFillAudioProcessor proc;
    prepareProcessor (proc, sr, 512, false, true);

    GF_EXPECT (proc.getMainBusNumInputChannels() == 1);

    configure (proc, { { "fill", 90.0f }, { "complexity", 80.0f }, { "intensity", 80.0f } });

    auto input = makeDrumLoop (sr, 1.5, 120.0, 1);
    auto output = renderThroughPlugin (proc, input, nullptr, 512);

    GF_EXPECT (output.getNumChannels() == 1);
    GF_EXPECT (allFinite (output));
    GF_EXPECT (maxAbs (output) < 8.0f);
}

GF_TEST (Plugin, stereo_channels_stay_independent)
{
    constexpr double sr = 48000.0;
    GapFillAudioProcessor proc;
    prepareProcessor (proc, sr, 512, false);
    configure (proc, { { "fill", 0.0f }, { "dry", 100.0f }, { "wet", 0.0f }, { "lookahead", 0.0f } });
    proc.prepareToPlay (sr, 512);

    // Different content per channel must come out on the same channel.
    auto input = makeSilence (2, (int) (sr * 0.5));

    for (int i = 0; i < input.getNumSamples(); ++i)
    {
        input.setSample (0, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * 200.0f * (float) i / (float) sr));
        input.setSample (1, i, 0.0f);
    }

    auto output = renderThroughPlugin (proc, input, nullptr, 512);

    GF_EXPECT (maxAbs (output, 0, output.getNumSamples()) > 0.4f);

    float rightMax = 0.0f;

    for (int i = 0; i < output.getNumSamples(); ++i)
        rightMax = juce::jmax (rightMax, std::abs (output.getSample (1, i)));

    GF_EXPECT_MSG (rightMax < 1.0e-4f, "signal leaked into the right channel: " + juce::String (rightMax));
}

//==============================================================================
GF_TEST (Plugin, missing_sidechain_does_not_break_anything)
{
    constexpr double sr = 48000.0;

    // Sidechain detection requested, but the bus is disabled entirely.
    GapFillAudioProcessor proc;
    prepareProcessor (proc, sr, 512, false);
    configure (proc, { { "detSource", (float) DetSource::Sidechain }, { "fill", 80.0f } });

    auto input = makeDrumLoop (sr, 1.5, 120.0, 2);
    auto output = renderThroughPlugin (proc, input, nullptr, 512);

    GF_EXPECT (allFinite (output));
    GF_EXPECT (maxAbs (output) < 8.0f);

    // It should have fallen back to main detection and still produced fills.
    GF_EXPECT_MSG (proc.getFillEngine().getFillsStarted() > 0,
                   "fallback to main detection produced no fills");
}

GF_TEST (Plugin, sidechain_drives_detection_on_a_static_source)
{
    constexpr double sr = 48000.0;

    // A sustained pad has no transients of its own, so any fills must be driven
    // by the sidechain.
    auto pad = makeSilence (2, (int) (sr * 3.0));

    for (int i = 0; i < pad.getNumSamples(); ++i)
    {
        const auto v = 0.35f * std::sin (juce::MathConstants<float>::twoPi * 180.0f * (float) i / (float) sr);

        for (int ch = 0; ch < 2; ++ch)
            pad.setSample (ch, i, v);
    }

    auto drums = makeDrumLoop (sr, 3.0, 120.0, 2);

    GapFillAudioProcessor withSidechain;
    prepareProcessor (withSidechain, sr, 512, true);
    configure (withSidechain, { { "detSource", (float) DetSource::Sidechain },
                                { "fill", 100.0f }, { "density", 100.0f },
                                { "minGap", 60.0f }, { "dry", 0.0f }, { "wet", 100.0f },
                                { "protSC", 30.0f }, { "protNext", 40.0f } });

    auto wetOutput = renderThroughPlugin (withSidechain, pad, &drums, 512);

    GF_EXPECT (allFinite (wetOutput));
    GF_EXPECT_MSG (withSidechain.getFillEngine().getFillsStarted() > 0,
                   "sidechain detection produced no fills");
    GF_EXPECT_MSG (rmsLevel (wetOutput) > 1.0e-4f, "sidechain-driven fills produced no audio");
}

//==============================================================================
GF_TEST (Plugin, seeded_auto_behaviour_is_deterministic)
{
    constexpr double sr = 48000.0;
    auto input = makeDrumLoop (sr, 2.5, 120.0, 2);

    auto render = [&] (int seed)
    {
        GapFillAudioProcessor proc;
        prepareProcessor (proc, sr, 512, false);
        configure (proc, { { "seed", (float) seed }, { "fill", 90.0f }, { "density", 100.0f },
                           { "complexity", 60.0f }, { "chaos", 60.0f },
                           { "dry", 0.0f }, { "wet", 100.0f } });

        TestPlayHead playHead;
        return renderWetOnly (proc, input, 512, &playHead);
    };

    auto a1 = render (7);
    auto a2 = render (7);
    auto b1 = render (8);

    float maxDiffSameSeed = 0.0f, maxDiffDifferentSeed = 0.0f;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < a1.getNumSamples(); ++i)
        {
            maxDiffSameSeed = juce::jmax (maxDiffSameSeed,
                                          std::abs (a1.getSample (ch, i) - a2.getSample (ch, i)));
            maxDiffDifferentSeed = juce::jmax (maxDiffDifferentSeed,
                                               std::abs (a1.getSample (ch, i) - b1.getSample (ch, i)));
        }

    GF_EXPECT_MSG (rmsLevel (a1) > 1.0e-4f, "seeded run produced no fills to compare");
    GF_EXPECT_MSG (maxDiffSameSeed < 1.0e-6f,
                   "the same seed produced different audio (max diff " + juce::String (maxDiffSameSeed) + ")");
    GF_EXPECT_MSG (maxDiffDifferentSeed > 1.0e-4f,
                   "different seeds produced identical audio");
}

GF_TEST (Plugin, transport_position_makes_playback_repeatable)
{
    constexpr double sr = 48000.0;
    auto input = makeDrumLoop (sr, 2.0, 120.0, 2);

    auto renderFrom = [&] (double startPpq)
    {
        GapFillAudioProcessor proc;
        prepareProcessor (proc, sr, 512, false);
        configure (proc, { { "seed", 21.0f }, { "fill", 95.0f }, { "density", 100.0f },
                           { "dry", 0.0f }, { "wet", 100.0f } });

        TestPlayHead playHead;
        playHead.ppq = startPpq;
        return renderWetOnly (proc, input, 512, &playHead);
    };

    auto first = renderFrom (0.0);
    auto again = renderFrom (0.0);

    float worst = 0.0f;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < first.getNumSamples(); ++i)
            worst = juce::jmax (worst, std::abs (first.getSample (ch, i) - again.getSample (ch, i)));

    GF_EXPECT_MSG (worst < 1.0e-6f, "replaying the same transport position changed the result");
}

GF_TEST (Plugin, missing_tempo_information_is_handled)
{
    constexpr double sr = 48000.0;
    auto input = makeDrumLoop (sr, 1.5, 120.0, 2);

    for (int variant = 0; variant < 4; ++variant)
    {
        GapFillAudioProcessor proc;
        prepareProcessor (proc, sr, 512, false);
        configure (proc, { { "fill", 80.0f }, { "syncEnable", 1.0f }, { "maxFillSync", 1.0f } });

        TestPlayHead playHead;
        playHead.provideInfo = (variant != 0);
        playHead.provideBpm  = (variant != 1);
        playHead.providePpq  = (variant != 2);
        playHead.playing     = (variant != 3);

        auto output = renderThroughPlugin (proc, input, nullptr, 512, &playHead);

        GF_EXPECT_MSG (allFinite (output), "variant " + juce::String (variant) + " produced non-finite audio");
        GF_EXPECT_MSG (maxAbs (output) < 8.0f, "variant " + juce::String (variant) + " level too high");
    }
}

GF_TEST (Plugin, transport_start_stop_is_handled)
{
    constexpr double sr = 48000.0;
    GapFillAudioProcessor proc;
    prepareProcessor (proc, sr, 512, false);
    configure (proc, { { "fill", 90.0f } });

    auto input = makeDrumLoop (sr, 3.0, 120.0, 2);

    TestPlayHead playHead;
    juce::AudioBuffer<float> work (2, 512);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> output (2, input.getNumSamples());
    output.clear();
    proc.setPlayHead (&playHead);

    int blockIndex = 0;

    for (int pos = 0; pos + 512 <= input.getNumSamples(); pos += 512, ++blockIndex)
    {
        // Stop and restart the transport (and jump the position) periodically.
        if (blockIndex % 20 == 10)
            playHead.playing = false;
        else if (blockIndex % 20 == 15)
        {
            playHead.playing = true;
            playHead.ppq = 4.0 * (double) (blockIndex / 20);
        }

        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, input, ch, pos, 512);

        proc.processBlock (work, midi);
        playHead.advance (512, sr);

        for (int ch = 0; ch < 2; ++ch)
            output.copyFrom (ch, pos, work, ch, 0, 512);
    }

    proc.setPlayHead (nullptr);

    GF_EXPECT (allFinite (output));
    GF_EXPECT (maxAbs (output) < 8.0f);
}

//==============================================================================
GF_TEST (Protection, transient_protect_reduces_activity_around_hits)
{
    constexpr double sr = 48000.0;
    const double bpm = 110.0;
    const auto beat = (int) (60.0 / bpm * sr);
    auto input = makeDrumLoop (sr, 4.0, bpm, 2);

    auto measureEnergyAroundHits = [&] (float protectAmount)
    {
        GapFillAudioProcessor proc;
        prepareProcessor (proc, sr, 512, false);
        // A short gap confirm plus long fills gets material still playing when the
        // next hit lands - which is the situation transient protect exists for.
        configure (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 40.0f },
                           { "maxFill", 600.0f }, { "protTrans", protectAmount },
                           { "protNext", 0.0f }, { "protLow", 20.0f },
                           { "gapRelease", 10.0f },
                           { "complexity", 100.0f }, { "chaos", 0.0f },
                           { "lookahead", 0.0f }, { "dry", 0.0f }, { "wet", 100.0f },
                           { "seed", 5.0f } });
        proc.prepareToPlay (sr, 512);

        auto wet = renderThroughPlugin (proc, input, nullptr, 512);

        double energy = 0.0;
        const auto window = (int) (sr * 0.03);

        for (int b = 2; b * beat + window < wet.getNumSamples(); ++b)
            energy += regionEnergy (wet, b * beat, window);

        return energy;
    };

    const auto unprotected = measureEnergyAroundHits (0.0f);
    const auto protectedEnergy = measureEnergyAroundHits (100.0f);

    GF_EXPECT_MSG (unprotected > 0.0, "no wet energy was generated to compare");
    GF_EXPECT_MSG (protectedEnergy < unprotected * 0.6,
                   "transient protect did not reduce activity around hits: "
                     + juce::String (protectedEnergy) + " vs " + juce::String (unprotected));
}

GF_TEST (Protection, next_event_protect_fades_fills_before_upcoming_events)
{
    constexpr double sr = 48000.0;
    const double bpm = 110.0;
    const auto beat = (int) (60.0 / bpm * sr);
    auto input = makeDrumLoop (sr, 4.0, bpm, 2);

    auto measureEnergyBeforeHits = [&] (float protectAmount)
    {
        GapFillAudioProcessor proc;
        prepareProcessor (proc, sr, 512, false);
        configure (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "minGap", 40.0f },
                           { "maxFill", 800.0f }, { "protTrans", 0.0f },
                           { "protNext", protectAmount }, { "protLow", 20.0f },
                           { "lookahead", 60.0f }, { "dry", 0.0f }, { "wet", 100.0f },
                           { "complexity", 95.0f }, { "intensity", 90.0f }, { "chaos", 0.0f },
                           { "seed", 11.0f } });
        proc.prepareToPlay (sr, 512);

        auto wet = renderThroughPlugin (proc, input, nullptr, 512);

        const auto latency = proc.getLatencySamples();
        double energy = 0.0;
        const auto window = (int) (sr * 0.015);

        // The 15 ms immediately before each hit, compensated for plugin latency.
        for (int b = 2; b * beat + latency < wet.getNumSamples(); ++b)
            energy += regionEnergy (wet, b * beat + latency - window, window);

        return energy;
    };

    const auto unprotected = measureEnergyBeforeHits (0.0f);
    const auto protectedEnergy = measureEnergyBeforeHits (100.0f);

    GF_EXPECT_MSG (unprotected > 0.0, "no wet energy was generated to compare");
    GF_EXPECT_MSG (protectedEnergy < unprotected * 0.5,
                   "next event protect did not clear the space before hits: "
                     + juce::String (protectedEnergy) + " vs " + juce::String (unprotected));
}

GF_TEST (Protection, low_protect_removes_low_frequency_content_from_the_wet_path)
{
    constexpr double sr = 48000.0;
    auto input = makeDrumLoop (sr, 3.0, 120.0, 2);

    auto measureLowEnergy = [&] (float lowProtectHz)
    {
        GapFillAudioProcessor proc;
        prepareProcessor (proc, sr, 512, false);
        configure (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "protLow", lowProtectHz },
                           { "protTrans", 0.0f }, { "protNext", 0.0f },
                           { "dry", 0.0f }, { "wet", 100.0f }, { "seed", 3.0f } });

        auto wet = renderThroughPlugin (proc, input, nullptr, 512);

        // Measure how much energy survives below 100 Hz.
        gapfill::BandSplitter split;
        split.prepare (sr, 1);
        split.setCrossovers (100.0f, 3500.0f);

        double lowEnergy = 0.0;

        for (int i = 0; i < wet.getNumSamples(); ++i)
        {
            float lo, mi, hi;
            split.processMono (0.5f * (wet.getSample (0, i) + wet.getSample (1, i)), lo, mi, hi);
            lowEnergy += (double) lo * lo;
        }

        return lowEnergy;
    };

    const auto off = measureLowEnergy (20.0f);
    const auto on  = measureLowEnergy (250.0f);

    GF_EXPECT_MSG (off > 0.0, "no wet energy generated");
    GF_EXPECT_MSG (on < off * 0.35,
                   "low protect did not clean up the sub region: " + juce::String (on)
                     + " vs " + juce::String (off));
}

GF_TEST (Protection, centre_protect_pushes_the_wet_path_to_the_sides)
{
    constexpr double sr = 48000.0;
    auto input = makeDrumLoop (sr, 3.0, 120.0, 2);

    auto measureMidSideRatio = [&] (float centreAmount)
    {
        GapFillAudioProcessor proc;
        prepareProcessor (proc, sr, 512, false);
        configure (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "protCenter", centreAmount },
                           { "protTrans", 0.0f }, { "protNext", 0.0f }, { "width", 100.0f },
                           { "dry", 0.0f }, { "wet", 100.0f }, { "seed", 17.0f } });

        auto wet = renderThroughPlugin (proc, input, nullptr, 512);

        double midEnergy = 0.0, sideEnergy = 0.0;

        for (int i = 0; i < wet.getNumSamples(); ++i)
        {
            const auto m = 0.5f * (wet.getSample (0, i) + wet.getSample (1, i));
            const auto s = 0.5f * (wet.getSample (0, i) - wet.getSample (1, i));
            midEnergy += (double) m * m;
            sideEnergy += (double) s * s;
        }

        return midEnergy / juce::jmax (1.0e-12, midEnergy + sideEnergy);
    };

    const auto off = measureMidSideRatio (0.0f);
    const auto on  = measureMidSideRatio (100.0f);

    GF_EXPECT_MSG (on < off, "centre protect did not reduce the mid proportion ("
                               + juce::String (on) + " vs " + juce::String (off) + ")");
    GF_EXPECT_MSG (on < 0.15, "centre protect at 100% still left " + juce::String (on * 100.0) + "% mid energy");
}

//==============================================================================
GF_TEST (Plugin, every_fill_algorithm_produces_audio_when_forced)
{
    constexpr double sr = 48000.0;
    // Slow pattern so even the longest algorithms have room.
    auto input = makeDrumLoop (sr, 6.0, 80.0, 2);

    for (int algo = 0; algo < numAlgos; ++algo)
    {
        GapFillAudioProcessor proc;
        prepareProcessor (proc, sr, 512, false);
        configure (proc, { { "fillType", (float) (algo + 1) },
                           { "fill", 100.0f }, { "density", 100.0f },
                           { "minGap", 60.0f }, { "maxFill", 700.0f },
                           { "intensity", 65.0f }, { "complexity", 55.0f }, { "chaos", 40.0f },
                           { "protTrans", 30.0f }, { "protNext", 40.0f }, { "protLow", 20.0f },
                           { "dry", 0.0f }, { "wet", 100.0f }, { "seed", 42.0f } });

        auto wet = renderThroughPlugin (proc, input, nullptr, 512);
        const auto name = juce::String (algoName ((Algo) algo));

        GF_EXPECT_MSG (allFinite (wet), name + ": non-finite output");
        GF_EXPECT_MSG (maxAbs (wet) < 8.0f, name + ": peaked at " + juce::String (maxAbs (wet)));
        GF_EXPECT_MSG (proc.getFillEngine().getFillsStarted() > 0, name + ": no fills were scheduled");
        GF_EXPECT_MSG (rmsLevel (wet) > 1.0e-5f, name + ": scheduled fills but produced no audio");
    }
}

GF_TEST (Plugin, fill_amount_controls_how_much_is_generated)
{
    constexpr double sr = 48000.0;
    auto input = makeDrumLoop (sr, 6.0, 110.0, 2);

    auto countFills = [&] (float fillPercent)
    {
        GapFillAudioProcessor proc;
        prepareProcessor (proc, sr, 512, false);
        configure (proc, { { "fill", fillPercent }, { "density", 100.0f }, { "minGap", 50.0f },
                           { "dry", 0.0f }, { "wet", 100.0f }, { "seed", 1.0f } });

        auto wet = renderThroughPlugin (proc, input, nullptr, 512);
        GF_EXPECT (allFinite (wet));
        return proc.getFillEngine().getFillsStarted();
    };

    const auto none = countFills (0.0f);
    const auto low  = countFills (20.0f);
    const auto high = countFills (100.0f);

    GF_EXPECT_MSG (none == 0, "Fill at 0% still generated " + juce::String (none) + " fills");
    GF_EXPECT_MSG (high > low, "Fill at 100% (" + juce::String (high) + ") should exceed 20% ("
                                 + juce::String (low) + ")");
    GF_EXPECT_MSG (low > 0, "Fill at 20% generated nothing at all");
}

//==============================================================================
GF_TEST (Plugin, extreme_settings_and_long_runs_stay_bounded)
{
    constexpr double sr = 48000.0;

    // All-maximum and all-minimum parameter settings, then a long run.
    for (int extreme = 0; extreme < 2; ++extreme)
    {
        GapFillAudioProcessor proc;
        prepareProcessor (proc, sr, 512, true);

        for (const auto& id : allParameterIDs())
        {
            if (id == "bypass")
                continue;

            if (auto* p = proc.getValueTreeState().getParameter (id))
                p->setValueNotifyingHost (extreme == 0 ? 0.0f : 1.0f);
        }

        // Keep the output trim sane so the assertion measures the DSP, not gain.
        if (auto* out = proc.getValueTreeState().getParameter ("output"))
            out->setValueNotifyingHost (out->convertTo0to1 (0.0f));

        proc.prepareToPlay (sr, 512);

        auto input = makeDrumLoop (sr, 8.0, 150.0, 2);
        auto sidechain = makeDrumLoop (sr, 8.0, 150.0, 2);
        auto output = renderThroughPlugin (proc, input, &sidechain, 512);

        GF_EXPECT_MSG (allFinite (output), "extreme setting " + juce::String (extreme) + " went non-finite");
        GF_EXPECT_MSG (maxAbs (output) < 8.0f,
                       "extreme setting " + juce::String (extreme) + " peaked at "
                         + juce::String (maxAbs (output)));

        // No runaway growth: the last second must not be far louder than the first.
        const auto early = rmsLevel (juce::AudioBuffer<float> (
            const_cast<float* const*> (output.getArrayOfReadPointers()), 2, (int) sr, (int) sr));
        const auto late = rmsLevel (juce::AudioBuffer<float> (
            const_cast<float* const*> (output.getArrayOfReadPointers()), 2,
            output.getNumSamples() - (int) sr, (int) sr));

        GF_EXPECT_MSG (late < juce::jmax (0.05f, early * 4.0f),
                       "level grew over time: " + juce::String (early) + " -> " + juce::String (late));
    }
}

GF_TEST (Plugin, rapid_parameter_changes_stay_stable)
{
    constexpr double sr = 48000.0;
    GapFillAudioProcessor proc;
    prepareProcessor (proc, sr, 128, true);

    auto input = makeDrumLoop (sr, 4.0, 140.0, 2);
    auto sidechain = makeDrumLoop (sr, 4.0, 90.0, 2);

    juce::AudioBuffer<float> work (4, 128);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> output (2, input.getNumSamples());
    output.clear();

    const auto ids = allParameterIDs();
    juce::Random rng (9876);

    for (int pos = 0; pos + 128 <= input.getNumSamples(); pos += 128)
    {
        // Change several parameters every single block.
        for (int i = 0; i < 6; ++i)
        {
            const auto& id = ids[rng.nextInt (ids.size())];

            if (id == "bypass" || id == "output")
                continue;

            if (auto* p = proc.getValueTreeState().getParameter (id))
                p->setValueNotifyingHost (rng.nextFloat());
        }

        work.clear();

        for (int ch = 0; ch < 2; ++ch)
        {
            work.copyFrom (ch, 0, input, ch, pos, 128);
            work.copyFrom (2 + ch, 0, sidechain, ch, pos, 128);
        }

        proc.processBlock (work, midi);

        for (int ch = 0; ch < 2; ++ch)
            output.copyFrom (ch, pos, work, ch, 0, 128);
    }

    GF_EXPECT (allFinite (output));
    GF_EXPECT_MSG (maxAbs (output) < 8.0f, "rapid changes peaked at " + juce::String (maxAbs (output)));
}

GF_TEST (Plugin, sample_rate_and_block_size_changes_are_safe)
{
    GapFillAudioProcessor proc;

    for (double sr : { 44100.0, 96000.0, 48000.0, 192000.0 })
    {
        for (int block : { 64, 1024, 231 })
        {
            prepareProcessor (proc, sr, block, true);
            configure (proc, { { "fill", 85.0f }, { "complexity", 70.0f } });

            auto input = makeDrumLoop (sr, 0.6, 130.0, 2);
            auto output = renderThroughPlugin (proc, input, nullptr, block);

            GF_EXPECT_MSG (allFinite (output), "sr " + juce::String (sr) + " block "
                                                 + juce::String (block) + " went non-finite");
            proc.reset();
        }
    }
}

//==============================================================================
GF_TEST (Editor, opens_and_closes_repeatedly_without_leaking_state)
{
    GapFillAudioProcessor proc;
    prepareProcessor (proc, 48000.0, 512, true);

    auto input = makeDrumLoop (48000.0, 0.4, 120.0, 2);
    juce::MidiBuffer midi;

    for (int i = 0; i < 6; ++i)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        GF_EXPECT (editor != nullptr);

        if (editor != nullptr)
        {
            GF_EXPECT (editor->getWidth() >= 800);
            GF_EXPECT (editor->getHeight() >= 560);

            editor->setSize (1200, 760);
            editor->setSize (900, 640);

            // Run audio while the editor exists so the visualiser fifo is exercised.
            juce::AudioBuffer<float> work (4, 512);

            for (int pos = 0; pos + 512 <= input.getNumSamples(); pos += 512)
            {
                work.clear();

                for (int ch = 0; ch < 2; ++ch)
                    work.copyFrom (ch, 0, input, ch, pos, 512);

                proc.processBlock (work, midi);
                GF_EXPECT (allFinite (work));
            }
        }
    }
}

//==============================================================================
// The FILE menu writes the APVTS tree straight to a .gapfill file and reads it
// back, so the round trip is what actually has to hold.
GF_TEST (Editor, preset_files_round_trip_through_disk)
{
    GapFillAudioProcessor proc;
    prepareProcessor (proc, 48000.0, 512, false);

    auto& state = proc.getValueTreeState();

    juce::Random random (20260916);
    randomiseParameters (state, true, random);

    // Snapshot every parameter before writing.
    juce::HashMap<juce::String, float> before;

    for (const auto& id : allParameterIDs())
        if (auto* v = state.getRawParameterValue (id))
            before.set (id, v->load());

    juce::TemporaryFile temp (".gapfill");
    auto file = temp.getFile();

    {
        auto xml = state.copyState().createXml();
        GF_EXPECT (xml != nullptr);
        GF_EXPECT (xml->writeTo (file));
    }

    GF_EXPECT (file.existsAsFile());
    GF_EXPECT (file.getSize() > 0);

    // Move everything away from the saved values, then load the file back.
    proc.resetAllParameters();

    {
        auto xml = juce::XmlDocument::parse (file);
        GF_EXPECT (xml != nullptr);

        auto tree = juce::ValueTree::fromXml (*xml);
        GF_EXPECT (tree.isValid());
        GF_EXPECT (tree.hasType (state.state.getType()));

        state.replaceState (tree);
    }

    // Compare the value the DSP actually reads. A bool parameter keeps the raw
    // normalised value it was set with, so getValue() can differ across a round
    // trip even when both sides mean the same thing.
    for (const auto& id : allParameterIDs())
        if (auto* v = state.getRawParameterValue (id))
            GF_EXPECT_MSG (std::abs (v->load() - before[id]) < 1.0e-4f,
                           id + ": saved " + juce::String (before[id])
                              + " but reloaded " + juce::String (v->load()));
}

//==============================================================================
// MIDI mappings are stored in their own child tree, and state written before
// that tree existed must still load.
GF_TEST (Editor, midi_mappings_survive_a_state_round_trip)
{
    GapFillAudioProcessor proc;
    prepareProcessor (proc, 48000.0, 512, false);

    auto& learn = proc.getMidiLearnManager();

    learn.setMapping ("wet", 21);
    learn.setMapping ("dry", 22);
    GF_EXPECT (learn.getNumMappings() == 2);

    juce::MemoryBlock saved;
    proc.getStateInformation (saved);

    learn.clearAllMappings();
    GF_EXPECT (learn.getNumMappings() == 0);

    proc.setStateInformation (saved.getData(), (int) saved.getSize());

    GF_EXPECT (learn.getMappedCC ("wet") == 21);
    GF_EXPECT (learn.getMappedCC ("dry") == 22);

    // One CC drives one parameter: re-pointing CC 21 must release "wet".
    learn.setMapping ("output", 21);
    GF_EXPECT (learn.getMappedCC ("output") == 21);
    GF_EXPECT (learn.getMappedCC ("wet") == -1);

    // A tree with no MIDMAP child must leave the current mappings alone rather
    // than silently wiping them.
    GapFillAudioProcessor older;
    juce::MemoryBlock legacy;
    older.getStateInformation (legacy);

    if (auto xml = juce::AudioProcessor::getXmlFromBinary (legacy.getData(), (int) legacy.getSize()))
    {
        auto root = juce::ValueTree::fromXml (*xml);
        auto stripped = root.getChildWithName (MidiLearnManager::stateType);
        GF_EXPECT_MSG (stripped.isValid(), "fresh state had no " + juce::String (MidiLearnManager::stateType) + " child");

        root.removeChild (stripped, nullptr);
        GF_EXPECT_MSG (! root.getChildWithName (MidiLearnManager::stateType).isValid(),
                       "failed to strip the map child");

        if (auto rewritten = root.createXml())
        {
            juce::MemoryBlock block;
            juce::AudioProcessor::copyXmlToBinary (*rewritten, block);
            proc.setStateInformation (block.getData(), (int) block.getSize());
        }
    }

    GF_EXPECT_MSG (learn.getMappedCC ("output") == 21,
                   "legacy state wiped the mappings: out -> "
                     + juce::String (learn.getMappedCC ("output"))
                     + ", total mappings " + juce::String (learn.getNumMappings()));
}

GF_TEST (Editor, midi_cc_flood_keeps_the_latest_controller_value)
{
    GapFillAudioProcessor proc;
    prepareProcessor (proc, 48000.0, 512, false);

    auto& learn = proc.getMidiLearnManager();
    learn.setMapping ("wet", 21);

    juce::MidiBuffer midi;
    constexpr int messageCount = 400;

    for (int i = 0; i < messageCount; ++i)
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 21, i % 128), i);

    learn.processMidi (midi);
    learn.applyPendingChanges();

    auto* wet = proc.getValueTreeState().getParameter ("wet");
    GF_EXPECT (wet != nullptr);

    if (wet != nullptr)
    {
        const auto expected = (float) ((messageCount - 1) % 128) / 127.0f;
        GF_EXPECT_NEAR (wet->getValue(), expected, 1.0e-5f);
    }
}

GF_TEST (Editor, reset_and_randomise_behave)
{
    GapFillAudioProcessor proc;
    prepareProcessor (proc, 48000.0, 512, false);

    auto& state = proc.getValueTreeState();

    // Randomise must actually change things, and never repeat itself.
    juce::Array<juce::String> signatures;

    for (int i = 0; i < 8; ++i)
    {
        proc.randomise (i % 2 == 0);

        juce::String sig;

        for (const auto& id : creativeParameterIDs())
            if (auto* p = state.getParameter (id))
                sig << juce::String (p->getValue(), 5) << ",";

        GF_EXPECT_MSG (! signatures.contains (sig), "RANDOM produced the same result twice");
        signatures.add (sig);
    }

    // Reset must restore every default.
    proc.resetAllParameters();

    for (const auto& id : allParameterIDs())
        if (auto* p = state.getParameter (id))
            GF_EXPECT_MSG (std::abs (p->getValue() - p->getDefaultValue()) < 1.0e-5f,
                           "parameter " + id + " was not reset to its default");
}


//==============================================================================
GF_TEST (Plugin, persisted_ui_metadata_round_trips_and_clamps)
{
    GapFillAudioProcessor source;
    source.setSavedEditorSize ({ 1450, 910 });
    source.setTooltipsEnabled (false);

    juce::MemoryBlock saved;
    source.getStateInformation (saved);

    GapFillAudioProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());

    GF_EXPECT (restored.getSavedEditorSize() == juce::Point<int> (1450, 910));
    GF_EXPECT (! restored.isTooltipsEnabled());

    auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
    GF_EXPECT (xml != nullptr);

    if (xml != nullptr)
    {
        auto root = juce::ValueTree::fromXml (*xml);
        root.setProperty ("editorWidth", 100000, nullptr);
        root.setProperty ("editorHeight", -100, nullptr);

        juce::MemoryBlock malformed;
        if (auto rewritten = root.createXml())
            juce::AudioProcessor::copyXmlToBinary (*rewritten, malformed);

        restored.setStateInformation (malformed.getData(), (int) malformed.getSize());
        GF_EXPECT (restored.getSavedEditorSize() == juce::Point<int> (3200, 560));
    }
}

GF_TEST (Plugin, runtime_lookahead_change_updates_reported_host_latency)
{
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;

    GapFillAudioProcessor proc;
    prepareProcessor (proc, sr, blockSize, false);
    configure (proc, { { "lookahead", 0.0f }, { "fill", 0.0f } });
    proc.prepareToPlay (sr, blockSize);
    GF_EXPECT (proc.getLatencySamples() == 0);

    configure (proc, { { "lookahead", 120.0f } });

    juce::AudioBuffer<float> work (2, blockSize);
    work.clear();
    juce::MidiBuffer midi;
    proc.processBlock (work, midi);

    // The processor deliberately reports latency from its message-thread timer.
    juce::Timer::callPendingTimersSynchronously();

    const auto expected = (int) std::round (0.120 * sr);
    GF_EXPECT_MSG (std::abs (proc.getLatencySamples() - expected) <= 1,
                   "runtime lookahead reported " + juce::String (proc.getLatencySamples())
                     + " samples instead of " + juce::String (expected));
}

GF_TEST (Plugin, spectral_band_probability_can_fully_disable_an_enabled_lane)
{
    constexpr double sr = 48000.0;
    auto input = makeDrumLoop (sr, 4.0, 110.0, 2);

    auto countFills = [&] (float highProbability)
    {
        GapFillAudioProcessor proc;
        prepareProcessor (proc, sr, 512, false);

        configure (proc, {
            { "detMode", (float) DetMode::Spectral },
            { "fill", 100.0f }, { "density", 100.0f },
            { "sensitivity", 75.0f }, { "gapThresh", 75.0f },
            { "minGap", 35.0f }, { "maxFill", 350.0f },
            { "bandLowOn", 0.0f }, { "bandMidOn", 0.0f }, { "bandHighOn", 1.0f },
            { "bandHighProb", highProbability }, { "bandHighAmt", 100.0f },
            { "dry", 0.0f }, { "wet", 100.0f }, { "seed", 41.0f }
        });

        auto output = renderThroughPlugin (proc, input, nullptr, 512);
        GF_EXPECT (allFinite (output));
        return proc.getFillEngine().getFillsStarted();
    };

    const auto disabled = countFills (0.0f);
    const auto enabled = countFills (100.0f);

    GF_EXPECT_MSG (disabled == 0,
                   "0% high-band probability still scheduled " + juce::String (disabled) + " fills");
    GF_EXPECT_MSG (enabled > 0,
                   "100% high-band probability did not schedule any fills");
}

GF_TEST (Plugin, spectral_band_intensity_changes_generated_audio)
{
    constexpr double sr = 48000.0;
    auto input = makeDrumLoop (sr, 4.0, 105.0, 2);

    auto renderAtIntensity = [&] (float intensity)
    {
        GapFillAudioProcessor proc;
        prepareProcessor (proc, sr, 512, false);

        configure (proc, {
            { "detMode", (float) DetMode::Spectral },
            { "fillType", (float) ((int) Algo::Distort + 1) },
            { "fill", 100.0f }, { "density", 100.0f },
            { "sensitivity", 80.0f }, { "gapThresh", 80.0f },
            { "minGap", 35.0f }, { "maxFill", 350.0f },
            { "bandLowOn", 0.0f }, { "bandMidOn", 0.0f }, { "bandHighOn", 1.0f },
            { "bandHighProb", 100.0f }, { "bandHighAmt", 100.0f },
            { "bandHighInt", intensity },
            { "dry", 0.0f }, { "wet", 100.0f }, { "seed", 77.0f }
        });

        auto output = renderThroughPlugin (proc, input, nullptr, 512);
        GF_EXPECT (allFinite (output));
        GF_EXPECT_MSG (proc.getFillEngine().getFillsStarted() > 0,
                       "spectral intensity comparison produced no fills");
        return output;
    };

    auto low = renderAtIntensity (0.0f);
    auto high = renderAtIntensity (100.0f);

    float maxDifference = 0.0f;

    for (int ch = 0; ch < low.getNumChannels(); ++ch)
        for (int i = 0; i < low.getNumSamples(); ++i)
            maxDifference = juce::jmax (maxDifference,
                std::abs (low.getSample (ch, i) - high.getSample (ch, i)));

    GF_EXPECT_MSG (maxDifference > 1.0e-4f,
                   "band intensity was wired but did not alter generated audio");
}
