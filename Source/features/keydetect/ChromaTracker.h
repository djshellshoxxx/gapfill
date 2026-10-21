#pragma once

// F-X03 front end: mono input -> decimated STFT -> peak-based, tonality-gated
// chroma -> leaky accumulation. Sample-count driven (block-size invariant), all
// buffers allocated in prepare(); the FFT is one 4096-point real transform per
// hop (2048 decimated samples, about 170 ms).

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>
#include <vector>

namespace gapfill::keydetect
{

class ChromaTracker
{
public:
    static constexpr int kFftOrder = 12;
    static constexpr int kFftSize = 1 << kFftOrder;      // 4096
    static constexpr int kHop = kFftSize / 2;            // decimated samples
    static constexpr int kBins = kFftSize / 2 + 1;

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Leaky-accumulation time constant (seconds). */
    void setWindowSeconds (float seconds) noexcept;

    /** Feeds mono input; returns the number of frames completed by this call. */
    int process (const float* mono, int numSamples) noexcept;

    const float (&chroma() const noexcept)[12] { return accum; }
    float tonalMass() const noexcept { return mass; }
    float tuningCents() const noexcept { return tuning; }
    int framesAnalysed() const noexcept { return frames; }
    bool lastFrameGated() const noexcept { return lastGate; }
    int decimation() const noexcept { return decim; }
    double hopSeconds() const noexcept { return hopSec; }

    /** Builds one frame from the newest kFftSize decimated samples (public for tests). */
    void analyseFrame() noexcept;

private:
    double sr = 48000.0;
    int decim = 4;
    double hopSec = 0.17;
    float lambda = 0.99f;
    float windowSec = 12.0f;

    // 4th-order Butterworth low-pass as two biquads (direct form II transposed).
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    Biquad lp[2];
    int decimCounter = 0;

    std::vector<float> ring;               // newest decimated samples
    int ringWrite = 0, sinceFrame = 0, filled = 0;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> fftData;            // 2 * kFftSize
    std::vector<float> window;
    std::vector<float> mag;
    std::vector<double> prefix;            // running sums for the local mean

    float accum[12] {};
    float mass = 0.0f;
    float tuning = 0.0f;
    int frames = 0;
    bool lastGate = false;
};

} // namespace gapfill::keydetect
