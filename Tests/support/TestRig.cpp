#include "TestRig.h"

namespace gftest
{

RenderResult renderOffline (gapfill::GapFillAudioProcessor& proc,
                            const juce::AudioBuffer<float>& in,
                            int blockSize, double sampleRate,
                            const RenderOptions& options)
{
    const bool withSidechain = options.sidechain != nullptr;
    prepareProcessor (proc, sampleRate, blockSize, withSidechain);

    TestPlayHead defaultPlayHead;
    auto* playHead = options.playHead != nullptr ? options.playHead : &defaultPlayHead;
    proc.setPlayHead (playHead);

    const auto mainChannels = proc.getMainBusNumInputChannels();
    const auto totalChannels = juce::jmax (proc.getTotalNumInputChannels(),
                                           proc.getTotalNumOutputChannels());

    RenderResult result;
    result.out.setSize (juce::jmax (1, proc.getMainBusNumOutputChannels()), in.getNumSamples());
    result.out.clear();

    juce::AudioBuffer<float> work (juce::jmax (1, totalChannels), juce::jmax (1, blockSize));
    juce::MidiBuffer midi;
    int blockIndex = 0;

    for (int pos = 0; pos < in.getNumSamples(); pos += blockSize, ++blockIndex)
    {
        const auto n = juce::jmin (blockSize, in.getNumSamples() - pos);

        if (options.onBlock)
            options.onBlock (blockIndex, pos);

        work.clear();

        for (int ch = 0; ch < mainChannels; ++ch)
            work.copyFrom (ch, 0, in, juce::jmin (ch, in.getNumChannels() - 1), pos, n);

        if (withSidechain && totalChannels > mainChannels)
            for (int ch = 0; ch < totalChannels - mainChannels; ++ch)
                work.copyFrom (mainChannels + ch, 0, *options.sidechain,
                               juce::jmin (ch, options.sidechain->getNumChannels() - 1), pos, n);

        juce::AudioBuffer<float> slice (work.getArrayOfWritePointers(), work.getNumChannels(), n);
        proc.processBlock (slice, midi);

        for (int ch = 0; ch < result.out.getNumChannels(); ++ch)
            result.out.copyFrom (ch, pos, work, juce::jmin (ch, work.getNumChannels() - 1), 0, n);

        playHead->advance (n, sampleRate);
    }

    proc.setPlayHead (nullptr);

    result.fills = proc.getFillEngine().getFillsStarted();
    result.gapsTooShort = proc.getFillEngine().getGapsRejectedTooShort();
    return result;
}

bool& writeGoldenMode()
{
    static bool flag = false;
    return flag;
}

juce::File testDataDir()
{
   #ifdef GAPFILL_TEST_DATA_DIR
    return juce::File (juce::CharPointer_UTF8 (GAPFILL_TEST_DATA_DIR));
   #else
    return juce::File();
   #endif
}

const char* goldenPlatform()
{
   #if JUCE_WINDOWS
    return "windows";
   #elif JUCE_MAC
    return "macos";
   #else
    return "linux";
   #endif
}

juce::File goldenPath (const juce::String& fileName)
{
    return testDataDir().getChildFile ("golden").getChildFile (goldenPlatform()).getChildFile (fileName);
}

bool writeGoldenFile (const juce::File& file, const juce::AudioBuffer<float>& b)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream out (file);

    if (! out.openedOk())
        return false;

    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        out.write (b.getReadPointer (ch), (size_t) b.getNumSamples() * sizeof (float));

    out.flush();
    return true;
}

bool readGoldenFile (const juce::File& file, juce::AudioBuffer<float>& b, int channels, int samples)
{
    juce::FileInputStream in (file);
    const auto bytes = samples * (int) sizeof (float);

    if (! in.openedOk() || in.getTotalLength() != (juce::int64) channels * bytes)
        return false;

    b.setSize (channels, samples);

    for (int ch = 0; ch < channels; ++ch)
        if (in.read (b.getWritePointer (ch), bytes) != bytes)
            return false;

    return true;
}

RenderResult renderGoldenScenarioA (gapfill::GapFillAudioProcessor& proc)
{
    TestPlayHead playHead;
    playHead.bpm = goldenBpm;

    RenderOptions options;
    options.playHead = &playHead;
    return renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, goldenBars),
                          goldenBlockSize, goldenSampleRate, options);
}

juce::AudioBuffer<float> drumLoop (double sampleRate, double bpm, int bars)
{
    return makeDrumLoop (sampleRate, (double) bars * 4.0 * 60.0 / bpm, bpm, 2);
}

void setParams (gapfill::GapFillAudioProcessor& proc,
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

BufferDiff compareBuffers (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b,
                           double maxTolerance)
{
    BufferDiff d;

    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
    {
        d.shapeMismatch = true;
        return d;
    }

    double sumSq = 0.0;
    const auto total = (double) a.getNumChannels() * (double) a.getNumSamples();

    for (int ch = 0; ch < a.getNumChannels(); ++ch)
    {
        const auto* pa = a.getReadPointer (ch);
        const auto* pb = b.getReadPointer (ch);

        for (int i = 0; i < a.getNumSamples(); ++i)
        {
            const auto e = std::abs ((double) pa[i] - (double) pb[i]);
            d.maxAbs = juce::jmax (d.maxAbs, e);
            sumSq += e * e;

            if (d.firstBad < 0 && ! (e <= maxTolerance))
            {
                d.firstBad = i;
                d.channel = ch;
            }
        }
    }

    d.rms = total > 0.0 ? std::sqrt (sumSq / total) : 0.0;
    return d;
}


RenderResult renderGoldenScenario (char id)
{
    gapfill::GapFillAudioProcessor proc;
    TestPlayHead playHead;
    playHead.bpm = goldenBpm;

    RenderOptions options;
    options.playHead = &playHead;

    const auto input = drumLoop (goldenSampleRate, goldenBpm, goldenBars);
    juce::AudioBuffer<float> click;

    switch (id)
    {
        case 'A': break;
        case 'B': setParams (proc, { { "detMode", 2.0f }, { "fill", 70.0f } }); break;
        case 'C':
            click = makeImpulseTrain (goldenSampleRate, (double) input.getNumSamples() / goldenSampleRate, 60.0 / goldenBpm);
            setParams (proc, { { "detSource", 1.0f }, { "fill", 60.0f } });
            options.sidechain = &click;
            break;
        case 'D': setParams (proc, { { "fillType", 15.0f }, { "fill", 80.0f } }); break;
        case 'E':
            setParams (proc, { { "complexity", 100.0f }, { "chaos", 100.0f }, { "fill", 100.0f },
                               { "density", 100.0f }, { "intensity", 100.0f } });
            break;
        case 'F':
            setParams (proc, { { "bypass", 1.0f }, { "fill", 70.0f } });
            options.onBlock = [&proc, half = input.getNumSamples() / 2] (int, int pos)
            {
                if (pos >= half)
                    setParams (proc, { { "bypass", 0.0f } });
            };
            break;
        default: break;
    }

    return renderOffline (proc, input, goldenBlockSize, goldenSampleRate, options);
}

} // namespace gftest
