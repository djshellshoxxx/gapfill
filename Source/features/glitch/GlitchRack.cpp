#include "GlitchRack.h"

#include <cmath>

namespace gapfill::glitch
{

void GlitchRack::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    const auto maxSlice = (int) std::ceil (kMaxSliceSeconds * sampleRate) + 16;
    const auto maxSegment = (int) std::ceil (kMaxReverseSeconds * sampleRate) + 16;

    stutter.prepare (sampleRate, maxSlice, maxSlice * (kMaxLookbackUnits + 1) + 16);
    stab.prepare (sampleRate, maxSlice);
    reverser.prepare (sampleRate, maxSegment);
    gator.prepare (sampleRate);
    sweep.prepare (sampleRate);
    phaser.prepare (sampleRate);
    flip.prepare (sampleRate);

    for (auto* e : { &gateEngage, &sweepEngage, &phaserEngage, &flipEngage })
        e->prepare (sampleRate);

    mixStep = (float) (1.0 / std::max (1.0, 0.020 * sampleRate));
    prepared = true;
    reset();
}

void GlitchRack::reset() noexcept
{
    mix = 0.0f;
    active = false;
    clock.reset();
    resetModules();
    meters.rackActive.store (false, std::memory_order_relaxed);
}

void GlitchRack::resetModules() noexcept
{
    stutter.reset();
    stab.reset();
    reverser.reset();
    gator.reset();
    sweep.reset();
    phaser.reset();
    flip.reset();

    for (auto* e : { &gateEngage, &sweepEngage, &phaserEngage, &flipEngage })
        e->reset();
}

void GlitchRack::process (float* left, float* right, int numSamples, const Settings& s, const Timing& t) noexcept
{
    if (! prepared || left == nullptr || numSamples <= 0)
        return;

    const bool wanted = s.on && ! t.bypassed && anyModuleActive (s);

    if (! wanted && ! active)
        return;     // bit-identical bypass

    if (wanted && ! active)
    {
        // Fresh start: every module begins from silence-free, neutral state.
        resetModules();
        clock.reset();
        active = true;
    }

    const auto target = wanted ? std::clamp (s.mix01, 0.0f, 1.0f) : 0.0f;
    clock.begin (t.playing, t.ppqAtStart, t.samplesPerBeat);

    for (int i = 0; i < numSamples; ++i)
    {
        mix = mix < target ? std::min (target, mix + mixStep) : std::max (target, mix - mixStep);

        const auto inL = left[i];
        const auto inR = right != nullptr ? right[i] : inL;
        auto l = inL, r = inR;
        processSample (l, r, s, t);

        // The rack is a sound-design stage: keep it finite and inside +-8 even with feedback.
        l = std::isfinite (l) ? std::clamp (l, -8.0f, 8.0f) : 0.0f;
        r = std::isfinite (r) ? std::clamp (r, -8.0f, 8.0f) : 0.0f;

        // Exact at the ends of the ramp: mix 1 is the rack output, mix 0 the untouched input.
        const auto outL = mix >= 1.0f ? l : inL + (l - inL) * mix;
        const auto outR = mix >= 1.0f ? r : inR + (r - inR) * mix;

        if (right != nullptr)
        {
            left[i] = outL;
            right[i] = outR;
        }
        else
        {
            left[i] = 0.5f * (outL + outR);
        }
    }

    if (! wanted && mix <= 0.0f)
    {
        active = false;
        resetModules();
    }

    meters.rackActive.store (active, std::memory_order_relaxed);
    meters.stutterActive.store (stutter.isRepeating(), std::memory_order_relaxed);
    meters.stabRunning.store (stab.isRunning(), std::memory_order_relaxed);
    meters.stabRepeat.store (stab.repeatIndex(), std::memory_order_relaxed);
    meters.gateGain.store (gator.getGainLeft(), std::memory_order_relaxed);
    meters.cutoffHz.store (s.filtMode != FilterMode::Off ? (float) sweep.getCutoffLeft() : 0.0f,
                           std::memory_order_relaxed);
}

void GlitchRack::processSample (float& l, float& r, const Settings& s, const Timing& t) noexcept
{
    const auto c = clock.next();

    // Time-domain modules manage their own crossfades and idle cheaply.
    stutter.process (s, t, c, l, r);
    stab.process (s, t, l, r);
    reverser.process (s, t, c, l, r);

    const auto blend = [] (float& x, float y, float e) { x += (y - x) * e; };

    if (const auto e = gateEngage.next (s.gateMode != ClockMode::Off); e > 0.0f)
    {
        auto gl = l, gr = r;
        gator.process (s, t, c, gl, gr);
        blend (l, gl, e);
        blend (r, gr, e);
    }
    else if (gateEngage.idle (false))
    {
        gator.reset();
    }

    if (const auto e = sweepEngage.next (s.filtMode != FilterMode::Off); e > 0.0f)
    {
        auto fl = l, fr = r;
        sweep.process (s, t, c, fl, fr);
        blend (l, fl, e);
        blend (r, fr, e);
    }
    else
    {
        sweep.reset();
    }

    if (const auto e = phaserEngage.next (s.phMix01 > 0.0f); e > 0.0f)
    {
        auto pl = l, pr = r;
        phaser.process (s, t, c, pl, pr);
        blend (l, pl, e);
        blend (r, pr, e);
    }
    else
    {
        phaser.reset();
    }

    const bool flipOn = s.polLeft || s.polRight || s.panInvert01 > 0.0f || s.flipMode != FlipMode::Off;

    if (const auto e = flipEngage.next (flipOn); e > 0.0f || flipOn)
        flip.process (s, t, c, l, r);   // the matrix itself glides back to identity
}

} // namespace gapfill::glitch
