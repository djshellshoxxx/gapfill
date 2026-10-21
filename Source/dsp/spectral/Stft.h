#pragma once

// SP-0 Shared spectral processor (docs/dev/12 section 3, WP9).
//
// One audited STFT / phase-vocoder core for F-L20, F-X02 and F-X01: periodic
// Hann analysis and synthesis windows, hop = N/4 (overlap-add gain 1.5 is
// normalised, so an identity callback reconstructs the input), a juce::dsp::FFT
// plan created in prepare(), every buffer preallocated. The existing
// SpectralSmear is untouched and does not use this class.
//
// Latency: the output at time t is the input at time t - latencySamples(). For
// sample-by-sample (arbitrary host block size) delivery that is exactly N - 1:
// a hop-aligned block pipeline would need N - hop, but a sample stream must wait
// for the last frame that still overlaps the sample (deviation recorded in
// docs/progress/WP9.md).
//
// Threading: prepare()/reset() off the audio thread (prepare allocates); process()
// is real-time safe: no allocation, locks or I/O, denormals flushed, non-finite
// bins zeroed after the callback.

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace gapfill::spectral
{

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 6.28318530717958647692f;

/** Wraps an angle to [-pi, pi]. */
inline float wrapPi (float x) noexcept
{
    return x - kTwoPi * std::floor (x / kTwoPi + 0.5f);
}

/** One analysis frame of one channel. The callback edits `mag` / `phase` in
    place; the (modified) values are resynthesised. Arrays have numBins = N/2 + 1
    entries (bin k = k * sampleRate / N Hz). */
struct SpectralFrame
{
    int channel = 0;
    int numBins = 0;
    int fftSize = 0;
    int hop = 0;
    double sampleRate = 48000.0;
    std::int64_t frameIndex = 0;     ///< counts from 0 at prepare / reset
    float* mag = nullptr;
    float* phase = nullptr;
};

class SpectralCallback
{
public:
    virtual ~SpectralCallback() = default;

    /** Called on the audio thread once per channel per hop. Must not allocate. */
    virtual void processFrame (SpectralFrame&) noexcept = 0;
};

class Stft
{
public:
    static constexpr int kMinSize = 512;
    static constexpr int kMaxSize = 4096;

    /** 2048 up to 96 kHz, 4096 above (docs/dev/12 SP-0). */
    static int defaultSize (double sampleRate) noexcept { return sampleRate <= 96000.0 ? 2048 : 4096; }
    static bool isValidSize (int n) noexcept { return n >= kMinSize && n <= kMaxSize && (n & (n - 1)) == 0; }

    /** Allocates everything; never called on the audio thread. */
    void prepare (double sampleRate, int channels, int fftSize);
    void reset() noexcept;

    int size() const noexcept { return N; }
    int hop() const noexcept { return N / 4; }
    int numBins() const noexcept { return N / 2 + 1; }
    int numChannels() const noexcept { return channels; }
    double getSampleRate() const noexcept { return sr; }
    bool isPrepared() const noexcept { return N > 0; }

    /** Exact fixed delay between input and output, in samples (N - 1, see above). */
    int latencySamples() const noexcept { return N > 0 ? N - 1 : 0; }

    /** Bytes held by the processor (for the memory-ceiling record). */
    std::size_t preallocatedBytes() const noexcept;

    /** Processes `n` samples per channel. `in` and `out` may alias. */
    void process (const float* const* in, float** out, int n, SpectralCallback&) noexcept;

private:
    void runFrame (int ch, SpectralCallback&, std::int64_t frameIndex) noexcept;

    int N = 0;
    int channels = 0;
    double sr = 48000.0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;
    std::vector<float> fftBuf;                 ///< 2N
    std::vector<float> mag, phase;
    std::vector<std::vector<float>> inRing;    ///< per channel, N
    std::vector<std::vector<float>> acc;       ///< per channel, 2N (overlap-add by absolute index)
    float inverseScale = 1.0f;
    float synthGain = 1.0f;
    std::int64_t inCount = 0;                  ///< samples consumed (per channel)
    std::int64_t frames = 0;
};

//==============================================================================
// Phase-vocoder helpers

/** Expected phase advance of bin k over one hop. */
inline float binPhaseAdvance (int k, int fftSize, int hop) noexcept
{
    return kTwoPi * (float) k * (float) hop / (float) fftSize;
}

/** Instantaneous frequency of bin k in bins (k + deviation), from the
    unwrapped phase difference between consecutive frames. */
inline float instantaneousBin (int k, float phase, float prevPhase, int fftSize, int hop) noexcept
{
    const auto dev = wrapPi (phase - prevPhase - binPhaseAdvance (k, fftSize, hop));
    return (float) k + dev * (float) fftSize / (kTwoPi * (float) hop);
}

/** Identity phase locking (Laroche & Dolson 1999): every bin inside a spectral
    peak's region of influence takes the synthesis phase of the peak plus its
    own analysis-phase offset from the peak, preserving the vertical phase
    coherence of transients and partials. `synthPhase` must hold the propagated
    phase of every bin on entry (only peak bins keep it; others are rewritten).
    `peaks` is caller-owned scratch of numBins ints. Returns the peak count. */
int identityPhaseLock (const float* mag, const float* analysisPhase, float* synthPhase,
                       int numBins, int* peaks) noexcept;

} // namespace gapfill::spectral
