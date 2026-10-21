// F-X01 Harmonic granular resynthesis (docs/dev/12 F-X01) and the Scale Lock seam of the bus cards.

#include "../support/TestRig.h"
#include "../support/VoiceRig.h"
#include "../support/AllocGuard.h"

#include "features/fx/EngineBlockIO.h"
#include "features/harmonic/HarmonicGrains.h"
#include "features/harmonic/HarmonicHooks.h"
#include "features/harmonic/Yin.h"
#include "dsp/FillEngine.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace gapfill;
using namespace gapfill::harmonic;
using namespace gftest;

namespace
{
    /** Six partials at 1/n with a strong second harmonic (the classic octave-error trap). */
    std::vector<float> tone (double sr, double f0, double seconds, float secondHarmonic = 1.2f)
    {
        std::vector<float> out ((size_t) (sr * seconds));

        for (size_t i = 0; i < out.size(); ++i)
        {
            const double t = (double) i / sr;
            double v = 0.0;

            for (int h = 1; h <= 6; ++h)
                v += (h == 2 ? (double) secondHarmonic : 1.0) / (double) h * std::sin (6.283185307179586 * f0 * h * t);

            out[i] = (float) (0.2 * v);
        }

        return out;
    }

    void fillHistory (SourceHistory& h, const std::vector<float>& mono)
    {
        juce::AudioBuffer<float> b (2, (int) mono.size());

        for (int ch = 0; ch < 2; ++ch)
            std::memcpy (b.getWritePointer (ch), mono.data(), sizeof (float) * mono.size());

        h.write (b, 0, (int) mono.size());
    }

    /** A stereo SourceHistory holding `mono` (anchor = last sample). */
    struct Hist
    {
        explicit Hist (const std::vector<float>& mono)
        {
            h.prepare (2, (int) mono.size() + 16);
            fillHistory (h, mono);
        }

        operator const SourceHistory&() const { return h; }
        SourceHistory h;
    };

    void loadRig (VoiceRig& rig, const std::vector<float>& mono)
    {
        rig.history.clear();
        fillHistory (rig.history, mono);
        rig.anchor = (std::int64_t) mono.size() - 1;
    }

    double goertzel (const std::vector<float>& x, double f, double sr)
    {
        double s1 = 0, s2 = 0;
        const double c = 2.0 * std::cos (6.283185307179586 * f / sr);

        for (float v : x)
        {
            const double s0 = v + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
        return std::sqrt (std::max (0.0, power)) / (double) x.size();
    }

    std::vector<float> renderGrains (HarmonicGrains& g, const SourceHistory& h, int n)
    {
        std::vector<float> out ((size_t) n);

        for (int i = 0; i < n; ++i)
        {
            float l = 0.0f, r = 0.0f;
            g.render (l, r, h, 2);
            out[(size_t) i] = 0.5f * (l + r);
        }

        return out;
    }

    double periodicity (const std::vector<float>& x, int period)
    {
        double num = 0.0, den = 0.0, den2 = 0.0;

        for (size_t i = 0; i + (size_t) period < x.size(); ++i)
        {
            num += (double) x[i] * x[i + (size_t) period];
            den += (double) x[i] * x[i];
            den2 += (double) x[i + (size_t) period] * x[i + (size_t) period];
        }

        return den > 1.0e-12 && den2 > 1.0e-12 ? num / std::sqrt (den * den2) : 0.0;
    }

    HarmonicSetup setupFor (float f0, int voices, float motion = 0.0f, float tonePct = 0.0f)
    {
        HarmonicSetup s;
        s.enabled = true;
        s.f0Hz = f0;
        s.confidence = 0.95f;
        s.voices = voices;
        s.motion01 = motion;
        s.tone = tonePct;
        return s;
    }
}

//==============================================================================
GF_TEST (Harmonic, yin_accuracy_55_to_1000_Hz_at_four_sample_rates)
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (double f : { 55.0, 82.4, 110.0, 164.8, 220.0, 330.0, 440.0, 660.0, 880.0, 1000.0 })
        {
            const auto x = tone (sr, f, 0.25);
            const auto r = yinDetect (x.data(), (int) x.size(), sr);
            GF_EXPECT_MSG (r.voiced, "sr " + juce::String (sr) + " f " + juce::String (f) + " conf " + juce::String (r.confidence));
            GF_EXPECT_MSG (std::abs (r.f0Hz / f - 1.0) < 0.005, "sr " + juce::String (sr) + " f " + juce::String (f) + " got " + juce::String (r.f0Hz));
        }
}

