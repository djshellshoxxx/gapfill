#pragma once
#include "IntervalSets.h"
#include "PitchMarks.h"
#include "Random.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace gapfill::harmonic::psola
{
/** Fixed-memory captured-buffer PSOLA. configure borrows immutable capture
    storage owned by the SAME FillVoice until reset/end. No source-history or
    processor pointer is retained. All new controls latch at configure. */
class Voice
{
public:
    void reset() noexcept;
    bool configure (const Setup&, const Capture&, const EpochTable&, double sampleRate,
                    std::uint32_t recipeSeed, int plannedSamples) noexcept;
    /** Add one stereo pair. Inactive leaves both arguments bit-identical. */
    void render (float& left, float& right) noexcept;
    bool active() const noexcept { return on; }
    int voices() const noexcept { return numStreams; }
    /** Renderer is unweighted. The adapter blends ordinary Granular by1-mix
        and this output bymix; zero mix skips epoch/configure work. */
    float mixWeight() const noexcept { return settings.settings.mix01; }
    int activeGrains() const noexcept;
    std::uint64_t faultCount() const noexcept { return faults; }
    std::size_t stateBytes() const noexcept { return sizeof (*this); }

private:
    struct Grain
    {
        double position = 0, cosine = 1, sine = 0;
        int age = 0;
        bool active = false;
    };
    struct Stream
    {
        double countdown = 0, period = 1, cosStep = 1, sinStep = 0;
        float normalise = 1, panL = 1, panR = 1;
        int length = 1;
        std::array<Grain, kGrainsPerVoice> grains;
    };
    void spawn (Stream&) noexcept;
    double read (const float*, double position) const noexcept;
    void fail() noexcept;

    Setup settings;
    Capture source;
    EpochTable epochs;
    detail::Random random { 0 };
    std::array<Stream, kMaxVoices> streams;
    double rate = 48000, sourcePeriod = 1, dcCoefficient = 0.999;
    double dcInput[2] {}, dcOutput[2] {}, toneLow[2] {};
    double toneCoefficient = 0.4, toneGain = 0;
    int duration = 0, elapsed = 0, numStreams = 0;
    bool on = false, faulted = false;
    std::uint64_t faults = 0;
};
static_assert (sizeof (Voice) <= 4 * 1024);
} // namespace gapfill::harmonic::psola
