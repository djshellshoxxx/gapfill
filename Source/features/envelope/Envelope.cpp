#include "Envelope.h"
#include "../Salts.h"
#include "../../dsp/DspUtils.h"

#include <algorithm>
#include <cmath>

namespace gapfill::envelope
{

float shapeAt (int shape, float age01, const float (&knots)[numKnots]) noexcept
{
    const auto t = std::clamp (std::isfinite (age01) ? age01 : 0.0f, 0.0f, 1.0f);

    switch (shape)
    {
        case Rise:      return t;
        case Fall:      return 1.0f - t;
        case RiseFall:  return std::sin (juce::MathConstants<float>::pi * t);
        case RandomWalk:
        {
            const auto pos = t * (float) (numKnots - 1);
            const auto i = std::min ((int) pos, numKnots - 2);
            const auto f = pos - (float) i;
            const auto sm = f * f * (3.0f - 2.0f * f);
            return knots[i] + (knots[i + 1] - knots[i]) * sm;
        }
        case Flat:
        default:        return 1.0f;
    }
}

void makeKnots (std::uint64_t seed, float (&knots)[numKnots]) noexcept
{
    Rng rng;
    rng.seed (seed);

    for (auto& k : knots)
        k = rng.nextFloat();
}

float levelFor (float s, float amount01) noexcept
{
    return 1.0f - amount01 * (1.0f - s);
}

float cutoffFor (float s, float amount01) noexcept
{
    return 20000.0f * std::pow (0.1f, amount01 * (1.0f - s));
}

void EnvelopeStage::start (int newShape, float amount01, double sampleRate, std::uint64_t rngSeed) noexcept
{
    amount = std::clamp (std::isfinite (amount01) ? amount01 : 0.0f, 0.0f, 1.0f);
    shape = std::clamp (newShape, 0, (int) NumShapes - 1);
    active = shape != Flat && amount > 0.0f;

    if (! active)
        return;

    sr = (float) sampleRate;
    smoothCoeff = onePoleCoeff (sampleRate, 5.0f);

    if (shape == RandomWalk)
        makeKnots (hashCombine (rngSeed, kSalt_Envelope), knots);

    // Start settled at the age-0 target so a fill does not begin with a transient.
    const auto s0 = shapeAt (shape, 0.0f, knots);
    level = levelFor (s0, amount);
    fc = cutoffFor (s0, amount);
    lpL = lpR = 0.0f;
}

void EnvelopeStage::process (float& l, float& r, float age01) noexcept
{
    if (! active)
        return;

    const auto s = shapeAt (shape, age01, knots);
    level += smoothCoeff * (levelFor (s, amount) - level);
    fc    += smoothCoeff * (cutoffFor (s, amount) - fc);

    const auto g = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * fc / sr);
    lpL += g * (l - lpL);
    lpR += g * (r - lpR);

    l = lpL * level;
    r = lpR * level;
}

} // namespace gapfill::envelope
