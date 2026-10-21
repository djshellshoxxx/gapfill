#include "EnergyTracker.h"

#include <algorithm>
#include <cmath>

namespace gapfill::context
{

namespace
{
    constexpr float kLowThr = 0.33f, kHighThr = 0.66f, kHyst = 0.05f;
    constexpr float kSilenceDb = -55.0f;
    constexpr int kMinDwellFrames = 8;          // 4 s
    constexpr int kConfirmFrames = 2;
    constexpr double kOnsetFloor = 1.0e-7;      // about -70 dBFS^2
}

void EnergyTracker::prepare (double sampleRate) noexcept
{
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    frameLen = std::max (1, (int) std::lround (0.5 * sr));
    hopLen = std::max (1, (int) std::lround (0.01 * sr));

    const double twoPi = 6.283185307179586;
    aLow = (float) (1.0 - std::exp (-twoPi * 180.0 / sr));
    aHigh = (float) (1.0 - std::exp (-twoPi * std::min (3500.0, sr * 0.45) / sr));
    reset();
}

void EnergyTracker::reset() noexcept
{
    lpLow = lpHigh = 0.0f;
    posInFrame = posInHop = 0;
    frameSq = 0.0;

    for (int b = 0; b < 3; ++b)
        hopSq[b] = frameBand[b] = prevHop[b] = 0.0;

    onsets = 0;
    last = {};
    dbRing.fill (0.0f);
    onsetRing.fill (0.0f);
    onsetRecent.fill (0.0f);
    ringCount = ringWrite = recentWrite = 0;
    framesSeen = 0;
    smoothed = 0.5f;
    currentLevel = EnergyLevel::Mid;
    pendingDir = -1; pendingCount = 0;
    dwellFrames = 0;
    changes = 0;
}

float EnergyTracker::normalise (float x, float lo, float hi, float minSpan) noexcept
{
    auto span = hi - lo;

    if (span < minSpan)
    {
        const auto mid = 0.5f * (lo + hi);
        lo = mid - 0.5f * minSpan;
        span = minSpan;
    }

    const auto v = (x - lo) / span;
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

void EnergyTracker::process (const float* mono, int numSamples) noexcept
{
    if (mono == nullptr)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = mono[i];
        lpLow += aLow * (x - lpLow);
        lpHigh += aHigh * (x - lpHigh);

        const double low = lpLow, mid = lpHigh - lpLow, high = x - lpHigh;
        hopSq[0] += low * low;
        hopSq[1] += mid * mid;
        hopSq[2] += high * high;
        frameSq += (double) x * (double) x;

        if (++posInHop >= hopLen)
            endHop();

        if (++posInFrame >= frameLen)
            endFrame();
    }
}

void EnergyTracker::endHop() noexcept
{
    for (int b = 0; b < 3; ++b)
    {
        const auto e = hopSq[b] / (double) hopLen;

        if (e > kOnsetFloor && e > 2.0 * prevHop[b])          // > 3 dB rise over the previous hop
            ++onsets;

        prevHop[b] = e;
        frameBand[b] += hopSq[b];
        hopSq[b] = 0.0;
    }

    posInHop = 0;
}

void EnergyTracker::percentiles (const float* ring, float& lo, float& hi) const noexcept
{
    const int n = std::min (ringCount, kHistoryFrames);
    std::array<float, kHistoryFrames> tmp {};
    std::copy (ring, ring + n, tmp.begin());
    std::sort (tmp.begin(), tmp.begin() + n);
    lo = tmp[(size_t) (0.05f * (float) (n - 1))];
    hi = tmp[(size_t) (0.95f * (float) (n - 1) + 0.5f)];
}

void EnergyTracker::endFrame() noexcept
{
    const auto frameSeconds = (double) frameLen / sr;
    const auto rms = std::sqrt (frameSq / (double) frameLen);
    last.rmsDb = std::clamp ((float) (20.0 * std::log10 (rms + 1.0e-9)), -60.0f, 0.0f);
    last.onsetsPerSec = std::min (kMaxOnsetsPerSec, (float) ((double) onsets / frameSeconds));

    const auto total = frameBand[0] + frameBand[1] + frameBand[2];

    for (int b = 0; b < 3; ++b)
        last.bandShare[b] = total > 1.0e-12 ? (float) (frameBand[b] / total) : 0.0f;

    last.brightness = last.bandShare[2] + 0.5f * last.bandShare[1];

    frameSq = 0.0;
    onsets = 0;
    posInFrame = 0;

    for (int b = 0; b < 3; ++b)
        frameBand[b] = 0.0;

    dbRing[(size_t) ringWrite] = last.rmsDb;
    onsetRing[(size_t) ringWrite] = last.onsetsPerSec;
    ringWrite = (ringWrite + 1) % kHistoryFrames;
    ringCount = std::min (ringCount + 1, kHistoryFrames);
    onsetRecent[(size_t) recentWrite] = last.onsetsPerSec;
    recentWrite = (recentWrite + 1) % (int) onsetRecent.size();
    ++framesSeen;

    if (framesSeen <= kWarmupFrames)
        return;                                             // neutral while the range is learned

    float dbLo, dbHi, onLo, onHi;
    percentiles (dbRing.data(), dbLo, dbHi);
    percentiles (onsetRing.data(), onLo, onHi);

    const int recentN = std::min (framesSeen, (int) onsetRecent.size());
    float onsetMean = 0.0f;

    for (int i = 0; i < recentN; ++i)
        onsetMean += onsetRecent[(size_t) i];

    onsetMean /= (float) recentN;

    // Silence is Low by definition (an absolute floor, so the relative range cannot lift it).
    const auto raw = last.rmsDb < kSilenceDb ? 0.0f
                   : 0.6f * normalise (onsetMean, onLo, onHi, kMinSpanOnsets)
                   + 0.4f * normalise (last.rmsDb, dbLo, dbHi, kMinSpanDb);

    const auto k = raw > smoothed ? 1.0f - (float) std::exp (-frameSeconds / 1.0)       // attack 1 s
                                  : 1.0f - (float) std::exp (-frameSeconds / 3.0);      // release 3 s
    smoothed += k * (raw - smoothed);

    // ---- class with hysteresis, two-frame confirmation and minimum dwell ----------
    dwellFrames = std::min (dwellFrames + 1, 1000);

    int desired = (int) currentLevel;
    const auto up = [this] (float thr) { return smoothed > thr + kHyst; };
    const auto down = [this] (float thr) { return smoothed < thr - kHyst; };

    switch (currentLevel)
    {
        case EnergyLevel::Low:  desired = up (kHighThr) ? 2 : (up (kLowThr) ? 1 : 0); break;
        case EnergyLevel::Mid:  desired = up (kHighThr) ? 2 : (down (kLowThr) ? 0 : 1); break;
        case EnergyLevel::High: desired = down (kLowThr) ? 0 : (down (kHighThr) ? 1 : 2); break;
    }

    if (desired == (int) currentLevel)
    {
        pendingDir = -1; pendingCount = 0;
        return;
    }

    if (desired == pendingDir)
        ++pendingCount;
    else
    {
        pendingDir = desired;
        pendingCount = 1;
    }

    if (pendingCount >= kConfirmFrames && dwellFrames >= kMinDwellFrames)
    {
        currentLevel = (EnergyLevel) desired;
        dwellFrames = 0;
        pendingDir = -1; pendingCount = 0;
        ++changes;
    }
}

} // namespace gapfill::context
