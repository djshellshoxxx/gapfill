#include "PitchTracker.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace gapfill::harmonic::psola
{
float PitchTracker::Biquad::process (float x) noexcept
{
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}

void PitchTracker::prepare (double sampleRate) noexcept
{
    rate = std::isfinite (sampleRate) && sampleRate >= 8000 && sampleRate <= 768000 ? sampleRate : 48000;
    const int exponent = std::clamp ((int) std::lround (std::log2 (rate / 12000)), 0, 6);
    decim = 1 << exponent;
    const double fs = analysisRate();
    tauMin = std::max (2, (int) std::floor (fs / 1000));
    tauMax = std::min (kMaxPitchLag, (int) std::ceil (fs / 50));
    for (int i = 0; i < 2; ++i)
    {
        const double omega = 6.283185307179586 * 1000 / rate;
        const double q = i == 0 ? 0.541196100146197 : 1.30656296487638;
        const double c = std::cos (omega), alpha = std::sin (omega) / (2 * q), a0 = 1 + alpha;
        auto& b = lowpass[(std::size_t) i];
        b.b0 = (float) ((1 - c) * 0.5 / a0); b.b1 = (float) ((1 - c) / a0); b.b2 = b.b0;
        b.a1 = (float) (-2 * c / a0); b.a2 = (float) ((1 - alpha) / a0);
    }
    active = false;
    reset();
}

void PitchTracker::reset() noexcept
{
    for (auto& filter : lowpass) filter.z1 = filter.z2 = 0;
    ring.fill (0); work.fill (0); difference.fill (0); normalised.fill (1); frames.fill ({});
    working = havePosition = false;
    expectedPosition = newestPosition = snapshotPosition = 0;
    inputHop = decimCount = ringWrite = filled = frameWrite = stored = maximumMacs = maximumLags = 0;
    nextLag = 1;
    total = 0;
}

void PitchTracker::setActive (bool value) noexcept
{
    if (active != value) { active = value; reset(); }
}

void PitchTracker::push (const float* mono, int count, std::int64_t first) noexcept
{
    if (! active || mono == nullptr || count <= 0) return;
    if (first > std::numeric_limits<std::int64_t>::max() - count) { reset(); return; }
    if (havePosition && expectedPosition != first) reset();
    havePosition = true;
    for (int i = 0; i < count; ++i)
    {
        const auto position = first + i;
        if (! std::isfinite (mono[i])) { reset(); return; }
        const float filtered = lowpass[1].process (lowpass[0].process (mono[i]));
        if (++decimCount == decim)
        {
            decimCount = 0;
            ring[(std::size_t) ringWrite] = filtered;
            ringWrite = (ringWrite + 1) % kDecimatedRing;
            filled = std::min (filled + 1, kDecimatedRing);
            newestPosition = position;
        }
        if (++inputHop == kInputHop)
        {
            inputHop = 0;
            if (! working && filled >= kDifferenceWindow + tauMax) beginFrame();
            if (working) computeHop();
        }
    }
    expectedPosition = first + count;
}

void PitchTracker::beginFrame() noexcept
{
    const int count = kDifferenceWindow + tauMax;
    for (int i = 0; i < count; ++i)
        work[(std::size_t) i] = ring[(std::size_t) ((ringWrite + kDecimatedRing - count + i) % kDecimatedRing)];
    snapshotPosition = newestPosition;
    nextLag = 1;
    working = true;
}

void PitchTracker::computeHop() noexcept
{
    const int lanes = std::min (kLagsPerHop, tauMax - nextLag + 1);
    std::array<float, kLagsPerHop> sums {};
    for (int j = 0; j < kDifferenceWindow; ++j)
    {
        const float base = work[(std::size_t) j];
        // Independent lag lanes permit SIMD without reassociating a sum.
        for (int lane = 0; lane < lanes; ++lane)
        {
            const float delta = base - work[(std::size_t) (j + nextLag + lane)];
            sums[(std::size_t) lane] += delta * delta;
        }
    }
    for (int i = 0; i < lanes; ++i) difference[(std::size_t) (nextLag + i)] = sums[(std::size_t) i];
    maximumLags = std::max (maximumLags, lanes);
    maximumMacs = std::max (maximumMacs, lanes * kDifferenceWindow);
    nextLag += lanes;
    if (nextLag > tauMax) finishFrame();
}

void PitchTracker::finishFrame() noexcept
{
    double running = 0;
    for (int lag = 1; lag <= tauMax; ++lag)
    {
        const double d = difference[(std::size_t) lag];
        running += d;
        normalised[(std::size_t) lag] = running > 1e-12 ? (float) (d * lag / running) : 1;
    }
    int best = -1;
    for (int lag = tauMin; lag <= tauMax; ++lag)
        if (normalised[(std::size_t) lag] < 0.15f)
        {
            while (lag < tauMax && normalised[(std::size_t) (lag + 1)] < normalised[(std::size_t) lag]) ++lag;
            best = lag; break;
        }
    if (best < 0)
        for (int lag = tauMin; lag <= tauMax; ++lag)
            if (best < 0 || normalised[(std::size_t) lag] < normalised[(std::size_t) best]) best = lag;
    PitchFrame result;
    result.histPos = snapshotPosition;
    if (best >= 0 && std::isfinite (normalised[(std::size_t) best]))
    {
        double exact = best;
        if (best > 1 && best < tauMax)
        {
            const double a = normalised[(std::size_t) (best - 1)], b = normalised[(std::size_t) best], c = normalised[(std::size_t) (best + 1)];
            const double denominator = a - 2 * b + c;
            if (std::abs (denominator) > 1e-12) exact += std::clamp (0.5 * (a - c) / denominator, -1.0, 1.0);
        }
        result.clarity = std::clamp (1 - normalised[(std::size_t) best], 0.0f, 1.0f);
        const float hz = (float) (analysisRate() / exact);
        // Energy/stationarity rejects silence and clean but decaying drum hits.
        double early = 0, late = 0;
        constexpr int third = kDifferenceWindow / 3;
        for (int i = 0; i < third; ++i)
        {
            early += (double) work[(std::size_t) i] * work[(std::size_t) i];
            const auto index = (std::size_t) (kDifferenceWindow - 1 - i);
            late += (double) work[index] * work[index];
        }
        // Parabolic YIN has a small bin/interpolation bias at the supported
        // endpoints. Accept only the20-cent accuracy margin, then publish a
        // pitch inside the promised50..1000Hz range.
        constexpr float endpointRatio = 1.01161944f; // 2^(20/1200)
        if (result.clarity >= 0.8f && hz >= 50 / endpointRatio && hz <= 1000 * endpointRatio && early + late > 1e-8
            && std::max (early, late) <= 2 * std::min (early, late))
            result.f0Hz = std::clamp (hz, 50.0f, 1000.0f);
    }
    frames[(std::size_t) frameWrite] = result;
    frameWrite = (frameWrite + 1) % kPitchFrames;
    stored = std::min (stored + 1, kPitchFrames);
    ++total;
    working = false;
}

const PitchFrame& PitchTracker::frame (int index) const noexcept
{
    index = std::clamp (index, 0, std::max (0, stored - 1));
    return frames[(std::size_t) ((frameWrite + kPitchFrames - stored + index) % kPitchFrames)];
}

PitchTracker::Query PitchTracker::query (std::int64_t from, std::int64_t to) const noexcept
{
    Query result;
    if (! active || from > to) return result;
    std::array<float, kPitchFrames> pitches {};
    int present = 0, voiced = 0;
    double clarity = 0;
    for (int i = 0; i < stored; ++i)
    {
        const auto& value = frame (i);
        if (value.histPos < from || value.histPos > to) continue;
        ++present;
        if (value.f0Hz >= 50 && value.f0Hz <= 1000 && value.clarity >= 0.8f)
        {
            pitches[(std::size_t) voiced++] = value.f0Hz;
            clarity += value.clarity;
        }
    }
    result.voicedFraction = present > 0 ? (float) voiced / (float) present : 0;
    if (voiced > 0)
    {
        std::sort (pitches.begin(), pitches.begin() + voiced);
        result.f0Hz = voiced % 2 != 0 ? pitches[(std::size_t) (voiced / 2)]
            : 0.5f * (pitches[(std::size_t) (voiced / 2 - 1)] + pitches[(std::size_t) (voiced / 2)]);
        result.clarity = (float) (clarity / voiced);
        result.voiced = result.voicedFraction >= 0.6f;
    }
    return result;
}
} // namespace gapfill::harmonic::psola