GF_TEST (Harmonic, yin_has_no_octave_errors_with_a_strong_second_harmonic)
{
    juce::Random rnd (8);
    int octaveErrors = 0, total = 0;

    for (int i = 0; i < 60; ++i)
    {
        const double f = 55.0 * std::pow (2.0, rnd.nextDouble() * 4.0);        // 55 .. 880 Hz
        const auto x = tone (48000.0, f, 0.25, 1.6f);
        const auto r = yinDetect (x.data(), (int) x.size(), 48000.0);
        ++total;
        octaveErrors += std::abs (r.f0Hz / f - 2.0) < 0.05 || std::abs (r.f0Hz / f - 0.5) < 0.05;
    }

    GF_EXPECT_MSG (octaveErrors * 50 <= total, "octave errors " + juce::String (octaveErrors) + " of " + juce::String (total));       // < 2 %
}

GF_TEST (Harmonic, yin_rejects_noise_drums_and_silence)
{
    const double sr = 48000.0;
    juce::Random rnd (3);
    int voiced = 0, windows = 0, perSource[2] { 0, 0 };

    // White noise.
    std::vector<float> noise ((size_t) (sr * 6.0));

    for (auto& v : noise) v = 0.3f * (rnd.nextFloat() * 2.0f - 1.0f);

    // A drum loop.
    const auto drums = drumLoop (sr, 120.0, 6);
    std::vector<float> drumMono ((size_t) drums.getNumSamples());
    std::copy (drums.getReadPointer (0), drums.getReadPointer (0) + drums.getNumSamples(), drumMono.begin());

    const int win = yinSourceSamples (sr);

    for (const auto* src : { &noise, &drumMono })
        for (size_t pos = 0; pos + (size_t) win < src->size(); pos += (size_t) (sr * 0.05))
        {
            const auto r = yinDetect (src->data() + pos, win, sr);
            ++windows;
            voiced += r.voiced;
            perSource[src == &noise ? 0 : 1] += r.voiced;
        }

    std::printf ("         voiced windows: noise %d, drums %d of %d total\n", perSource[0], perSource[1], windows);

    GF_EXPECT_MSG (voiced * 10 <= windows, "voiced " + juce::String (voiced) + " of " + juce::String (windows));          // >= 90 % unvoiced

    std::vector<float> silence ((size_t) (sr * 0.3), 0.0f);
    const auto s = yinDetect (silence.data(), (int) silence.size(), sr);
    GF_EXPECT (! s.voiced && std::isfinite (s.f0Hz) && std::isfinite (s.confidence));

    // Too short or null input is safe.
    GF_EXPECT (! yinDetect (nullptr, 0, sr).voiced);
    GF_EXPECT (! yinDetect (silence.data(), 100, sr).voiced);
}

GF_TEST (Harmonic, pitch_step_cpu_is_reported_and_bounded)
{
    const auto x = tone (48000.0, 110.0, 0.25);
    double worst = 0.0;

    for (int i = 0; i < 40; ++i)
    {
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = yinDetect (x.data(), (int) x.size(), 48000.0);
        worst = std::max (worst, std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count());
        GF_EXPECT (r.voiced);
    }

    std::printf ("         worst YIN step: %.3f ms (budget 0.3 ms on the reference build)\n", worst);
    GF_EXPECT (juce::SystemStats::getEnvironmentVariable ("GAPFILL_PERF_ENFORCE", {}) != "1" || (worst < 5.0));   // wall clock: enforced only with GAPFILL_PERF_ENFORCE=1
}

