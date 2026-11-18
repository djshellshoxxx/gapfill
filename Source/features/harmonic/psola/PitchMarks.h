#pragma once
#include "Settings.h"
#include <array>
#include <cstddef>
#include <vector>

namespace gapfill::harmonic::psola
{
/** Borrowed capture; channels is the ACTUAL source channel count, independent
    of a stereo wet/render bus. Memory stays stable for the voice lifetime. */
struct Capture
{
    const float* left = nullptr;
    const float* right = nullptr;
    int length = 0;
    int channels = 1;
};
struct EpochTable
{
    std::array<int, kEpochs> positions {}; // ascending, captured-buffer offsets
    int count = 0;
    float spacingCv = 0;
};
enum class EpochFailure { None, InvalidCapture, InvalidPitch, NonFinite, TooShort, Irregular };
struct EpochResult
{
    EpochTable epochs;
    EpochFailure failure = EpochFailure::InvalidCapture;
    bool ready() const noexcept { return failure == EpochFailure::None && epochs.count >= 4; }
};

/** Central scratch owner, reused sequentially at fill starts. Only prepare
    allocates. find borrows capture data for the call and returns owned marks. */
class PitchMarks
{
public:
    void prepare (double sampleRate);
    EpochResult find (const Capture&, float f0Hz) noexcept;
    int capacitySamples() const noexcept { return (int) scratch.size(); }
    std::size_t preallocatedBytes() const noexcept { return scratch.capacity() * sizeof (float); }
private:
    double rate = 48000;
    std::vector<float> scratch;
};
} // namespace gapfill::harmonic::psola
