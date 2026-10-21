#include "TasteModel.h"

#include "../../Parameters.h"

#include <algorithm>
#include <cmath>

namespace gapfill::taste
{

namespace
{
    constexpr float kAlpha = 2.0f;
    constexpr float kConfidenceRatings = 40.0f;
    constexpr float kMin = 0.5f, kMax = 2.0f;

    void featuresOf (const FillRecipe& r, int (&out)[2], int& n) noexcept
    {
        n = 0;

        if (r.algo < kAlgoFeatures)
            out[n++] = r.algo;

        if (r.band >= 0 && r.band < kBandFeatures)
            out[n++] = kAlgoFeatures + r.band;
    }
}

void addRating (Counts& c, const FillRecipe& r, int rating) noexcept
{
    if (rating != 1 && rating != -1)
        return;

    int f[2], n;
    featuresOf (r, f, n);

    if (rating > 0)
    {
        c.likes = (std::uint16_t) std::min (65535, c.likes + 1);

        for (int i = 0; i < n; ++i)
            c.liked[(size_t) f[i]] = (std::uint16_t) std::min (65535, c.liked[(size_t) f[i]] + 1);
    }
    else
    {
        c.skips = (std::uint16_t) std::min (65535, c.skips + 1);

        for (int i = 0; i < n; ++i)
            c.skipped[(size_t) f[i]] = (std::uint16_t) std::min (65535, c.skipped[(size_t) f[i]] + 1);
    }
}

float score (const Counts& c, int f) noexcept
{
    if (f < 0 || f >= kFeatures)
        return 0.0f;

    const float l = (float) c.liked[(size_t) f], d = (float) c.skipped[(size_t) f];
    return std::log ((l + kAlpha) / ((float) c.likes + 2.0f * kAlpha)) - std::log ((d + kAlpha) / ((float) c.skips + 2.0f * kAlpha));
}

bool isReady (const Counts& c) noexcept
{
    return c.likes >= 3 && c.skips >= 3 && c.likes + c.skips >= 8;
}

float confidence (const Counts& c) noexcept
{
    return std::min (1.0f, (float) (c.likes + c.skips) / kConfidenceRatings);
}

TasteBias biasFrom (const Counts& c, float strength01) noexcept
{
    TasteBias b;
    b.ready = isReady (c);

    if (! b.ready)
        return b;

    const float beta = std::clamp (strength01, 0.0f, 1.0f) * confidence (c);

    for (int a = 0; a < kAlgoFeatures; ++a)
        b.algo[a] = std::clamp (std::exp (beta * score (c, a)), kMin, kMax);

    for (int i = 0; i < kBandFeatures; ++i)
        b.band[i] = std::clamp (std::exp (beta * score (c, kAlgoFeatures + i)), kMin, kMax);

    return b;
}

void quantise (const TasteBias& b, std::int8_t (&q)[kFeatures]) noexcept
{
    const auto one = [] (float m) { return (std::int8_t) std::clamp ((int) std::lround (64.0f * std::log (m)), -44, 44); };

    for (int a = 0; a < kAlgoFeatures; ++a)
        q[a] = one (b.algo[a]);

    for (int i = 0; i < kBandFeatures; ++i)
        q[kAlgoFeatures + i] = one (b.band[i]);
}

TasteBias dequantise (const std::int8_t (&q)[kFeatures], bool ready) noexcept
{
    TasteBias b;
    b.ready = ready;

    for (int a = 0; a < kAlgoFeatures; ++a)
        b.algo[a] = std::clamp (std::exp ((float) q[a] / 64.0f), kMin, kMax);

    for (int i = 0; i < kBandFeatures; ++i)
        b.band[i] = std::clamp (std::exp ((float) q[kAlgoFeatures + i] / 64.0f), kMin, kMax);

    return b;
}

const char* featureName (int f) noexcept
{
    if (f >= 0 && f < kAlgoFeatures)
        return algoName ((Algo) f);

    static const char* bands[3] = { "Low band", "Mid band", "High band" };
    return f >= kAlgoFeatures && f < kFeatures ? bands[f - kAlgoFeatures] : "?";
}

void explain (const Counts& c, int maxItems, std::vector<ExplainItem>& toward, std::vector<ExplainItem>& away)
{
    toward.clear();
    away.clear();

    for (int f = 0; f < kFeatures; ++f)
    {
        const int total = c.liked[(size_t) f] + c.skipped[(size_t) f];

        if (total < 3)
            continue;

        ExplainItem it { f, (int) c.liked[(size_t) f], total, score (c, f) };

        if (it.score > 0.0f)
            toward.push_back (it);
        else if (it.score < 0.0f)
            away.push_back (it);
    }

    std::sort (toward.begin(), toward.end(), [] (const auto& a, const auto& b) { return a.score > b.score; });
    std::sort (away.begin(), away.end(), [] (const auto& a, const auto& b) { return a.score < b.score; });

    if ((int) toward.size() > maxItems) toward.resize ((size_t) maxItems);
    if ((int) away.size() > maxItems) away.resize ((size_t) maxItems);
}

} // namespace gapfill::taste
