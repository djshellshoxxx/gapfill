#include "../support/VoiceRig.h"
#include "features/harmonic/psola/Voice.h"
#include <cmath>
#include <cstdio>
#include <vector>
using namespace gapfill;
using namespace gftest;
namespace
{
    double correlation (const std::vector<float>& samples, int first, int period)
    {
        double cross = 0, a = 0, b = 0;
        for (int i = first; i + period < (int) samples.size(); ++i)
        {
            const double x = samples[(std::size_t) i], y = samples[(std::size_t) (i + period)];
            cross += x * y; a += x * x; b += y * y;
        }
        return a > 1e-12 && b > 1e-12 ? cross / std::sqrt (a * b) : 0;
    }
}
GF_TEST (PitchSyncComparison, unison_exceeds_point_eight_while_actual_plain_granular_stays_below_point_four)
{
    constexpr double fundamental = 196;
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        VoiceRig rig (rate);
        const int sourceLength = (int) (rate * 1.5), outputLength = (int) (rate * 0.5);
        juce::AudioBuffer<float> source (2, sourceLength);
        for (int i = 0; i < sourceLength; ++i)
        {
            double value = 0;
            for (int partial = 1; partial <= 6; ++partial)
                value += (partial == 2 ? 1.2 : 1.0) / partial
                    * std::sin (6.283185307179586 * fundamental * partial * i / rate);
            for (int channel = 0; channel < 2; ++channel) source.setSample (channel, i, (float) (0.2 * value));
        }
        rig.history.clear(); rig.history.write (source, 0, sourceLength); rig.anchor = sourceLength - 1;
        FillVoice plainVoice; plainVoice.prepare (rate, 2);
        auto plainSetup = rig.setup (Algo::Granular, 500, 9, rate);
        plainSetup.intensity01 = plainSetup.chaos01 = 0.8f;
        bool started = false;
        const auto plainOutput = rig.render (plainVoice, plainSetup, outputLength, 64, &started);
        GF_EXPECT (started && allFinite (plainOutput));
        std::vector<float> plain ((std::size_t) outputLength), harmonic ((std::size_t) outputLength);
        for (int i = 0; i < outputLength; ++i)
            plain[(std::size_t) i] = 0.5f * (plainOutput.getSample (0, i) + plainOutput.getSample (1, i));

        namespace psola = gapfill::harmonic::psola;
        const int capturedLength = (int) (rate * 0.32);
        const float* captured = source.getReadPointer (0) + sourceLength - capturedLength;
        const psola::Capture capture { captured, nullptr, capturedLength, 1 };
        psola::PitchMarks finder; finder.prepare (rate);
        const auto epochs = finder.find (capture, (float) fundamental);
        GF_EXPECT (epochs.ready());
        psola::Setup setup;
        setup.settings.enabled = setup.voiced = true;
        setup.settings.voices = 1; setup.settings.motion01 = setup.settings.scatter01 = 0;
        setup.f0Hz = (float) fundamental; setup.clarity = 1;
        psola::Voice harmonicVoice;
        GF_EXPECT (harmonicVoice.configure (setup, capture, epochs.epochs, rate, 9, outputLength));
        for (int i = 0; i < outputLength; ++i)
        {
            float left = 0, right = 0;
            harmonicVoice.render (left, right);
            GF_EXPECT (std::isfinite (left) && left == right);
            harmonic[(std::size_t) i] = left;
        }
        const int period = (int) std::lround (rate / fundamental), first = (int) (rate / 12);
        const double harmonicCorrelation = correlation (harmonic, first, period);
        const double plainCorrelation = correlation (plain, first, period);
        std::printf (" M4 sr%.0f: pitch-sync%.6f actual Granular%.6f (limits>=0.8/<=0.4)\n", rate, harmonicCorrelation, plainCorrelation);
        GF_EXPECT_MSG (harmonicCorrelation >= 0.8, "Pitch-sync autocorrelation" + juce::String (harmonicCorrelation, 6));
        GF_EXPECT_MSG (plainCorrelation <= 0.4, "Actual plain Granular autocorrelation" + juce::String (plainCorrelation, 6));
    }
}