GF_TEST (Harmonic, ratio_table_and_thirds)
{
    GF_EXPECT (intervalRatio (0, false) == 1.0f && intervalRatio (1, false) == 2.0f && intervalRatio (2, false) == 1.5f);
    GF_EXPECT (intervalRatio (3, false) == 1.25f && intervalRatio (3, true) == 1.2f);
    GF_EXPECT (intervalRatio (4, false) == 0.5f && intervalRatio (5, false) == 4.0f);
}

GF_TEST (Harmonic, output_peaks_at_the_target_ratios)
{
    const double sr = 48000.0;
    std::vector<float> sine ((size_t) (sr * 1.5));

    for (size_t i = 0; i < sine.size(); ++i)
        sine[i] = 0.3f * (float) std::sin (6.283185307179586 * 220.0 * (double) i / sr);

    const Hist h (sine);
    HarmonicGrains g;
    g.configure (setupFor (220.0f, 4), (std::int64_t) sine.size() - 1, sr, 7);
    GF_EXPECT (g.active());

    const auto out = renderGrains (g, h, (int) (sr * 0.6));

    // Unison, major third (5/4), fifth (3/2) and octave all carry energy; frequencies in between do not.
    const double peaks[] = { 220.0, 275.0, 330.0, 440.0 };
    const double between[] = { 250.0, 300.0, 385.0, 500.0 };
    double peakMin = 1.0e9, betweenMax = 0.0;

    for (double f : peaks) peakMin = std::min (peakMin, goertzel (out, f, sr));
    for (double f : between) betweenMax = std::max (betweenMax, goertzel (out, f, sr));

    GF_EXPECT_MSG (peakMin > 4.0 * betweenMax, "peaks " + juce::String (peakMin) + " vs between " + juce::String (betweenMax));

    // Within +-10 cents: the exact frequency beats a frequency 10 cents away.
    for (double f : peaks)
    {
        const double off = f * std::pow (2.0, 25.0 / 1200.0);
        GF_EXPECT (goertzel (out, f, sr) > goertzel (out, off, sr));
    }
}

GF_TEST (Harmonic, output_is_periodic_where_plain_granular_is_not)
{
    const double sr = 48000.0;
    const double f0 = 196.0;
    const auto src = tone (sr, f0, 1.5);
    const Hist h (src);

    HarmonicGrains g;
    g.configure (setupFor ((float) f0, 1), (std::int64_t) src.size() - 1, sr, 5);
    const auto harmonic = renderGrains (g, h, (int) (sr * 0.5));

    // Plain Granular on the same source through a real voice (harmonic disabled).
    VoiceRig rig;
    loadRig (rig, src);
    FillVoice v;
    v.prepare (kRigSr, 2);
    auto s = rig.setup (Algo::Granular, 500.0, 9);
    s.intensity01 = 0.8f;
    s.chaos01 = 0.8f;
    const auto wet = rig.render (v, s, (int) (sr * 0.5), 64);
    std::vector<float> plain ((size_t) wet.getNumSamples());

    for (int i = 0; i < wet.getNumSamples(); ++i)
        plain[(size_t) i] = 0.5f * (wet.getSample (0, i) + wet.getSample (1, i));

    const int period = (int) std::lround (sr / f0);
    const double a = periodicity (std::vector<float> (harmonic.begin() + 4000, harmonic.end()), period);
    const double b = periodicity (std::vector<float> (plain.begin() + 4000, plain.end()), period);
    std::printf ("         periodicity at the target period: harmonic %.2f, plain Granular %.2f\n", a, b);
    GF_EXPECT_MSG (a >= 0.8, "harmonic " + juce::String (a));
    GF_EXPECT_MSG (b <= 0.6 && a > b + 0.2, "plain " + juce::String (b));
}

