#pragma once

// F-X01 Harmonic granular resynthesis: the grain engine owned by a Granular FillVoice.
//
// `hgVoices` streams, each a cloud of Hann grains whose length is an even number (4 or 6) of the
// detected periods. Grains of one stream overlap by 50 % and start a whole number of periods apart in
// the source, so overlapping grains stay in phase and the stream is a clean tone at f0 * ratio
// (pitch change comes from the read rate, as in N-FX2). Streams take the ordered interval set
// {unison, octave, fifth, third, -octave, +2 octaves}; the third is major or minor (Scale Lock).
// hgMotion moves the read position and detunes by up to 15 cents; hgTone tilts the brightness.
// Fixed memory: <= 6 streams x 4 grains (cap 96 is never approached). No allocation, no RNG draw
// outside a salted sub-generator seeded in configure().

#include "../../dsp/DspUtils.h"

#include <array>
#include <cstdint>

namespace gapfill::harmonic
{

inline constexpr int kMaxStreams = 6;
inline constexpr int kGrainsPerStream = 4;
inline constexpr int kGrainCap = 96;

/** What the scheduler decided for one fill (part of VoiceSetup; neutral = disabled). */
struct HarmonicSetup
{
    bool  enabled = false;
    float f0Hz = 0.0f;
    float confidence = 0.0f;
    int   voices = 3;
    float motion01 = 0.3f;
    float tone = 0.0f;                ///< -1 .. 1
    bool  minorThird = false;
};

/** Ratio of interval `index` in the ordered set (0 unison, 1 octave, 2 fifth, 3 third, 4 -octave, 5 +2 octaves). */
float intervalRatio (int index, bool minorThird) noexcept;

class HarmonicGrains
{
public:
    void reset() noexcept;

    /** `anchor` is the absolute source-history position the fill is cut from. */
    void configure (const HarmonicSetup&, std::int64_t anchor, double sampleRate, std::uint64_t voiceSeed) noexcept;

    bool active() const noexcept { return on; }

    /** One output sample, added to l / r. */
    void render (float& l, float& r, const SourceHistory&, int channels) noexcept;

    int activeGrains() const noexcept;
    int grainLengthSamples() const noexcept { return grainSourceLen; }

private:
    struct Grain
    {
        bool   active = false;
        double pos = 0.0;             ///< source read position
        double rate = 1.0;
        int    age = 0, length = 1;
    };

    struct Stream
    {
        double ratio = 1.0;
        float  lfoPhase = 0.0f;
        float  panL = 0.7071f, panR = 0.7071f;
        double countdown = 0.0;       ///< output samples until the next grain
        Grain  grains[kGrainsPerStream];
    };

    void spawn (Stream&, int streamIndex) noexcept;

    bool on = false;
    HarmonicSetup cfg;
    std::int64_t anchorPos = 0;
    double sr = 48000.0;
    double t0 = 0.0;                  ///< detected period in samples
    int    periodsPerGrain = 4;
    int    grainSourceLen = 1;
    int    maxBackPeriods = 0;
    long   elapsed = 0;
    float  lfoHz = 0.1f;
    float  toneLp[2] { 0.0f, 0.0f };
    float  toneCoef = 0.4f;
    Rng    rng;
    int    numStreams = 0;
    std::array<Stream, kMaxStreams> streams;
};

} // namespace gapfill::harmonic
