#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace gftest
{

//==============================================================================
// Signal generators
//==============================================================================

juce::AudioBuffer<float> makeSilence (int numChannels, int numSamples);

/** Single unit impulse at `position`. */
juce::AudioBuffer<float> makeImpulse (int numChannels, int numSamples, int position);

/** Repeated unit impulses. */
juce::AudioBuffer<float> makeImpulseTrain (double sampleRate, double seconds,
                                           double intervalSeconds, int numChannels = 2);

/** Drum-like exponential-decay hit written into an existing buffer. */
void addDrumHit (juce::AudioBuffer<float>& buffer, int position, double sampleRate,
                 float frequency, float decaySeconds, float amplitude, float noiseAmount);

/** Four-on-the-floor kick/clap pattern with clear gaps between the hits. */
juce::AudioBuffer<float> makeDrumLoop (double sampleRate, double seconds, double bpm,
                                       int numChannels = 2);

/** Low-frequency kicks only: the low band is busy while the top end is empty. */
juce::AudioBuffer<float> makeLowKicksWithOpenHighs (double sampleRate, double seconds, double bpm,
                                                    int numChannels = 2);

/** Sine bursts separated by silence. */
juce::AudioBuffer<float> makeSineBursts (double sampleRate, double seconds, float frequency,
                                         double burstSeconds, double gapSeconds,
                                         int numChannels = 2);

/** Alternating loud and quiet phrases (never digitally silent). */
juce::AudioBuffer<float> makePhrases (double sampleRate, double seconds,
                                      double phraseSeconds, int numChannels = 2);

/** Broadband noise bursts. */
juce::AudioBuffer<float> makeNoiseBursts (double sampleRate, double seconds,
                                          double burstSeconds, double gapSeconds,
                                          int numChannels = 2);

/** Continuous white noise at a given amplitude. */
juce::AudioBuffer<float> makeNoise (double sampleRate, double seconds, float amplitude,
                                    int numChannels = 2);

//==============================================================================
// Measurement helpers
//==============================================================================

bool allFinite (const juce::AudioBuffer<float>& buffer);
float maxAbs (const juce::AudioBuffer<float>& buffer);
float rmsLevel (const juce::AudioBuffer<float>& buffer);
float maxAbs (const juce::AudioBuffer<float>& buffer, int start, int numSamples);

/** Largest absolute difference between consecutive samples. */
float maxSampleJump (const juce::AudioBuffer<float>& buffer);

/** Click detector. Even a heavily distorted or bit-crushed waveform can swing by
    roughly its own peak from one sample to the next, so an absolute jump limit
    says nothing useful about it. What marks a click is a single-sample jump that
    the material around it cannot account for, so this measures the largest ratio
    of one jump to the fastest the waveform moves either side of it. A real
    attack passes: the transient it releases moves just as fast. Jumps quieter
    than `floorLevel` are ignored - they are inaudible whatever their ratio. */
float maxRelativeJump (const juce::AudioBuffer<float>& buffer, int lookbackSamples = 64,
                       float floorLevel = 0.02f);

/** Positions where a rectified, smoothed envelope crosses `threshold` upwards. */
std::vector<int> findOnsets (const juce::AudioBuffer<float>& buffer, double sampleRate,
                             float threshold, double minSpacingSeconds);

/** Energy of `b` minus energy of `a`, per sample region. */
float regionEnergy (const juce::AudioBuffer<float>& buffer, int start, int numSamples);

//==============================================================================
// Plugin driving helpers
//==============================================================================

/** Minimal play head so tests can supply (or withhold) transport information. */
class TestPlayHead : public juce::AudioPlayHead
{
public:
    TestPlayHead() = default;

    juce::Optional<PositionInfo> getPosition() const override
    {
        if (! provideInfo)
            return juce::nullopt;

        PositionInfo info;

        if (provideBpm)
            info.setBpm (bpm);

        if (providePpq)
            info.setPpqPosition (ppq);

        info.setIsPlaying (playing);
        info.setTimeInSamples (timeInSamples);
        return info;
    }

    void advance (int numSamples, double sampleRate)
    {
        timeInSamples += numSamples;

        if (playing)
            ppq += (double) numSamples / (60.0 / bpm * sampleRate);
    }

    bool provideInfo = true, provideBpm = true, providePpq = true, playing = true;
    double bpm = 120.0, ppq = 0.0;
    juce::int64 timeInSamples = 0;
};

/** Configures the processor's bus layout and prepares it. */
void prepareProcessor (juce::AudioProcessor& proc, double sampleRate, int blockSize,
                       bool withSidechain, bool mono = false);

/** Renders `input` (and optional `sidechain`) through the processor. */
juce::AudioBuffer<float> renderThroughPlugin (juce::AudioProcessor& proc,
                                              const juce::AudioBuffer<float>& input,
                                              const juce::AudioBuffer<float>* sidechain,
                                              int blockSize,
                                              TestPlayHead* playHead = nullptr);

} // namespace gftest