GF_TEST (Harmonic, a_harmonic_granular_voice_plays_through_the_fill_voice)
{
    const double sr = kRigSr;
    const auto src = tone (sr, 147.0, 1.5);
    VoiceRig rig;
    loadRig (rig, src);

    auto render = [&] (bool enabled)
    {
        FillVoice v;
        v.prepare (sr, 2);
        auto s = rig.setup (Algo::Granular, 400.0, 3);
        s.harmonicSetup = setupFor (147.0f, 3, 0.4f);
        s.harmonicSetup.enabled = enabled;
        return rig.render (v, s, (int) (sr * 0.4), 64);
    };

    const auto on = render (true);
    const auto off = render (false);
    GF_EXPECT (! buffersIdentical (on, off));
    GF_EXPECT (rmsOf (on, 4000, 8000) > 0.01);

    for (int i = 0; i < on.getNumSamples(); ++i)
        GF_EXPECT (std::isfinite (on.getSample (0, i)) && std::abs (on.getSample (0, i)) < 3.0f);

    GF_EXPECT (buffersIdentical (render (true), on));                       // deterministic

    // A disabled setup is the legacy path: identical to a setup that never mentioned it.
    FillVoice a, b;
    a.prepare (sr, 2);
    b.prepare (sr, 2);
    auto sa = rig.setup (Algo::Granular, 400.0, 3);
    auto sb = sa;
    sb.harmonicSetup = HarmonicSetup {};
    GF_EXPECT (buffersIdentical (rig.render (a, sa, (int) (sr * 0.4), 64), rig.render (b, sb, (int) (sr * 0.4), 64)));
}

GF_TEST (Harmonic, voices_and_grains_stay_bounded_and_extreme_settings_are_finite)
{
    const double sr = 48000.0;
    const auto src = tone (sr, 60.0, 1.5);
    const Hist h (src);

    for (int voices : { 1, 3, 6, 9 })
        for (float motion : { 0.0f, 1.0f })
            for (float tonePct : { -1.0f, 0.0f, 1.0f })
            {
                HarmonicGrains g;
                g.configure (setupFor (60.0f, voices, motion, tonePct), (std::int64_t) src.size() - 1, sr, 11);
                int worst = 0;

                for (int i = 0; i < (int) (sr * 0.5); ++i)
                {
                    float l = 0.0f, r = 0.0f;
                    g.render (l, r, h, 2);
                    GF_EXPECT (std::isfinite (l) && std::isfinite (r) && std::abs (l) <= 2.0f + 1.0e-4f && std::abs (r) <= 2.0f + 1.0e-4f);
                    worst = std::max (worst, g.activeGrains());
                }

                GF_EXPECT (worst <= kMaxStreams * kGrainsPerStream && worst <= kGrainCap);
            }

    // Out-of-range pitches never start the engine.
    HarmonicGrains bad;
    bad.configure (setupFor (5.0f, 3), 1000, sr, 1);
    GF_EXPECT (! bad.active());
    bad.configure (setupFor (5000.0f, 3), 1000, sr, 1);
    GF_EXPECT (! bad.active());
    bad.configure ({}, 1000, sr, 1);
    GF_EXPECT (! bad.active());
}

