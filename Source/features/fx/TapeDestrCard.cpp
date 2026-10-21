#include "TapeDestrCard.h"

#include "../Salts.h"

#include <algorithm>
#include <cmath>

namespace gapfill::fx
{

namespace
{
    constexpr double kTwoPi = 6.283185307179586;

    inline float hermite (float y0, float y1, float y2, float y3, float t) noexcept
    {
        const auto c0 = y1;
        const auto c1 = 0.5f * (y2 - y0);
        const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }
}

void TapeDestrCard::prepare (double sampleRate, int /*maxBlock*/, int /*channels*/)
{
    sr = sampleRate;
    lineSize = (int) std::ceil (sr * 0.001 * (double) kLineMs) + 8;
    lineL.assign ((size_t) lineSize, 0.0f);
    lineR.assign ((size_t) lineSize, 0.0f);
    dcR = 1.0f - (float) (kTwoPi * 10.0 / sr);
    reset();
}

void TapeDestrCard::reset()
{
    active = false;
    std::fill (lineL.begin(), lineL.end(), 0.0f);
    std::fill (lineR.begin(), lineR.end(), 0.0f);
    writeIndex = 0;
    dcXl = dcYl = dcXr = dcYr = 0.0f;
    dropActive = false;
    minDelay = 1.0e9f;
    maxDelay = 0.0f;
    dropoutCount = spliceCount = 0;
}

double TapeDestrCard::nextInterval (float ratePerSecond) noexcept
{
    if (ratePerSecond <= 1.0e-4f)
        return 1.0e18;

    const auto u = (double) rng.nextFloat();
    return -std::log (1.0 - u) / (double) ratePerSecond * sr;
}

bool TapeDestrCard::startFill (const VoiceFxStart& st)
{
    active = false;
    s = st.settings.tape;

    if (s.amount01 <= kOffThreshold)
        return false;

    reset();
    depth = juce::jlimit (0.0f, 1.0f, s.amount01);

    rng.seed (hashCombine (st.seed, kSalt_TapeDestr));

    wowInc = kTwoPi * (double) rng.range (0.4f, 2.0f) / sr;
    flutInc = kTwoPi * (double) rng.range (6.0f, 14.0f) / sr;
    wowPhase = (double) rng.nextFloat() * kTwoPi;
    flutPhase = (double) rng.nextFloat() * kTwoPi;
    noiseTarget = noiseValue = rng.nextBipolar();
    noiseCountdown = (int) (sr * 0.25);

    wowDepthSamples = (float) (sr * 0.001) * kWowDepthMs * s.wow01 * depth;
    flutDepthSamples = (float) (sr * 0.001) * kFlutterDepthMs * s.flutter01 * depth;

    const auto sat = juce::jlimit (0.0f, 1.0f, s.sat01 * depth);
    saturate = sat > 0.001f;
    drive = 1.0f + 7.0f * sat;
    satNorm = 1.0f / std::tanh (drive);

    dropRate = kMaxDropoutsPerSecond * s.dropout01 * depth;
    spliceRate = kMaxSplicesPerSecond * s.splice01 * depth;
    nextDropoutIn = nextInterval (dropRate);
    nextSpliceIn = nextInterval (spliceRate);

    dropActive = false;
    offsetFrom = offsetTo = 0.0;
    spliceFade = spliceFadeLen = 1;
    spliceSide = false;

    active = true;
    return true;
}

float TapeDestrCard::readLine (const std::vector<float>& line, double d) const noexcept
{
    const auto maxD = (double) (lineSize - 5);
    const auto delay = std::min (std::max (d, 2.0), maxD);
    const auto pos = (double) writeIndex - delay;
    const auto base = (int) std::floor (pos);
    const auto frac = (float) (pos - (double) base);

    auto idx = [this] (int i) { return ((i % lineSize) + lineSize) % lineSize; };

    return hermite (line[(size_t) idx (base - 1)], line[(size_t) idx (base)],
                    line[(size_t) idx (base + 1)], line[(size_t) idx (base + 2)], frac);
}

void TapeDestrCard::startDropout() noexcept
{
    dropLen = std::max (8, (int) (sr * 0.001 * (double) rng.range (5.0f, 80.0f)));
    dropEdge = std::max (1, std::min (dropLen / 2, (int) (sr * 0.001 * (double) rng.range (2.0f, 8.0f))));
    const auto db = -40.0f * rng.range (0.5f, 1.0f);
    dropMinGain = dbToGain (db);
    dropPos = 0;
    dropActive = true;
    ++dropoutCount;
}

void TapeDestrCard::startSplice() noexcept
{
    const auto jump = (double) rng.range (5.0f, 60.0f) * 0.001 * sr;
    offsetFrom = offsetTo;
    offsetTo = spliceSide ? 0.0 : jump;
    spliceSide = ! spliceSide;
    spliceFadeLen = std::max (1, (int) (sr * 0.002));
    spliceFade = 0;
    ++spliceCount;
}

void TapeDestrCard::processVoice (float* l, float* r, int n, const VoiceFxContext&) noexcept
{
    noteCall();

    if (! active)
        return;

    const auto base = sr * 0.001 * (double) kBaseDelayMs;

    for (int i = 0; i < n; ++i)
    {
        // --- write -----------------------------------------------------------
        const auto inL = l[i];
        const auto inR = r[i];
        lineL[(size_t) writeIndex] = inL;
        lineR[(size_t) writeIndex] = inR;

        // --- modulation ------------------------------------------------------
        if (--noiseCountdown <= 0)
        {
            noiseCountdown = (int) (sr * 0.25);
            noiseTarget = rng.nextBipolar();
        }

        noiseValue += 0.0008f * (noiseTarget - noiseValue);

        const auto wow = (0.75f * (float) std::sin (wowPhase) + 0.25f * noiseValue) * wowDepthSamples;
        const auto flut = (float) std::sin (flutPhase) * flutDepthSamples;
        wowPhase += wowInc;
        flutPhase += flutInc;

        if (wowPhase > kTwoPi) wowPhase -= kTwoPi;
        if (flutPhase > kTwoPi) flutPhase -= kTwoPi;

        // --- events ----------------------------------------------------------
        if (--nextSpliceIn <= 0.0)
        {
            startSplice();
            nextSpliceIn = nextInterval (spliceRate) + (double) spliceFadeLen;
        }

        if (! dropActive && --nextDropoutIn <= 0.0)
        {
            startDropout();
            nextDropoutIn = nextInterval (dropRate) + (double) dropLen;
        }

        // --- read ------------------------------------------------------------
        const auto delayNow = base + (double) wow + (double) flut;
        float outL, outR;

        if (spliceFade < spliceFadeLen)
        {
            const auto t = (float) spliceFade / (float) spliceFadeLen;
            float ga, gb;
            equalPower (t, ga, gb);
            const auto dA = delayNow + offsetFrom, dB = delayNow + offsetTo;
            outL = readLine (lineL, dA) * ga + readLine (lineL, dB) * gb;
            outR = readLine (lineR, dA) * ga + readLine (lineR, dB) * gb;
            ++spliceFade;
            minDelay = std::min (minDelay, (float) std::min (dA, dB));
            maxDelay = std::max (maxDelay, (float) std::max (dA, dB));
        }
        else
        {
            const auto d = delayNow + offsetTo;
            outL = readLine (lineL, d);
            outR = readLine (lineR, d);
            minDelay = std::min (minDelay, (float) d);
            maxDelay = std::max (maxDelay, (float) d);
        }

        writeIndex = (writeIndex + 1) % lineSize;

        // --- saturation (level matched at full scale) ------------------------
        if (saturate)
        {
            outL = std::tanh (drive * outL) * satNorm;
            outR = std::tanh (drive * outR) * satNorm;
        }

        // --- dropout ---------------------------------------------------------
        if (dropActive)
        {
            float w = 1.0f;

            if (dropPos < dropEdge)
                w = hann (0.5f * (float) dropPos / (float) dropEdge);
            else if (dropPos >= dropLen - dropEdge)
                w = hann (0.5f * (float) (dropLen - 1 - dropPos) / (float) dropEdge);

            const auto g = 1.0f - (1.0f - dropMinGain) * w;
            outL *= g;
            outR *= g;

            if (++dropPos >= dropLen)
                dropActive = false;
        }

        // --- DC blocker ------------------------------------------------------
        const auto yl = outL - dcXl + dcR * dcYl;
        const auto yr = outR - dcXr + dcR * dcYr;
        dcXl = outL;
        dcXr = outR;
        dcYl = std::isfinite (yl) ? yl : 0.0f;
        dcYr = std::isfinite (yr) ? yr : 0.0f;

        l[i] = inL * (1.0f - depth) + dcYl * depth;
        r[i] = inR * (1.0f - depth) + dcYr * depth;
    }
}

} // namespace gapfill::fx
