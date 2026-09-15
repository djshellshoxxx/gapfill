#include "TestSignals.h"
#include <cmath>

namespace gftest
{

namespace
{
    juce::Random& testRandom()
    {
        static juce::Random r (0x5eed1234);
        return r;
    }
}

//==============================================================================
juce::AudioBuffer<float> makeSilence (int numChannels, int numSamples)
{
    juce::AudioBuffer<float> b (juce::jmax (1, numChannels), juce::jmax (1, numSamples));
    b.clear();
    return b;
}

juce::AudioBuffer<float> makeImpulse (int numChannels, int numSamples, int position)
{
    auto b = makeSilence (numChannels, numSamples);

    if (juce::isPositiveAndBelow (position, b.getNumSamples()))
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            b.setSample (ch, position, 1.0f);

    return b;
}

juce::AudioBuffer<float> makeImpulseTrain (double sampleRate, double seconds,
                                           double intervalSeconds, int numChannels)
{
    const auto n = (int) (sampleRate * seconds);
    auto b = makeSilence (numChannels, n);
    const auto step = juce::jmax (1, (int) (sampleRate * intervalSeconds));

    for (int pos = 0; pos < n; pos += step)
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            b.setSample (ch, pos, 1.0f);

    return b;
}

void addDrumHit (juce::AudioBuffer<float>& buffer, int position, double sampleRate,
                 float frequency, float decaySeconds, float amplitude, float noiseAmount)
{
    const auto length = juce::jmin (buffer.getNumSamples() - position,
                                    (int) (sampleRate * decaySeconds * 4.0));

    if (position < 0 || length <= 0)
        return;

    auto& rng = testRandom();
    const auto decayCoeff = (float) std::exp (-1.0 / (decaySeconds * sampleRate));
    float env = 1.0f;
    double phase = 0.0;
    const auto phaseInc = juce::MathConstants<double>::twoPi * frequency / sampleRate;

    for (int i = 0; i < length; ++i)
    {
        const auto tone = (float) std::sin (phase);
        const auto noise = rng.nextFloat() * 2.0f - 1.0f;
        const auto v = amplitude * env * ((1.0f - noiseAmount) * tone + noiseAmount * noise);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addSample (ch, position + i, v);

        phase += phaseInc;
        env *= decayCoeff;
    }
}

juce::AudioBuffer<float> makeDrumLoop (double sampleRate, double seconds, double bpm, int numChannels)
{
    const auto n = (int) (sampleRate * seconds);
    auto b = makeSilence (numChannels, n);

    const auto beatSamples = (int) (60.0 / bpm * sampleRate);

    for (int beat = 0; beat * beatSamples < n; ++beat)
    {
        const auto pos = beat * beatSamples;

        if (beat % 2 == 0)
            addDrumHit (b, pos, sampleRate, 55.0f, 0.055f, 0.85f, 0.06f);   // kick
        else
            addDrumHit (b, pos, sampleRate, 220.0f, 0.045f, 0.62f, 0.75f);  // clap/snare

        // Off-beat hat so there is a little detail between the main hits.
        addDrumHit (b, pos + beatSamples / 2, sampleRate, 7000.0f, 0.012f, 0.16f, 0.9f);
    }

    return b;
}

juce::AudioBuffer<float> makeLowKicksWithOpenHighs (double sampleRate, double seconds, double bpm,
                                                    int numChannels)
{
    const auto n = (int) (sampleRate * seconds);
    auto b = makeSilence (numChannels, n);
    const auto beatSamples = (int) (60.0 / bpm * sampleRate);

    for (int beat = 0; beat * beatSamples < n; ++beat)
        addDrumHit (b, beat * beatSamples, sampleRate, 48.0f, 0.09f, 0.9f, 0.0f);

    return b;
}

juce::AudioBuffer<float> makeSineBursts (double sampleRate, double seconds, float frequency,
                                         double burstSeconds, double gapSeconds, int numChannels)
{
    const auto n = (int) (sampleRate * seconds);
    auto b = makeSilence (numChannels, n);

    const auto burst = juce::jmax (1, (int) (sampleRate * burstSeconds));
    const auto period = burst + juce::jmax (1, (int) (sampleRate * gapSeconds));
    const auto phaseInc = juce::MathConstants<double>::twoPi * frequency / sampleRate;
    const auto fade = juce::jmax (4, burst / 40);

    for (int start = 0; start < n; start += period)
    {
        double phase = 0.0;

        for (int i = 0; i < burst && start + i < n; ++i)
        {
            auto env = 1.0f;

            if (i < fade)                 env = (float) i / (float) fade;
            else if (i > burst - fade)    env = (float) (burst - i) / (float) fade;

            const auto v = 0.7f * env * (float) std::sin (phase);

            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                b.setSample (ch, start + i, v);

            phase += phaseInc;
        }
    }

    return b;
}

juce::AudioBuffer<float> makePhrases (double sampleRate, double seconds,
                                      double phraseSeconds, int numChannels)
{
    const auto n = (int) (sampleRate * seconds);
    auto b = makeSilence (numChannels, n);
    const auto phrase = juce::jmax (1, (int) (sampleRate * phraseSeconds));
    auto& rng = testRandom();

    double phase = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const auto inLoudPhrase = ((i / phrase) % 2) == 0;
        const auto amp = inLoudPhrase ? 0.6f : 0.004f;     // quiet, but never digital silence
        const auto freq = inLoudPhrase ? 330.0 : 180.0;

        phase += juce::MathConstants<double>::twoPi * freq / sampleRate;

        const auto v = amp * ((float) std::sin (phase) * 0.85f + (rng.nextFloat() * 2.0f - 1.0f) * 0.15f);

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            b.setSample (ch, i, v);
    }

