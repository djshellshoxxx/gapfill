#pragma once

// C-4 Fill Intensity Envelope (docs/dev/04-SPEC-CREATIVE.md C-4).
//
// A shape s(age01) in [0,1] over the voice's life (age = elapsed / total length)
// scales the voice output gain by level = 1 - a * (1 - s) and darkens it with a
// one-pole lowpass at fc = 20000 * 0.1^(a * (1 - s)), both smoothed over 5 ms.
// With Flat or a == 0 the stage is inactive: no filter, no gain, no float ops,
// so the voice output is bit-identical to a build without the feature.
//
// Pure DSP, no JUCE UI, allocation free. One stage per FillVoice.

#include <cstdint>

namespace gapfill::envelope
{

enum Shape { Flat = 0, Rise, Fall, RiseFall, RandomWalk, NumShapes };

inline constexpr int numKnots = 5;

/** Shape value at `age01` (clamped to 0..1). `knots` is only read for RandomWalk
    (smoothstep interpolation between 5 knots in [0,1]). */
float shapeAt (int shape, float age01, const float (&knots)[numKnots]) noexcept;

/** Fills the RandomWalk knots from a seed (Rng seeded with `seed`). */
void makeKnots (std::uint64_t seed, float (&knots)[numKnots]) noexcept;

/** level = 1 - a * (1 - s), in [1 - a, 1]. */
float levelFor (float s, float amount01) noexcept;

/** fc = 20000 * 0.1^(a * (1 - s)) Hz, in [2000, 20000] for a in [0, 1]. */
float cutoffFor (float s, float amount01) noexcept;

class EnvelopeStage
{
public:
    /** `rngSeed` is the voice seed; the RandomWalk knots use hashCombine (rngSeed, kSalt_Envelope). */
    void start (int shape, float amount01, double sampleRate, std::uint64_t rngSeed) noexcept;

    bool isActive() const noexcept { return active; }

    /** Processes one stereo frame at voice age `age01` (0..1). No-op when inactive. */
    void process (float& l, float& r, float age01) noexcept;

private:
    bool active = false;
    int shape = Flat;
    float amount = 0.0f;
    float knots[numKnots] {};
    float sr = 48000.0f;
    float smoothCoeff = 0.01f;     // 5 ms one-pole
    float level = 1.0f;
    float fc = 20000.0f;
    float lpL = 0.0f, lpR = 0.0f;
};

} // namespace gapfill::envelope