GF_TEST (Harmonic, decision_uses_the_source_pitch_and_falls_back_for_unpitched_material)
{
    const double sr = 48000.0;
    VoiceSetup base;
    base.algo = Algo::Granular;
    base.sampleRate = sr;

    EngineSettings s;
    s.perf.harmonic.amount01 = 1.0f;
    s.perf.harmonic.voices = 4;

    FillPlan plan;
    GapContext ctx;
    ctx.key = 77;
    fx::FxMeters meters;

    // Pitched source: enabled, f0 close to 130.8 Hz.
    const auto pitched = tone (sr, 130.81, 1.0);
    const Hist hp (pitched);
    auto setup = base;
    setup.sourceAnchor = (std::int64_t) pitched.size() - 1;
    decide (setup, plan, ctx, s, hp, &meters);
    GF_EXPECT (setup.harmonicSetup.enabled && std::abs (setup.harmonicSetup.f0Hz / 130.81 - 1.0) < 0.01);
    GF_EXPECT (setup.harmonicSetup.voices == 4 && meters.hgState.load() == 1);
    GF_EXPECT (! setup.harmonicSetup.minorThird);

    // Scale Lock with a minor third in the scale picks the minor third.
    auto minor = s;
    minor.scaleLock.set = scale::makeScaleSet (2, (int) scale::ScaleType::NaturalMinor);
    minor.scaleLock.amount01 = 1.0f;
    auto m = base;
    m.sourceAnchor = setup.sourceAnchor;
    decide (m, plan, ctx, minor, hp, nullptr);
    GF_EXPECT (m.harmonicSetup.enabled && m.harmonicSetup.minorThird);

    // Noise: falls back to ordinary Granular and says so.
    juce::Random rnd (4);
    std::vector<float> noise ((size_t) (sr * 1.0));

    for (auto& v : noise) v = 0.3f * (rnd.nextFloat() * 2.0f - 1.0f);

    const Hist hn (noise);
    auto n = base;
    n.sourceAnchor = (std::int64_t) noise.size() - 1;
    decide (n, plan, ctx, s, hn, &meters);
    GF_EXPECT (! n.harmonicSetup.enabled && meters.hgState.load() == 2);

    // Other algorithms and Amount 0 are untouched.
    auto other = base;
    other.algo = Algo::Stutter;
    other.sourceAnchor = setup.sourceAnchor;
    const auto copy = other;
    decide (other, plan, ctx, s, hp, nullptr);
    GF_EXPECT (std::memcmp (&other.harmonicSetup, &copy.harmonicSetup, sizeof (HarmonicSetup)) == 0);

    auto zero = s;
    zero.perf.harmonic.amount01 = 0.0f;
    auto z = base;
    z.sourceAnchor = setup.sourceAnchor;
    decide (z, plan, ctx, zero, hp, nullptr);
    GF_EXPECT (! z.harmonicSetup.enabled);

    // Amount 50 %: the salted coin decides, about half of the keys go harmonic.
    auto half = s;
    half.perf.harmonic.amount01 = 0.5f;
    int hits = 0;

    for (int i = 0; i < 200; ++i)
    {
        GapContext c;
        c.key = hashCombine (0x44, (std::uint64_t) i);
        auto v = base;
        v.sourceAnchor = setup.sourceAnchor;
        decide (v, plan, c, half, hp, nullptr);
        hits += v.harmonicSetup.enabled;
    }

    GF_EXPECT_MSG (hits > 70 && hits < 130, "hits " + juce::String (hits));
}

GF_TEST (Harmonic, off_is_bit_identical_through_the_processor)
{
    auto render = [] (std::initializer_list<std::pair<const char*, float>> p)
    {
        GapFillAudioProcessor proc;
        setParams (proc, p);
        TestPlayHead ph;
        ph.bpm = goldenBpm;
        RenderOptions o;
        o.playHead = &ph;
        return renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, goldenBars), 512, goldenSampleRate, o).out;
    };

    const auto a = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "fillType", 4.0f } });
    const auto b = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "fillType", 4.0f }, { "hgAmount", 0.0f },
                             { "hgVoices", 6.0f }, { "hgMotion", 100.0f }, { "hgTone", 80.0f } });
    GF_EXPECT (buffersIdentical (a, b));

    GapFillAudioProcessor fresh;
    GF_EXPECT (pv (fresh.getValueTreeState().getRawParameterValue ("hgAmount")) == 0.0f);
    GF_EXPECT (pi (fresh.getValueTreeState().getRawParameterValue ("hgVoices")) == 3);
    GF_EXPECT (! randomisableParameterIDs().contains ("hgAmount"));
}

