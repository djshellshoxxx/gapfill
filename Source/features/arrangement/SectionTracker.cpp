#include "SectionTracker.h"

#include <algorithm>
#include <cmath>

namespace gapfill::arrangement
{

namespace
{
    constexpr float kAbsoluteNoveltyFloor = 0.06f;       // fraction of sum|K|: a boundary must be a real step in the features
    constexpr int   kNoveltyWindow = 64;
    constexpr float kMinSpan[kDims] = { 0.10f, 0.15f, 0.10f, 0.10f, 0.10f, 0.10f };

    int slotOf (int frame) noexcept { return ((frame % kFrames) + kFrames) % kFrames; }
}

void SectionTracker::reset() noexcept
{
    for (auto& r : ssm) r.fill (0.0f);
    for (auto& f : feat) f.fill (0.0f);
    for (auto& f : rawFeat) f.fill (0.0f);
    raw.fill (0.0f);
    noveltySeries.fill (0.0f);
    rawMin.fill (0.0f);
    rawMax.fill (1.0f);
    framesObserved = 0;
    lastBoundary = -1;
    boundaryCount = 0;
    boundaryList.fill (-1);
    sectionStartFrame = 0;
    sectionSum = 0.0;
    sectionFrames = 0;
    sectionMeans.fill (0.0f);
    sectionLengths.fill (0);
    completed = 0;
    st = {};
}

float SectionTracker::kernel (int a, int b) noexcept
{
    const float ax = (float) a - 0.5f, bx = (float) b - 0.5f;
    const float sign = (ax > 0.0f ? 1.0f : -1.0f) * (bx > 0.0f ? 1.0f : -1.0f);
    const float sigma = 0.5f * (float) kKernelHalf;
    return sign * std::exp (-(ax * ax + bx * bx) / (2.0f * sigma * sigma));
}

float SectionTracker::kernelSum() noexcept
{
    float sum = 0.0f;

    for (int a = -kKernelHalf + 1; a <= kKernelHalf; ++a)
        for (int b = -kKernelHalf + 1; b <= kKernelHalf; ++b)
            sum += std::abs (kernel (a, b));

    return sum;
}

float SectionTracker::similarity (int a, int b) const noexcept
{
    const int newest = framesObserved - 1;

    if (a < 0 || b < 0 || a > newest || b > newest || newest - a >= kFrames || newest - b >= kFrames)
        return 0.0f;

    return ssm[(size_t) slotOf (a)][(size_t) slotOf (b)];
}

float SectionTracker::noveltyAt (int frame) const noexcept
{
    const int newest = framesObserved - 1 - kKernelHalf;

    if (frame < kKernelHalf || frame > newest || newest - frame >= kFrames)
        return 0.0f;

    return noveltySeries[(size_t) slotOf (frame)];
}

float SectionTracker::novelty (int t) const noexcept
{
    float n = 0.0f;

    for (int a = -kKernelHalf + 1; a <= kKernelHalf; ++a)
        for (int b = -kKernelHalf + 1; b <= kKernelHalf; ++b)
            n += kernel (a, b) * similarity (t + a, t + b);

    return n;
}

void SectionTracker::normalised (const context::FrameFeatures& f, float (&out)[kDims]) noexcept
{
    const float rf[kDims] = { std::clamp ((f.rmsDb + 60.0f) / 60.0f, 0.0f, 1.0f),
                              std::clamp (f.onsetsPerSec / 12.0f, 0.0f, 1.0f),
                              f.bandShare[0], f.bandShare[1], f.bandShare[2],
                              std::clamp (f.brightness, 0.0f, 1.5f) / 1.5f };

    const int k = framesObserved;
    const int slot = slotOf (k);
    rawFeat[(size_t) slot] = { rf[0], rf[1], rf[2], rf[3], rf[4], rf[5] };

    const int valid = std::min (k + 1, kFrames);

    for (int d = 0; d < kDims; ++d)
    {
        float lo = rf[d], hi = rf[d];

        for (int i = 0; i < valid; ++i)
        {
            const float v = rawFeat[(size_t) slotOf (k - i)][(size_t) d];
            lo = std::min (lo, v);
            hi = std::max (hi, v);
        }

        const float span = std::max (hi - lo, kMinSpan[d]);
        const float mid = 0.5f * (lo + hi);
        // A flat history maps to the middle of the scale, so noise cannot be stretched to the full range.
        out[d] = std::clamp ((rf[d] - (mid - 0.5f * span)) / span, 0.0f, 1.0f);
    }

    raw[(size_t) slot] = 0.5f * (rf[0] + rf[1]);
}

void SectionTracker::pushFrame (const context::FrameFeatures& f, float sensitivity01) noexcept
{
    const int k = framesObserved;
    const int slot = slotOf (k);

    float fn[kDims];
    normalised (f, fn);

    for (int d = 0; d < kDims; ++d)
        feat[(size_t) slot][(size_t) d] = fn[d];

    // ---- one new row and column of the self-similarity matrix --------------------------------
    const int valid = std::min (k + 1, kFrames);

    for (int i = 0; i < valid; ++i)
    {
        const int other = slotOf (k - i);
        float dist = 0.0f;

        for (int d = 0; d < kDims; ++d)
        {
            const float diff = fn[d] - feat[(size_t) other][(size_t) d];
            dist += diff * diff;
        }

        const float s = std::clamp (1.0f - std::sqrt (dist) / std::sqrt ((float) kDims), 0.0f, 1.0f);
        ssm[(size_t) slot][(size_t) other] = s;
        ssm[(size_t) other][(size_t) slot] = s;
    }

    ++framesObserved;

    // ---- novelty, delayed by the kernel half-width ------------------------------------------------
    const int tn = k - kKernelHalf;

    if (tn >= kKernelHalf)
        noveltySeries[(size_t) slotOf (tn)] = novelty (tn);

    // ---- peak picking on the 3-frame moving average, two frames behind the newest one ---------
    bool boundary = false;
    int boundaryAt = -1;
    const int sNewest = tn - 1;

    auto smoothed = [&] (int i)
    {
        return (noveltyAt (i - 1) + noveltyAt (i) + noveltyAt (i + 1)) / 3.0f;
    };

    const int p = sNewest - 2;

    if (p >= kKernelHalf + 1 && p > lastBoundary)
    {
        const float sp = smoothed (p);
        bool localMax = true;

        for (int o = -2; o <= 2 && localMax; ++o)
            if (o != 0)
                localMax = o < 0 ? sp > smoothed (p + o) : sp >= smoothed (p + o);

        if (localMax)
        {
            // Threshold from the last 64 smoothed values.
            double sum = 0.0, sq = 0.0;
            int count = 0;

            for (int i = sNewest; i > sNewest - kNoveltyWindow && i >= kKernelHalf + 1; --i)
            {
                const float v = smoothed (i);
                sum += v;
                sq += (double) v * v;
                ++count;
            }

            const double mean = count > 0 ? sum / count : 0.0;
            const double sd = count > 1 ? std::sqrt (std::max (0.0, sq / count - mean * mean)) : 0.0;
            const double kfac = 1.5 - std::clamp (sensitivity01, 0.0f, 1.0f);

            if (sp > mean + kfac * sd && sp / kernelSum() > kAbsoluteNoveltyFloor
                 && (lastBoundary < 0 || p - lastBoundary >= kRefractoryFrames))
            {
                boundary = true;
                boundaryAt = p;
            }
        }
    }

    updateSections (boundaryAt, boundary);

    if (boundary)
    {
        lastBoundary = boundaryAt;

        if (boundaryCount < (int) boundaryList.size())
            boundaryList[(size_t) boundaryCount] = boundaryAt;

        ++boundaryCount;
    }

    // ---- confidence ------------------------------------------------------------------------------
    float maxN = 0.0f;

    for (int i = sNewest; i > sNewest - kNoveltyWindow && i >= kKernelHalf + 1; --i)
        maxN = std::max (maxN, smoothed (i));

    const float contrast = std::clamp ((maxN / kernelSum() - 0.04f) / 0.12f, 0.0f, 1.0f);
    st.confidence = std::clamp ((float) (framesObserved - kLearningFrames) / 48.0f, 0.0f, 1.0f) * contrast;
    st.learning = framesObserved < kLearningFrames;
}

void SectionTracker::updateSections (int boundaryAt, bool boundary) noexcept
{
    const int newest = framesObserved - 1;

    auto meanRaw = [&] (int from, int toExclusive)
    {
        double sum = 0.0;
        int n = 0;

        for (int i = std::max (from, newest - kFrames + 1); i < toExclusive; ++i, ++n)
            sum += raw[(size_t) slotOf (i)];

        return n > 0 ? (float) (sum / n) : 0.0f;
    };

    if (boundary)
    {
        const int len = boundaryAt - sectionStartFrame;

        if (len > 0)
        {
            const int idx = completed % kMaxSections;
            sectionMeans[(size_t) idx] = meanRaw (sectionStartFrame, boundaryAt);
            sectionLengths[(size_t) idx] = len;
            ++completed;
        }

        sectionStartFrame = boundaryAt;
        ++st.sectionIndex;
    }

    st.framesInto = (std::uint16_t) std::clamp (newest + 1 - sectionStartFrame, 0, 65535);

    // Energy of the current section relative to the sections heard so far.
    const float cur = meanRaw (sectionStartFrame, newest + 1);
    const int known = std::min (completed, kMaxSections);
    st.level01 = std::clamp (cur, 0.0f, 1.0f);

    if (known >= 1)
    {
        // Position of this section between the quietest and the busiest section heard so far
        // (a flat history, span < 0.05, is neutral): the quietest section gets exactly 0.
        float lo = cur, hi = cur;

        for (int i = 0; i < known; ++i)
        {
            lo = std::min (lo, sectionMeans[(size_t) i]);
            hi = std::max (hi, sectionMeans[(size_t) i]);
        }

        st.energy01 = hi - lo < 0.05f ? 0.5f : std::clamp ((cur - lo) / (hi - lo), 0.0f, 1.0f);
    }
    else
        st.energy01 = std::clamp (cur, 0.0f, 1.0f);              // absolute fallback until a second section exists

    // Loudness trend over the last 16 frames (least squares), scaled so +-0.25 per 16 frames = +-1.
    const int n = std::min (16, framesObserved);

    if (n >= 4)
    {
        double sx = 0, sy = 0, sxy = 0, sxx = 0;

        for (int i = 0; i < n; ++i)
        {
            const double x = i, y = rawFeat[(size_t) slotOf (newest - n + 1 + i)][0];
            sx += x; sy += y; sxy += x * y; sxx += x * x;
        }

        const double slope = (n * sxy - sx * sy) / std::max (1.0e-9, n * sxx - sx * sx);
        st.trend = (float) std::clamp (slope * 16.0 / 0.25, -1.0, 1.0);
    }
    else
        st.trend = 0.0f;

    // Prediction: the last two completed sections agree in length.
    st.expectedIn = -1;

    if (completed >= 2)
    {
        const int a = sectionLengths[(size_t) ((completed - 1) % kMaxSections)];
        const int b = sectionLengths[(size_t) ((completed - 2) % kMaxSections)];

        if (std::abs (a - b) <= 1)
            st.expectedIn = (std::int16_t) std::clamp (a - (int) st.framesInto, 0, 32000);
    }
}

} // namespace gapfill::arrangement
