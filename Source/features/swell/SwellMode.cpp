#include "SwellMode.h"

#include <algorithm>

namespace gapfill::fx
{

SwellLanding computeSwellLanding (int target, int gapLength, double sr, const SwellTransport& tr) noexcept
{
    SwellLanding out;

    const auto minFade = std::max (4, (int) std::lround (sr * 0.006));
    const auto maxFade = std::max (minFade, (int) std::lround (sr * 0.016));
    out.fade = std::max (4, std::min ({ std::max (minFade, std::min (maxFade, (int) std::lround (sr * 0.001 * (double) kSwellFadeMs))),
                                         std::max (4, gapLength / 4) }));

    const auto gapEndLanding = std::max (1, gapLength - out.fade);
    out.landing = gapEndLanding;

    if (target > 0)
    {
        if (! tr.valid || tr.samplesPerBeat < 1.0)
        {
            out.noTransport = true;
            out.fellBackToGapEnd = true;
        }
        else
        {
            const auto barBeats = (tr.tsNum > 0 && tr.tsDen > 0) ? 4.0 * (double) tr.tsNum / (double) tr.tsDen : 4.0;
            const auto unit = target == 1 ? 1.0 : barBeats;
            const auto leadSamples = (int) std::lround (sr * 0.001 * (double) kSwellMinLeadMs);
            const auto leadBeats = (double) leadSamples / tr.samplesPerBeat;
            const auto boundary = std::ceil ((tr.ppqNow + leadBeats) / unit - 1.0e-9) * unit;
            const auto samples = (int) std::llround ((boundary - tr.ppqNow) * tr.samplesPerBeat);

            if (samples >= leadSamples && samples <= gapEndLanding)
                out.landing = samples;
            else
                out.fellBackToGapEnd = true;
        }
    }

    out.voiceLength = out.landing + out.fade;

    if (tr.valid && tr.samplesPerBeat >= 1.0)
        out.landingPpq = tr.ppqNow + (double) out.landing / tr.samplesPerBeat;

    return out;
}

float swellCurveValue (int curve, float t) noexcept
{
    t = juce::jlimit (0.0f, 1.0f, t);

    switch (curve)
    {
        case 0:  return t;
        case 2:  return t * t * (3.0f - 2.0f * t);
        case 3:  return t * t * t * t * t;
        case 1:
        default: return std::pow (t, 2.5f);
    }
}

SwellPlan planSwell (int captureLength, int landing, int endIndex) noexcept
{
    SwellPlan p;

    if (captureLength < 4 || landing < 1)
        return p;

    endIndex = juce::jlimit (0, captureLength / 2, endIndex);
    const auto usable = captureLength - endIndex;

    p.landing = landing;
    p.endIndex = endIndex;
    p.rate = juce::jlimit (1.0, 2.0, (double) usable / (double) landing);

    // Material that does not fit at x2 is dropped from the NEWEST end (the quiet
    // start of the reversed swell), so the end of the swell still lands on the hit.
    const auto span = std::min ((double) (usable - 1), (double) landing * p.rate);
    p.startPos = (double) endIndex + span;
    p.delay = std::max (0, (int) std::lround ((double) landing - span / p.rate));
    p.active = true;
    return p;
}

bool SwellMode::configure (bool swellRequested, const SwellSettings& s, int landing, int fade,
                           int captureLength, double sampleRate,
                           int& totalLength, int& fadeIn, int& fadeOut) noexcept
{
    active = false;
    filtL.reset();
    filtR.reset();

    if (! swellRequested || landing < 8 || fade < 4)
        return false;

    // The capture's first/last 2 ms are faded (FillVoice::captureSource): end just inside.
    const auto edge = std::min (captureLength / 8, (int) (sampleRate * 0.002));
    plan = planSwell (captureLength, landing, edge + 1);

    if (! plan.active)
        return false;

    curve = s.curve;
    filterAmount = juce::jlimit (0.0f, 1.0f, s.filter01);
    sr = sampleRate;

    // The voice's own fade-out IS the post-landing cosine fade.
    totalLength = landing + fade;
    fadeOut = fade;
    fadeIn = juce::jlimit (4, std::max (4, totalLength / 3), fadeIn);
    active = true;
    return true;
}

} // namespace gapfill::fx
