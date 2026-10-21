#pragma once

// N-FX1 Reverse Swell (docs/dev/12 section 4): a render mode of Algo::Reverse
// voices (recipe flag bit3). Material is read backwards so it ends at the hit
// that opened the gap, crescendos along a curve while a low-pass opens, and
// peaks exactly at the landing time T; nothing is emitted after T + fade.
//
// Pure planning (landing time, rate, delay, curve) lives in free functions so
// it can be unit-tested without a voice; SwellMode is the per-voice renderer.

#include "../../dsp/DspUtils.h"
#include "../fx/FxSettings.h"

#include <cmath>

namespace gapfill::fx
{

/** Where the swell peaks, in samples from the start of the voice. */
struct SwellLanding
{
    int  landing = 0;            ///< T: sample at which the swell peaks
    int  fade = 0;               ///< cosine fade-out after T
    int  voiceLength = 0;        ///< T + fade
    bool noTransport = false;    ///< a beat/bar target fell back to Gap end for lack of transport
    bool fellBackToGapEnd = false;
    double landingPpq = -1.0;    ///< transport position of T when the transport is valid
};

struct SwellTransport
{
    bool   valid = false;
    double ppqNow = 0.0;          ///< ppq at the voice start
    double samplesPerBeat = 0.0;
    int    tsNum = 4, tsDen = 4;
};

inline constexpr float kSwellMinLeadMs = 60.0f;
inline constexpr float kSwellFadeMs = 12.0f;

/** Landing for a voice that would otherwise run `gapLength` samples. */
SwellLanding computeSwellLanding (int target, int gapLength, double sampleRate, const SwellTransport& tr) noexcept;

/** g(t), t in [0,1]: 0 Linear, 1 Exponential t^2.5, 2 S-curve (smoothstep), 3 Late t^5. */
float swellCurveValue (int curve, float t) noexcept;

/** How the reversed material is laid out between voice start and T. */
struct SwellPlan
{
    bool   active = false;
    int    landing = 0;
    int    delay = 0;            ///< leading silence (material shorter than T)
    double startPos = 0.0;       ///< first read index into the capture (counts down to endIndex)
    int    endIndex = 0;
    double rate = 1.0;           ///< 1..2 (never above x2 / +12 semitones)
};

/** `endIndex` is the capture index the reversed material ends at (just inside the
    capture's faded first samples, so the peak is not attenuated). */
SwellPlan planSwell (int captureLength, int landing, int endIndex) noexcept;

//==============================================================================
class SwellMode
{
public:
    void reset() noexcept { active = false; filtL.reset(); filtR.reset(); }

    bool isActive() const noexcept { return active; }
    const SwellPlan& getPlan() const noexcept { return plan; }

    /** Engages the swell for a voice when its setup asks for it. Adjusts the
        voice length / fades so the post-T fade is the voice's own fade-out.
        Returns whether the swell is active. */
    bool configure (bool swellRequested, const SwellSettings& s, int landing, int fade,
                    int captureLength, double sampleRate,
                    int& totalLength, int& fadeIn, int& fadeOut) noexcept;

    /** Produces one sample of swell material; false when inactive (caller renders
        the plain reverse). `read(channel, position)` reads the voice capture. */
    template <class Reader>
    bool generate (float& l, float& r, int elapsed, Reader&& read) noexcept
    {
        if (! active)
            return false;

        const auto e = elapsed - plan.delay;

        if (e < 0)
        {
            l = r = 0.0f;
            return true;
        }

        const auto pos = plan.startPos - plan.rate * (double) e;
        l = read (0, pos);
        r = read (1, pos);

        const auto span = (float) std::max (1, plan.landing - plan.delay);
        const auto t = juce::jlimit (0.0f, 1.0f, (float) e / span);
        const auto c = swellCurveValue (curve, t);

        if (filterAmount > 0.001f && (e & 15) == 0)
        {
            // cutoff sweeps lerp(250 Hz, 18 kHz, curve(t)), scaled by swellFilter
            const auto swept = lerp (250.0f, 18000.0f, c);
            const auto cutoff = lerp (18000.0f, swept, filterAmount);
            filtL.set (sr, cutoff, 0.707f);
            filtR.set (sr, cutoff, 0.707f);
        }

        if (filterAmount > 0.001f)
        {
            float bp, hp, lpL, lpR;
            filtL.process (l, lpL, bp, hp);
            filtR.process (r, lpR, bp, hp);
            l = lpL;
            r = lpR;
        }

        l *= c;
        r *= c;
        return true;
    }

private:
    bool active = false;
    SwellPlan plan;
    int curve = 1;
    float filterAmount = 0.0f;
    double sr = 48000.0;
    SVF filtL, filtR;
};

} // namespace gapfill::fx
