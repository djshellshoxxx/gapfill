#include "KeyEstimator.h"

#include <algorithm>
#include <cmath>

namespace gapfill::keydetect
{

namespace
{
    // Krumhansl-Kessler probe-tone ratings, tonic first.
    constexpr float kMajor[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    constexpr float kMinor[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

    constexpr float kMinCorrelation = 0.35f;
    constexpr float kSwitchMargin = 0.05f;
    constexpr int   kConfirmEvaluations = 3;
}

std::uint32_t pack (const KeyEstimate& e) noexcept
{
    return (std::uint32_t) (std::uint8_t) e.tonic | ((std::uint32_t) e.mode << 8)
         | ((std::uint32_t) e.confidence << 16) | ((e.valid ? 1u : 0u) << 24);
}

KeyEstimate unpack (std::uint32_t w) noexcept
{
    KeyEstimate e;
    e.tonic = (std::int8_t) (w & 0xFF);
    e.mode = (std::uint8_t) ((w >> 8) & 0xFF);
    e.confidence = (std::uint8_t) std::min<std::uint32_t> (100u, (w >> 16) & 0xFF);
    e.valid = ((w >> 24) & 1u) != 0;

    if (e.tonic < 0 || e.tonic > 11 || e.mode > 1)
        e = {};

    return e;
}

float profileValue (int keyIndex, int pc) noexcept
{
    const int tonic = keyIndex % 12;
    const int mode = keyIndex / 12;
    const int degree = ((pc - tonic) % 12 + 12) % 12;
    return mode == 0 ? kMajor[degree] : kMinor[degree];
}

float pearson (const float* a, const float* b, int n) noexcept
{
    double ma = 0.0, mb = 0.0;

    for (int i = 0; i < n; ++i)
    {
        ma += a[i];
        mb += b[i];
    }

    ma /= n;
    mb /= n;
    double sab = 0.0, saa = 0.0, sbb = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const double da = a[i] - ma, db = b[i] - mb;
        sab += da * db;
        saa += da * da;
        sbb += db * db;
    }

    return (saa > 1.0e-18 && sbb > 1.0e-18) ? (float) (sab / std::sqrt (saa * sbb)) : 0.0f;
}

KeyDecision evaluate (const float (&chroma)[12], float tonalMass, float minConfidence01) noexcept
{
    KeyDecision d;

    for (int k = 0; k < kNumKeys; ++k)
    {
        float profile[12];

        for (int pc = 0; pc < 12; ++pc)
            profile[pc] = profileValue (k, pc);

        d.r[(size_t) k] = pearson (chroma, profile, 12);
    }

    d.best = 0;

    for (int k = 1; k < kNumKeys; ++k)
        if (d.r[(size_t) k] > d.r[(size_t) d.best])
            d.best = k;

    d.second = d.best == 0 ? 1 : 0;

    for (int k = 0; k < kNumKeys; ++k)
        if (k != d.best && d.r[(size_t) k] > d.r[(size_t) d.second])
            d.second = k;

    const auto rb = d.r[(size_t) d.best], rs = d.r[(size_t) d.second];
    const auto clamp01 = [] (float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };

    d.confidence = clamp01 ((rb - rs) / 0.12f) * clamp01 (tonalMass / 0.4f) * clamp01 (rb / 0.5f);
    d.valid = d.confidence >= minConfidence01 && rb >= kMinCorrelation && d.confidence > 0.0f;
    return d;
}

void KeyHysteresis::reset() noexcept
{
    published = {};
    pendingKey = -1;
    pendingCount = invalidCount = 0;
    keyChanges = 0;
}

void KeyHysteresis::seed (const KeyEstimate& e) noexcept
{
    reset();
    published = e;
}

KeyEstimate KeyHysteresis::update (const KeyDecision& d) noexcept
{
    const auto toEstimate = [] (int key, float conf, bool valid)
    {
        KeyEstimate e;
        e.tonic = (std::int8_t) (key % 12);
        e.mode = (std::uint8_t) (key / 12);
        e.confidence = (std::uint8_t) std::lround (std::clamp (conf, 0.0f, 1.0f) * 100.0f);
        e.valid = valid;
        return e;
    };

    const int cur = published.keyIndex();

    if (! d.valid)
    {
        pendingKey = -1;
        pendingCount = 0;

        if (++invalidCount >= kConfirmEvaluations && published.valid)
            published.valid = false;                    // evidence gone: say "not detected", keep the key for comparisons

        return published;
    }

    invalidCount = 0;

    if (cur < 0)
    {
        // First lock needs three consistent evaluations.
        if (d.best == pendingKey)
            ++pendingCount;
        else
        {
            pendingKey = d.best;
            pendingCount = 1;
        }

        if (pendingCount >= kConfirmEvaluations)
        {
            published = toEstimate (d.best, d.confidence, true);
            pendingKey = -1;
            pendingCount = 0;
        }

        return published;
    }

    if (d.best == cur)
    {
        pendingKey = -1;
        pendingCount = 0;
        published = toEstimate (cur, d.confidence, true);
        return published;
    }

    // Another key leads: it needs a margin over the current key, repeatedly.
    if (d.r[(size_t) d.best] - d.r[(size_t) cur] >= kSwitchMargin)
    {
        if (d.best == pendingKey)
            ++pendingCount;
        else
        {
            pendingKey = d.best;
            pendingCount = 1;
        }

        if (pendingCount >= kConfirmEvaluations)
        {
            published = toEstimate (d.best, d.confidence, true);
            pendingKey = -1;
            pendingCount = 0;
            ++keyChanges;
        }
    }
    else
    {
        pendingKey = -1;
        pendingCount = 0;
    }

    if (! published.valid)
        published.valid = true;                         // evidence is back for the key we still hold

    return published;
}

} // namespace gapfill::keydetect
