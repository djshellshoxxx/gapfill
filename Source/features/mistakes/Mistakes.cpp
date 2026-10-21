#include "Mistakes.h"
#include "../Salts.h"

#include <algorithm>
#include <cmath>

namespace gapfill::mistakes
{

namespace
{
    // Algo order: Reverse, Stutter, Delay, Granular, Freeze, PitchRise, PitchFall, Filter, Texture,
    // Crush, Distort, Scramble, Spectral, Comb, TapeStop.
    constexpr bool kOctave[15]   { true, true, false, true, true, true, true, false, false, false, false, false, false, false, false };
    constexpr bool kRepeat[15]   { false, true, true, false, false, false, false, false, false, false, false, true, false, false, false };
    constexpr bool kBrake[15]    { true, true, false, true, true, false, false, false, false, false, false, false, false, false, false };

    constexpr int kAlgoStutter = 1, kAlgoDelay = 2, kAlgoGranular = 3, kAlgoScramble = 11;
}

bool applicable (int type, int algo) noexcept
{
    if (algo < 0 || algo >= 15)
        return false;

    switch (type)
    {
        case Wobble:         return true;
        case OctaveDisplace: return kOctave[algo];
        case MissedRepeat:   return kRepeat[algo];
        case Brake:          return kBrake[algo];
        case StereoBounce:   return true;
        default:             return false;
    }
}

bool octaveUp (std::uint32_t seed) noexcept
{
    return ((seed ^ (seed >> 7) ^ (seed >> 16)) & 1u) != 0u;
}

bool skipped (int type, const Facts& f) noexcept
{
    if (! applicable (type, f.algo))
        return true;

    const auto lengthMs = 1000.0 * (double) f.lengthSamples / std::max (1.0, f.sampleRate);

    switch (type)
    {
        case OctaveDisplace:
            // x2 would consume twice the material: needs it to exist. Granular grains read ahead
            // of their start, so for them only the octave DOWN is allowed.
            return octaveUp (f.seed) && (f.algo == kAlgoGranular || f.sourceAvailable < 2 * f.lengthSamples);

        case MissedRepeat:   return f.repeats < kMinRepeats;
        case Brake:          return lengthMs < kMinBrakeMs;
        case StereoBounce:   return f.mono || f.stereoCoherent || lengthMs < kMinBounceMs;
        default:             return false;
    }
}

//==============================================================================
Roller::Roller (float mistakes01, std::uint64_t key) noexcept
    : amount01 (std::isfinite (mistakes01) ? std::clamp (mistakes01, 0.0f, 1.0f) : 0.0f)
{
    if (amount01 > 0.0f)
        rng.seed (hashCombine (key, kSalt_Mistakes));
}

bool Roller::hit() noexcept
{
    if (amount01 <= 0.0f)
        return false;

    ++drawCount;
    return rng.chance (kMaxProbability * amount01);
}

Roll Roller::pick (const Facts& facts) noexcept
{
    std::array<std::uint8_t, NumTypes - 1> candidates {};
    int count = 0;

    for (int t = Wobble; t < NumTypes; ++t)
        if (! skipped (t, facts))
            candidates[(size_t) count++] = (std::uint8_t) t;

    Roll r;

    if (count == 0)
        return r;

    ++drawCount;
    r.type = candidates[(size_t) rng.nextInt (count)];     // uniform over what remains == re-rolling skipped types
    r.amount = (std::uint8_t) std::clamp ((int) std::lround (amount01 * 100.0f), 0, 100);
    return r;
}

int estimateRepeats (int algo, std::int64_t lengthSamples, double sr, float stutterSeconds,
                     float delaySeconds, float complexity01) noexcept
{
    const auto c = std::clamp (complexity01, 0.0f, 1.0f);

    switch (algo)
    {
        case kAlgoStutter:
        {
            const auto slice = std::clamp ((double) stutterSeconds * sr, 0.01 * sr, 0.3 * sr);
            return (int) ((double) lengthSamples / std::max (1.0, slice));
        }
        case kAlgoDelay:
        {
            const auto spacing = std::max (1.0, (double) delaySeconds * sr);
            const auto taps = std::clamp (2 + (int) std::lround (c * 6.0f), 2, 10);
            return std::min (taps, (int) std::ceil ((double) lengthSamples / spacing));
        }
        case kAlgoScramble:
            return std::clamp (4 + (int) std::lround (c * 20.0f), 4, 32);
        default:
            return 0;
    }
}

//==============================================================================
void MistakeStage::start (const StageConfig& c) noexcept
{
    type = None;
    n = 0;

    if (c.type <= None || c.type >= NumTypes || ! applicable (c.type, c.algo) || c.lengthSamples <= 0)
        return;

    sr = std::max (8000.0, c.sampleRate);
    length = c.lengthSamples;
    amount = std::clamp (c.amount01, 0.0f, 1.0f);
    octUp = octaveUp (c.seed);

    if (c.type == OctaveDisplace && octUp && c.algo == kAlgoGranular)
        return;                                              // see skipped(): Granular only goes down

    if (c.type == StereoBounce && c.mono)
        return;

    Rng r;
    r.seed (hashCombine (c.seed, kSalt_Mistakes));

    switch (c.type)
    {
        case Wobble:
        {
            const auto hz = r.range (1.5f, 5.0f);
            wobblePhase = r.nextFloat();
            wobbleInc = hz / (float) sr;
            const auto rateDeviation = 0.03f * (0.3f + 0.7f * amount);            // at most +-3 % rate (~ +-0.5 semitone)
            wobbleDepth = std::min (900.0f, rateDeviation * (float) sr / (juce::MathConstants<float>::twoPi * hz));
            wobbleBase = wobbleDepth + 1.0f;
            wobbleWrite = 0;

            for (auto& ch : wobbleBuf)
                std::fill (std::begin (ch), std::end (ch), 0.0f);

            break;
        }

        case MissedRepeat:
        {
            if (c.repeatLen < 8 || c.repeats < kMinRepeats)
                return;

            ramp = std::clamp ((int) std::lround (0.002 * sr), 1, std::max (1, c.repeatLen / 4));
            const auto k = 1 + r.nextInt (std::max (1, c.repeats - 2));      // never the first repeat (keep the downbeat) nor the fading last one
            muteStart = k * c.repeatLen;
            muteEnd = (k + 1) * c.repeatLen;
            break;
        }

        case StereoBounce:
        {
            bounceCycles = (float) (2 + r.nextInt (2));
            bounceDepth = 0.4f + 0.5f * amount;
            break;
        }

        default: break;
    }

    type = c.type;
}

float MistakeStage::rateMultiplier (float t01) const noexcept
{
    if (type == OctaveDisplace)
        return octUp ? 2.0f : 0.5f;

    if (type == Brake)
    {
        const auto t = std::clamp ((double) t01, 0.0, 1.0);
        return t < kBrakeStart ? 1.0f
                               : (float) (1.0 - (1.0 - (double) kBrakeEndRate) * (t - kBrakeStart) / (1.0 - kBrakeStart));
    }

    return 1.0f;
}

void MistakeStage::process (float& l, float& r) noexcept
{
    if (type == None)
        return;

    const auto t01 = std::clamp ((float) n / (float) length, 0.0f, 1.0f);

    switch (type)
    {
        case Wobble:
        {
            wobbleBuf[0][wobbleWrite] = l;
            wobbleBuf[1][wobbleWrite] = r;

            const auto delay = wobbleBase + wobbleDepth * std::sin (juce::MathConstants<float>::twoPi * wobblePhase);
            const auto readPos = (float) wobbleWrite - delay;
            const auto base = std::floor (readPos);
            const auto frac = readPos - base;
            const auto i0 = ((int) base) & (wobbleBufferSize - 1);
            const auto i1 = (i0 + 1) & (wobbleBufferSize - 1);

            l = wobbleBuf[0][i0] + (wobbleBuf[0][i1] - wobbleBuf[0][i0]) * frac;
            r = wobbleBuf[1][i0] + (wobbleBuf[1][i1] - wobbleBuf[1][i0]) * frac;

            wobbleWrite = (wobbleWrite + 1) & (wobbleBufferSize - 1);
            wobblePhase += wobbleInc;
            wobblePhase -= std::floor (wobblePhase);
            break;
        }

        case MissedRepeat:
        {
            float g = 1.0f;

            if (n >= muteStart && n < muteEnd)
            {
                if (n < muteStart + ramp)        g = 1.0f - (float) (n - muteStart) / (float) ramp;
                else if (n >= muteEnd - ramp)    g = (float) (n - (muteEnd - ramp)) / (float) ramp;
                else                             g = 0.0f;
            }

            l *= g;
            r *= g;
            break;
        }

        case Brake:
        {
            const auto g = rateMultiplier (t01);      // matching gain fade
            l *= g;
            r *= g;
            break;
        }

        case StereoBounce:
        {
            const auto pan = bounceDepth * std::sin (juce::MathConstants<float>::twoPi * bounceCycles * t01)
                                         * std::sin (juce::MathConstants<float>::pi * t01);
            const auto theta = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            l *= 1.41421356f * std::cos (theta);
            r *= 1.41421356f * std::sin (theta);
            break;
        }

        default: break;
    }

    ++n;
}

} // namespace gapfill::mistakes