    return b;
}

juce::AudioBuffer<float> makeNoiseBursts (double sampleRate, double seconds,
                                          double burstSeconds, double gapSeconds, int numChannels)
{
    const auto n = (int) (sampleRate * seconds);
    auto b = makeSilence (numChannels, n);
    const auto burst = juce::jmax (1, (int) (sampleRate * burstSeconds));
    const auto period = burst + juce::jmax (1, (int) (sampleRate * gapSeconds));
    auto& rng = testRandom();

    for (int start = 0; start < n; start += period)
        for (int i = 0; i < burst && start + i < n; ++i)
        {
            const auto env = 1.0f - (float) i / (float) burst;
            const auto v = 0.6f * env * (rng.nextFloat() * 2.0f - 1.0f);

            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                b.setSample (ch, start + i, v);
        }

    return b;
}

juce::AudioBuffer<float> makeNoise (double sampleRate, double seconds, float amplitude, int numChannels)
{
    const auto n = (int) (sampleRate * seconds);
    auto b = makeSilence (numChannels, n);
    auto& rng = testRandom();

    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < n; ++i)
            b.setSample (ch, i, amplitude * (rng.nextFloat() * 2.0f - 1.0f));

    return b;
}

//==============================================================================
bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* d = buffer.getReadPointer (ch);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (d[i]))
                return false;
    }

    return true;
}

float maxAbs (const juce::AudioBuffer<float>& buffer)
{
    return maxAbs (buffer, 0, buffer.getNumSamples());
}

float maxAbs (const juce::AudioBuffer<float>& buffer, int start, int numSamples)
{
    const auto s = juce::jlimit (0, buffer.getNumSamples(), start);
    const auto n = juce::jlimit (0, buffer.getNumSamples() - s, numSamples);

    if (n <= 0 || buffer.getNumChannels() <= 0)
        return 0.0f;

    return buffer.getMagnitude (s, n);
}

float rmsLevel (const juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0)
        return 0.0f;

    double sum = 0.0;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* d = buffer.getReadPointer (ch);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            sum += (double) d[i] * d[i];
    }

    return (float) std::sqrt (sum / (double) (buffer.getNumChannels() * buffer.getNumSamples()));
}

float maxSampleJump (const juce::AudioBuffer<float>& buffer)
{
    float worst = 0.0f;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* d = buffer.getReadPointer (ch);

        for (int i = 1; i < buffer.getNumSamples(); ++i)
            worst = juce::jmax (worst, std::abs (d[i] - d[i - 1]));
    }

    return worst;
}