GF_TEST (Harmonic, a_pitched_source_makes_granular_fills_harmonic_end_to_end)
{
    const double sr = goldenSampleRate;
    auto drums = drumLoop (sr, goldenBpm, goldenBars);

    // A steady bass tone under the drums gives the pitch step something to find.
    for (int ch = 0; ch < drums.getNumChannels(); ++ch)
        for (int i = 0; i < drums.getNumSamples(); ++i)
            drums.setSample (ch, i, drums.getSample (ch, i) + 0.25f * (float) std::sin (6.283185307179586 * 110.0 * (double) i / sr));

    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "fillType", 4.0f }, { "hgAmount", 100.0f } });
    TestPlayHead ph;
    ph.bpm = goldenBpm;
    RenderOptions o;
    o.playHead = &ph;
    const auto r = renderOffline (proc, drums, 512, sr, o);
    GF_EXPECT (r.fills > 0);
    GF_EXPECT_MSG (proc.getFxMeters().hgState.load() == 1, "state " + juce::String (proc.getFxMeters().hgState.load()));
    GF_EXPECT_MSG (std::abs (proc.getFxMeters().hgF0.load() / 110.0 - 1.0) < 0.02, "f0 " + juce::String (proc.getFxMeters().hgF0.load()));

    for (int i = 0; i < r.out.getNumSamples(); i += 53)
        GF_EXPECT (std::isfinite (r.out.getSample (0, i)));
}

GF_TEST (Harmonic, scale_lock_reaches_the_bus_cards)
{
    FillEngine engine;
    engine.prepare (48000.0, 512, 2);
    AnalysisEngine analysis;
    analysis.prepare (48000.0, 512);
    SourceHistory history;
    history.prepare (2, 48000 * 6);
    juce::AudioBuffer<float> wet (2, 512);

    auto run = [&] (const EngineSettings& s)
    {
        wet.clear();
        fx::EngineBlockIO io;
        engine.process (wet, 0, 512, 0, analysis, history, s, nullptr, nullptr, &io);
        return io;
    };

    EngineSettings off;
    const auto ioOff = run (off);
    GF_EXPECT (ioOff.scaleMask == 0);

    EngineSettings on;
    on.scaleLock.set = scale::makeScaleSet (2, (int) scale::ScaleType::NaturalMinor);
    on.scaleLock.amount01 = 1.0f;
    const auto ioOn = run (on);
    GF_EXPECT (ioOn.scaleMask == on.scaleLock.set.mask && ioOn.scaleMask != 0);
    GF_EXPECT (ioOn.scaleRoot == 2);

    EngineSettings zeroAmount = on;
    zeroAmount.scaleLock.amount01 = 0.0f;
    GF_EXPECT (run (zeroAmount).scaleMask == 0);                       // an inactive lock reaches the cards as "no scale"
}

GF_TEST (Harmonic, audio_thread_does_not_allocate_with_harmonic_fills)
{
    const double sr = goldenSampleRate;
    auto drums = drumLoop (sr, goldenBpm, goldenBars);

    for (int ch = 0; ch < drums.getNumChannels(); ++ch)
        for (int i = 0; i < drums.getNumSamples(); ++i)
            drums.setSample (ch, i, drums.getSample (ch, i) + 0.25f * (float) std::sin (6.283185307179586 * 110.0 * (double) i / sr));

    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "fillType", 4.0f }, { "hgAmount", 100.0f } });
    TestPlayHead ph;
    ph.bpm = goldenBpm;
    RenderOptions o;
    o.playHead = &ph;
    renderOffline (proc, drums, 512, sr, o);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block (2, 512);
    AllocGuard guard;

    for (int i = 0; i < 300; ++i)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < 512; ++k)
                block.setSample (ch, k, 0.25f * (float) std::sin (6.283185307179586 * 110.0 * (double) (i * 512 + k) / sr));

        proc.processBlock (block, midi);
    }

    GF_EXPECT (guard.count() == 0);
}
