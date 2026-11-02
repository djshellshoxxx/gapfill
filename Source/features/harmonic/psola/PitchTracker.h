#pragma once
#include "Settings.h"
#include <array>
#include <cstdint>

namespace gapfill::harmonic::psola
{
struct PitchFrame
{
    float f0Hz = 0.0f;
    float clarity = 0.0f;
    std::int64_t histPos = 0; // inclusive source-history endpoint of the snapshot
};

/** Single audio owner; fixed storage and <=16*512 difference MACs per32-input
    hop. Positions belong to source history. A Capture hold makes no push call.
    No reference is retained from the borrowed input span. */
class PitchTracker
{
public:
    struct Query { float f0Hz = 0, voicedFraction = 0, clarity = 0; bool voiced = false; };
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setActive (bool) noexcept;
    bool isActive() const noexcept { return active; }
    void push (const float* mono, int count, std::int64_t firstHistPos) noexcept;
    Query query (std::int64_t fromInclusive, std::int64_t toInclusive) const noexcept;

    int decimation() const noexcept { return decim; }
    double analysisRate() const noexcept { return rate / decim; }
    int storedFrames() const noexcept { return stored; }
    std::uint64_t totalFrames() const noexcept { return total; }
    const PitchFrame& frame (int oldestFirstIndex) const noexcept;
    int maxMacsPerHop() const noexcept { return maximumMacs; }
    int maxLagsPerHop() const noexcept { return maximumLags; }
    // Full analysed PCM extent plus a conservative low-pass settling interval.
    int sourceFootprintSamples() const noexcept { return (kDifferenceWindow + tauMax + 128) * decim; }

private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        float process (float x) noexcept;
    };
    void beginFrame() noexcept;
    void computeHop() noexcept;
    void finishFrame() noexcept;

    double rate = 48000;
    int decim = 4, tauMin = 12, tauMax = 240;
    bool active = false, working = false, havePosition = false;
    std::int64_t expectedPosition = 0, newestPosition = 0, snapshotPosition = 0;
    int inputHop = 0, decimCount = 0, ringWrite = 0, filled = 0, nextLag = 1, sinceSnapshot = 0;
    std::array<Biquad, 2> lowpass;
    std::array<float, kDecimatedRing> ring {};
    std::array<float, kDifferenceWindow + kMaxPitchLag> work {};
    std::array<float, kMaxPitchLag + 1> difference {}, normalised {};
    std::array<PitchFrame, kPitchFrames> frames {};
    int frameWrite = 0, stored = 0, maximumMacs = 0, maximumLags = 0;
    std::uint64_t total = 0;
};
static_assert (sizeof (PitchTracker) <= 40 * 1024);
} // namespace gapfill::harmonic::psola
