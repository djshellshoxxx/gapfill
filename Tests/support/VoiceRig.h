#pragma once

// Helpers for tests that drive a single FillVoice (and its effect chain)
// directly, without the scheduler: a source history with known material, a
// VoiceSetup builder, and a render loop with a selectable block size.

#include "../TestFramework.h"
#include "../TestSignals.h"

#include "dsp/FillVoice.h"
#include "features/fx/FxHooks.h"

#include <cmath>
#include <vector>

namespace gftest
{

constexpr double kRigSr = 48000.0;

struct VoiceRig
{
    gapfill::SourceHistory history;
    gapfill::SpectralSmear spectral;
    std::int64_t anchor = 0;

    explicit VoiceRig (double sr = kRigSr, double seconds = 3.0, std::uint32_t noiseSeed = 7)
    {
        history.prepare (2, (int) (sr * 6.0));
        spectral.prepare (sr, 2);

        const auto n = (int) (sr * seconds);
        juce::AudioBuffer<float> src (2, n);
        juce::Random rnd ((juce::int64) noiseSeed);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                src.setSample (ch, i, (rnd.nextFloat() * 2.0f - 1.0f) * 0.5f);

        history.write (src, 0, n);
        anchor = n - 1;
    }

    /** Replaces the history with a constant (DC) signal. */
    void fillDc (float value, double seconds = 3.0, double sr = kRigSr)
    {
        history.clear();
        const auto n = (int) (sr * seconds);
        juce::AudioBuffer<float> src (2, n);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                src.setSample (ch, i, value);

        history.write (src, 0, n);
        anchor = n - 1;
    }

    gapfill::VoiceSetup setup (gapfill::Algo algo, double lengthMs, std::uint64_t seed = 11,
                               double sr = kRigSr) const
    {
        gapfill::VoiceSetup s;
        s.algo = algo;
        s.sampleRate = sr;
        s.lengthSamples = (int) (sr * lengthMs * 0.001);
        s.fadeInSamples = (int) (sr * 0.004);
        s.fadeOutSamples = (int) (sr * 0.02);
        s.amp = 0.8f;
        s.intensity01 = 0.5f;
        s.complexity01 = 0.0f;       // no stacked modifiers: keeps expectations simple
        s.chaos01 = 0.0f;
        s.sourceAnchor = anchor;
        s.sinceEventSamples = (int) (sr * 0.12);
        s.rngSeed = seed;
        s.fxStart.seed = seed;
        s.fxStart.lengthSamples = s.lengthSamples;
        return s;
    }

    /** Starts the voice and renders `totalSamples` in blocks of `block`. */
    juce::AudioBuffer<float> render (gapfill::FillVoice& voice, const gapfill::VoiceSetup& s,
                                     int totalSamples, int block = 32, bool* started = nullptr,
                                     int stopAt = -1, int stopFade = 0)
    {
        spectral.release();
        const auto ok = voice.start (s, history, &spectral);

        if (started != nullptr)
            *started = ok;

        juce::AudioBuffer<float> wet (2, totalSamples);
        wet.clear();

        int pos = 0;

        while (pos < totalSamples)
        {
            auto n = juce::jmin (block, totalSamples - pos);

            if (stopAt >= 0 && pos < stopAt)
                n = juce::jmin (n, stopAt - pos);

            voice.render (wet, pos, n, history);
            pos += n;

            if (stopAt >= 0 && pos == stopAt)
                voice.requestStop (stopFade);
        }

        return wet;
    }
};

inline bool buffersIdentical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;

    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * (size_t) a.getNumSamples()) != 0)
            return false;

    return true;
}

/** Index of the last sample whose magnitude exceeds `threshold` (-1 if none). */
inline int lastAbove (const juce::AudioBuffer<float>& b, float threshold)
{
    for (int i = b.getNumSamples() - 1; i >= 0; --i)
        if (std::abs (b.getSample (0, i)) > threshold || std::abs (b.getSample (1, i)) > threshold)
            return i;

    return -1;
}

inline double rmsOf (const juce::AudioBuffer<float>& b, int from, int count, int ch = 0)
{
    double acc = 0.0;

    for (int i = from; i < from + count; ++i)
        acc += (double) b.getSample (ch, i) * (double) b.getSample (ch, i);

    return std::sqrt (acc / (double) std::max (1, count));
}

} // namespace gftest
