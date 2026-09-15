#pragma once

#include "DspUtils.h"

namespace gapfill
{

//==============================================================================
/** Streaming phase-vocoder style smear used by the SPECTRAL fill algorithm.

    Magnitudes are smoothed across frames (the "smear") while phases advance at
    their nominal bin rate with optional jitter, which gives the ghostly,
    de-transiented texture the algorithm is after. Overlap-add uses a Hann
    analysis and synthesis window at 75% overlap with the matching 2/3
    normalisation, so a steady input comes back out at unity level instead of
    building up.

    A single instance is shared by the engine: only one spectral fill runs at a
    time, which bounds the CPU cost. */
class SpectralSmear
{
public:
    static constexpr int fftOrder = 10;
    static constexpr int fftSize  = 1 << fftOrder;      // 1024
    static constexpr int hopSize  = fftSize / 4;        // 75% overlap
    static constexpr int numBins  = fftSize / 2 + 1;

    void prepare (double sampleRate, int numChannels);
    void reset();

    /** Smear amount 0..1 (magnitude persistence) and phase jitter 0..1. */
    void setCharacter (float smear01, float jitter01, std::uint64_t seed) noexcept;

    /** Processes one stereo sample pair. Output lags the input by fftSize
        samples - callers prime the processor before rendering. */
    void process (float inL, float inR, float& outL, float& outR) noexcept;

    /** Latency in samples between an input sample and its contribution. */
    static constexpr int latency() { return fftSize; }

    bool isClaimed() const noexcept { return claimed; }
    void claim() noexcept   { claimed = true; }
    void release() noexcept { claimed = false; }

private:
    struct Channel
    {
        std::vector<float> fifo, accum, fftData;
        std::vector<float> phase, smoothMag;
        int writeIdx = 0;
    };

    void computeFrame (Channel& c) noexcept;

    juce::dsp::FFT fft { fftOrder };
    std::vector<float> window;
    Channel channels[2];
    int hopCounter = 0;
    float smear = 0.7f;
    float jitter = 0.1f;
    Rng rng;
    bool claimed = false;
};

} // namespace gapfill