std::vector<int> findOnsets (const juce::AudioBuffer<float>& buffer, double sampleRate,
                             float threshold, double minSpacingSeconds)
{
    std::vector<int> onsets;

    if (buffer.getNumChannels() <= 0)
        return onsets;

    const auto minSpacing = juce::jmax (1, (int) (sampleRate * minSpacingSeconds));
    const auto* d = buffer.getReadPointer (0);

    float env = 0.0f;
    const auto attack = 1.0f - std::exp (-1.0f / (0.0005f * (float) sampleRate));
    const auto release = 1.0f - std::exp (-1.0f / (0.05f * (float) sampleRate));

    int lastOnset = -minSpacing * 2;
    bool armed = true;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto rect = std::abs (d[i]);
        env += (rect > env ? attack : release) * (rect - env);

        if (armed && env > threshold && i - lastOnset >= minSpacing)
        {
            onsets.push_back (i);
            lastOnset = i;
            armed = false;
        }
        else if (env < threshold * 0.4f)
        {
            armed = true;
        }
    }

    return onsets;
}

float regionEnergy (const juce::AudioBuffer<float>& buffer, int start, int numSamples)
{
    const auto s = juce::jlimit (0, buffer.getNumSamples(), start);
    const auto n = juce::jlimit (0, buffer.getNumSamples() - s, numSamples);

    if (n <= 0)
        return 0.0f;

    double sum = 0.0;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* d = buffer.getReadPointer (ch) + s;

        for (int i = 0; i < n; ++i)
            sum += (double) d[i] * d[i];
    }

    return (float) (sum / (double) juce::jmax (1, buffer.getNumChannels()));
}

//==============================================================================
void prepareProcessor (juce::AudioProcessor& proc, double sampleRate, int blockSize,
                       bool withSidechain, bool mono)
{
    const auto channelSet = mono ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (channelSet);
    layout.outputBuses.add (channelSet);
    layout.inputBuses.add (withSidechain ? juce::AudioChannelSet::stereo()
                                         : juce::AudioChannelSet::disabled());

    proc.setBusesLayout (layout);
    proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);
}

juce::AudioBuffer<float> renderThroughPlugin (juce::AudioProcessor& proc,
                                              const juce::AudioBuffer<float>& input,
                                              const juce::AudioBuffer<float>* sidechain,
                                              int blockSize,
                                              TestPlayHead* playHead)
{
    const auto mainChannels = proc.getMainBusNumInputChannels();
    const auto totalChannels = juce::jmax (proc.getTotalNumInputChannels(),
                                           proc.getTotalNumOutputChannels());

    juce::AudioBuffer<float> output (juce::jmax (1, proc.getMainBusNumOutputChannels()),
                                     input.getNumSamples());
    output.clear();

    juce::AudioBuffer<float> work (juce::jmax (1, totalChannels), juce::jmax (1, blockSize));
    juce::MidiBuffer midi;

    if (playHead != nullptr)
        proc.setPlayHead (playHead);

    for (int pos = 0; pos < input.getNumSamples(); pos += blockSize)
    {
        const auto n = juce::jmin (blockSize, input.getNumSamples() - pos);
        work.clear();

        for (int ch = 0; ch < mainChannels; ++ch)
            work.copyFrom (ch, 0, input, juce::jmin (ch, input.getNumChannels() - 1), pos, n);

        if (sidechain != nullptr && totalChannels > mainChannels)
        {
            const auto scChannels = totalChannels - mainChannels;

            for (int ch = 0; ch < scChannels; ++ch)
                work.copyFrom (mainChannels + ch, 0, *sidechain,
                               juce::jmin (ch, sidechain->getNumChannels() - 1), pos, n);
        }

        juce::AudioBuffer<float> slice (work.getArrayOfWritePointers(), work.getNumChannels(), n);
        proc.processBlock (slice, midi);

        for (int ch = 0; ch < output.getNumChannels(); ++ch)
            output.copyFrom (ch, pos, work, juce::jmin (ch, work.getNumChannels() - 1), 0, n);

        if (playHead != nullptr)
            playHead->advance (n, proc.getSampleRate());
    }

    if (playHead != nullptr)
        proc.setPlayHead (nullptr);

    return output;
}

} // namespace gftest
