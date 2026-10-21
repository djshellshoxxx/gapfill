#include "ScrubVoice.h"

#include <algorithm>
#include <cmath>

namespace gapfill::fx
{

void ScrubVoice::prepare (double sampleRate, int numChannels)
{
    sr = std::max (8000.0, sampleRate);
    channels = juce::jlimit (1, 2, numChannels);
    snap.setSize (2, (int) std::ceil (sr * 0.001 * (double) kMaxWindowMs) + 8, false, true, false);
    reset();
}

void ScrubVoice::reset() noexcept
{
    state = State::Off;
    suppressed = false;
    gain = 0.0f;
    start = 0.0;
    phase = 0;
    snapLen = 0;
    maxStep = 0.0f;
    snap.clear();
}

void ScrubVoice::panic() noexcept
{
    if (state == State::On)
    {
        state = State::Off;
        suppressed = true;          // stays off until scrubOn is released
    }
}

double ScrubVoice::maxStartStep (double sampleRate, float widthMs, float windowMs) noexcept
{
    juce::ignoreUnused (windowMs);
    const auto widthSamples = (double) widthMs * 0.001 * sampleRate;
    return std::min ((double) kMaxSpeed, (double) kMaxWidthsPerSecond * widthSamples / sampleRate);
}

float ScrubVoice::readSnap (int channel, double index) const noexcept
{
    if (snapLen < 2)
        return 0.0f;

    const auto p = juce::jlimit (0.0, (double) (snapLen - 1), index);
    const auto i0 = (int) p;
    const auto i1 = std::min (snapLen - 1, i0 + 1);
    const auto frac = (float) (p - (double) i0);
    const auto* d = snap.getReadPointer (juce::jlimit (0, snap.getNumChannels() - 1, channels > 1 ? channel : 0));
    return d[i0] + (d[i1] - d[i0]) * frac;
}

void ScrubVoice::render (juce::AudioBuffer<float>& wet, int startSample, int n, const SourceHistory& history,
                         std::int64_t anchor, const ScrubSettings& s, float duck) noexcept
{
    if (n <= 0)
        return;

    if (! s.on)
    {
        suppressed = false;          // a released switch re-arms the voice after a panic

        if (state == State::On)
            state = State::Off;
    }

    const bool wanted = s.on && ! suppressed;

    if (wanted && state == State::Off && gain <= 0.0f)
    {
        // Rising edge: snapshot the material (bounded memcpy, no allocation).
        snapLen = juce::jlimit (64, snap.getNumSamples() - 8, (int) std::lround (sr * 0.001 * (double) windowMs));
        history.copyTo (snap, anchor - (std::int64_t) snapLen, snapLen);
        phase = 0;
        start = 0.0;
        state = State::On;
        gain = 0.0f;

        const auto w0 = std::min (snapLen - 1, (int) std::lround (sr * 0.001 * (double) s.widthMs));
        start = (double) s.pos01 * (double) std::max (0, snapLen - w0);
    }
    else if (wanted && state == State::Off)
    {
        state = State::On;           // switched on again while still fading out
    }

    if (state == State::Off && gain <= 0.0f)
        return;

    const auto width = std::max (16, std::min (snapLen - 1, (int) std::lround (sr * 0.001 * (double) s.widthMs)));
    const auto xfade = std::max (1, std::min (width / 2, (int) std::lround (sr * 0.001 * (double) kFadeMs)));
    const auto target = (double) juce::jlimit (0.0f, 1.0f, s.pos01) * (double) std::max (0, snapLen - width);
    const auto coeff = (double) onePoleCoeff (sr, kSmoothMs);
    const auto stepLimit = maxStartStep (sr, s.widthMs, windowMs);
    const auto fadeStep = 1.0f / (float) std::max (1, (int) std::lround (sr * 0.001 * (double) kFadeMs));
    const bool wantGain = state == State::On;

    auto* outL = wet.getWritePointer (0, startSample);
    auto* outR = wet.getNumChannels() > 1 ? wet.getWritePointer (1, startSample) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        // Smoothed, speed-limited window position.
        const auto delta = juce::jlimit (-stepLimit, stepLimit, (target - start) * coeff);
        start += delta;
        maxStep = std::max (maxStep, (float) std::abs (delta));

        gain = wantGain ? std::min (1.0f, gain + fadeStep) : std::max (0.0f, gain - fadeStep);

        // Loop with an equal-power crossfade across the wrap.
        double idx = start + (double) phase;
        auto l = readSnap (0, idx);
        auto r = readSnap (1, idx);

        if (phase >= width - xfade)
        {
            const auto t = (float) (phase - (width - xfade)) / (float) xfade;
            float gA, gB;
            equalPower (t, gA, gB);
            idx -= (double) width;                       // the material just BEFORE the window start
            l = l * gA + readSnap (0, idx) * gB;
            r = r * gA + readSnap (1, idx) * gB;
        }

        if (++phase >= width)
            phase = 0;

        const auto g = kAmp * gain * duck;
        outL[i] += sanitise (l * g);

        if (outR != nullptr)
            outR[i] += sanitise (r * g);

        if (! wantGain && gain <= 0.0f)
            break;
    }
}

} // namespace gapfill::fx
