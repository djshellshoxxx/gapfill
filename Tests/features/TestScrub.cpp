// F-M19 Freeze & Scrub (docs/dev/12 section 7).

#include "../support/AllocGuard.h"
#include "../support/TestRig.h"
#include "../support/VoiceRig.h"

#include "features/scrub/ScrubVoice.h"

#include <algorithm>
#include <cmath>

using namespace gapfill;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;

    /** Stereo history holding a ramp (value = index * 1e-5) of `seconds`. */
    struct RampHistory
    {
        SourceHistory history;
        std::int64_t anchor = 0;

        explicit RampHistory (double seconds = 3.0, bool sine = false, float sineHz = 220.0f)
        {
            history.prepare (2, (int) (kSr * 6.0));
            const auto n = (int) (kSr * seconds);
            juce::AudioBuffer<float> b (2, n);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    b.setSample (ch, i, sine ? 0.5f * (float) std::sin (2.0 * 3.141592653589793 * sineHz * i / kSr)
                                             : (float) i * 1.0e-5f);

            history.write (b, 0, n);
            anchor = n;
        }
    };

    fx::ScrubSettings settings (bool on, float pos01, float widthMs)
    {
        fx::ScrubSettings s;
        s.on = on;
        s.pos01 = pos01;
        s.widthMs = widthMs;
        return s;
    }

    /** Renders `total` samples in chunks of `chunk`, applying `at(sampleIndex)` settings at each chunk start. */
    template <class Fn>
    juce::AudioBuffer<float> run (fx::ScrubVoice& v, const RampHistory& h, int total, int chunk, Fn at, float duck = 1.0f)
    {
        juce::AudioBuffer<float> out (2, total);
        out.clear();

        for (int pos = 0; pos < total; pos += chunk)
        {
            const auto n = std::min (chunk, total - pos);
            v.render (out, pos, n, h.history, h.anchor, at (pos), duck);
        }

        return out;
    }
}

//==============================================================================
GF_TEST (Scrub, off_identity)
{
    auto render = [] (std::initializer_list<std::pair<const char*, float>> p)
    {
        GapFillAudioProcessor proc;
        setParams (proc, p);
        TestPlayHead ph;
        ph.bpm = 150.0;
        RenderOptions o;
        o.playHead = &ph;
        return renderOffline (proc, drumLoop (kSr, 150.0, 3), 512, kSr, o).out;
    };

    const auto a = render ({ { "fill", 60.0f } });
    const auto b = render ({ { "fill", 60.0f }, { "scrubOn", 0.0f }, { "scrubPos", 100.0f }, { "scrubWidth", 500.0f } });
    GF_EXPECT (buffersIdentical (a, b));

    // Switched on during playback it adds the scrub voice to the wet signal.
    auto withScrub = [] (bool scrub)
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 0.0f } });
        TestPlayHead ph;
        ph.bpm = 150.0;
        RenderOptions o;
        o.playHead = &ph;
        o.onBlock = [&] (int block, int) { if (scrub && block == 150) setParams (proc, { { "scrubOn", 1.0f } }); };
        return renderOffline (proc, drumLoop (kSr, 150.0, 3), 512, kSr, o).out;
    };

    const auto c = withScrub (true);
    const auto d = withScrub (false);
    GF_EXPECT (! buffersIdentical (c, d));
    GF_EXPECT (maxAbs (d, 0, (int) (150 * 512)) == maxAbs (c, 0, (int) (150 * 512)));        // nothing before the switch-on

    auto diff = c;

    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::subtract (diff.getWritePointer (ch), d.getReadPointer (ch), diff.getNumSamples());

    GF_EXPECT_MSG (maxAbs (diff) > 0.02f, "scrub added " + juce::String (maxAbs (diff)));
    GF_EXPECT (allFinite (c));
}

GF_TEST (Scrub, window_position_exact)
{
    const RampHistory h;
    const auto snapLen = (int) std::lround (kSr * 1.0);
    const auto widthMs = 80.0f;
    const auto width = (int) std::lround (kSr * 0.001 * widthMs);

    for (float pos : { 0.0f, 0.25f, 0.5f, 1.0f })
    {
        fx::ScrubVoice v;
        v.prepare (kSr, 2);
        const auto out = run (v, h, (int) kSr, 32, [&] (int) { return settings (true, pos, widthMs); });

        GF_EXPECT (v.snapshotLength() == snapLen);
        const auto expectedStart = (double) pos * (double) (snapLen - width);
        GF_EXPECT_NEAR (v.windowStart(), expectedStart, 1.0);

        // The loop plays the ramp window: its mean level is the ramp at the window centre (x amp).
        const auto centre = (double) (h.anchor - snapLen) + expectedStart + (double) width * 0.5;
        double mean = 0.0;
        const int from = (int) (kSr * 0.8), count = width * 4;

        for (int i = from; i < from + count; ++i)
            mean += out.getSample (0, i);

        mean /= count;
        GF_EXPECT_MSG (std::abs (mean - centre * 1.0e-5 * fx::ScrubVoice::kAmp) < 0.01 * centre * 1.0e-5 + 0.002,
                       "pos " + juce::String (pos) + " mean " + juce::String (mean) + " expected " + juce::String (centre * 1.0e-5 * 0.8));
    }
}

GF_TEST (Scrub, window_movement_is_smoothed_and_speed_bounded)
{
    const RampHistory h;
    fx::ScrubVoice v;
    v.prepare (kSr, 2);

    juce::AudioBuffer<float> out (2, 1);
    double prev = -1.0, worst = 0.0;
    const auto limit = fx::ScrubVoice::maxStartStep (kSr, 80.0f, 1000.0f);

    for (int i = 0; i < 20000; ++i)
    {
        out.clear();
        // Jump the position from 0 to 100 % at sample 2000, back at 12000.
        const auto pos = (i >= 2000 && i < 12000) ? 1.0f : 0.0f;
        v.render (out, 0, 1, h.history, h.anchor, settings (true, pos, 80.0f), 1.0f);

        if (prev >= 0.0)
            worst = std::max (worst, std::abs ((double) v.windowStart() - prev));

        prev = v.windowStart();
    }

    GF_EXPECT_MSG (worst <= limit + 1.0e-4, "window moved " + juce::String (worst) + " samples/sample, limit " + juce::String (limit));
    GF_EXPECT (worst > 0.1);                                      // it does move
    GF_EXPECT (limit <= 2.0);                                     // never faster than 2x real time
}

GF_TEST (Scrub, loop_crossfade_is_click_free)
{
    const RampHistory h (3.0, true);

    for (float widthMs : { 10.0f, 80.0f, 500.0f })
    {
        fx::ScrubVoice v;
        v.prepare (kSr, 2);
        const auto out = run (v, h, (int) (kSr * 1.5), 64, [&] (int pos) { return settings (true, pos < 30000 ? 0.3f : 0.6f, widthMs); });
        GF_EXPECT (allFinite (out));
        GF_EXPECT_MSG (maxSampleJump (out) < 0.06f, "width " + juce::String (widthMs) + " step " + juce::String (maxSampleJump (out)));
        GF_EXPECT (std::abs (out.getSample (0, 0)) < 0.01f);          // the 8 ms fade-in starts silent
    }
}

GF_TEST (Scrub, source_is_a_snapshot_taken_when_the_voice_starts)
{
    RampHistory a (3.0, true), b (3.0, true);

    fx::ScrubVoice va, vb;
    va.prepare (kSr, 2);
    vb.prepare (kSr, 2);
    const auto oa = run (va, a, 24000, 32, [] (int) { return settings (true, 0.4f, 120.0f); });

    // Same history, but afterwards (the live input) other audio arrives: must not matter.
    juce::AudioBuffer<float> more (2, 24000);
    juce::Random rnd (4);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 24000; ++i)
            more.setSample (ch, i, rnd.nextFloat() - 0.5f);

    juce::AudioBuffer<float> out (2, 48000);
    out.clear();

    for (int pos = 0; pos < 48000; pos += 32)
    {
        if (pos == 24000)
            b.history.write (more, 0, 24000);

        vb.render (out, pos, 32, b.history, b.anchor, settings (true, 0.4f, 120.0f), 1.0f);
    }

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 24000; ++i)
            GF_EXPECT (out.getSample (ch, i) == oa.getSample (ch, i));

    // And while the voice runs, the later input never shows up: the second half equals a continuous run.
    fx::ScrubVoice vc;
    vc.prepare (kSr, 2);
    const auto oc = run (vc, a, 48000, 32, [] (int) { return settings (true, 0.4f, 120.0f); });
    GF_EXPECT (buffersIdentical (out, oc));
}

GF_TEST (Scrub, automation_is_block_size_invariant)
{
    const RampHistory h (3.0, true);

    auto automated = [] (int pos)
    {
        return settings (true, pos < 9600 ? 0.2f : (pos < 19200 ? 0.8f : 0.5f), pos < 14400 ? 60.0f : 150.0f);
    };

    juce::AudioBuffer<float> first;

    for (int chunk : { 32, 48, 96, 160, 480 })
    {
        fx::ScrubVoice v;
        v.prepare (kSr, 2);
        const auto out = run (v, h, 28800, chunk, automated);

        if (first.getNumSamples() == 0)
            first.makeCopyOf (out);
        else
            GF_EXPECT_MSG (buffersIdentical (out, first), "chunk " + juce::String (chunk));
    }
}

GF_TEST (Scrub, panic_fades_in_8ms_and_holds_the_voice_off_until_it_is_switched_again)
{
    const RampHistory h (3.0, true);
    fx::ScrubVoice v;
    v.prepare (kSr, 2);

    juce::AudioBuffer<float> out (2, 48000);
    out.clear();

    for (int pos = 0; pos < 48000; pos += 32)
    {
        if (pos == 12000)
            v.panic();

        v.render (out, pos, 32, h.history, h.anchor, settings (true, 0.5f, 80.0f), 1.0f);
    }

    GF_EXPECT (v.isHeldOffByPanic());
    GF_EXPECT (maxAbs (out, 12000 + (int) (kSr * 0.008) + 64, 48000 - 12000 - (int) (kSr * 0.008) - 64) == 0.0f);
    GF_EXPECT (maxAbs (out, 11000, 1000) > 0.05f);                          // it was sounding before
    GF_EXPECT (maxSampleJump (out) < 0.06f);                                // and fades rather than cuts

    // Release then press again: sounds again.
    juce::AudioBuffer<float> again (2, 4800);
    again.clear();
    v.render (again, 0, 32, h.history, h.anchor, settings (false, 0.5f, 80.0f), 1.0f);
    GF_EXPECT (! v.isHeldOffByPanic());

    for (int pos = 0; pos < 4800; pos += 32)
        v.render (again, pos, 32, h.history, h.anchor, settings (true, 0.5f, 80.0f), 1.0f);

    GF_EXPECT (maxAbs (again, 2400, 2000) > 0.05f);
}

GF_TEST (Scrub, switching_off_fades_and_the_duck_is_applied)
{
    const RampHistory h (3.0, true);
    fx::ScrubVoice v;
    v.prepare (kSr, 2);
    const auto loud = run (v, h, 9600, 32, [] (int) { return settings (true, 0.5f, 80.0f); }, 1.0f);

    fx::ScrubVoice d;
    d.prepare (kSr, 2);
    const auto ducked = run (d, h, 9600, 32, [] (int) { return settings (true, 0.5f, 80.0f); }, 0.25f);
    GF_EXPECT_NEAR (maxAbs (ducked, 2000, 7000) / maxAbs (loud, 2000, 7000), 0.25, 0.02);

    fx::ScrubVoice o;
    o.prepare (kSr, 2);
    const auto off = run (o, h, 9600, 32, [] (int pos) { return settings (pos < 4800, 0.5f, 80.0f); });
    GF_EXPECT (maxAbs (off, 4800 + 400, 4800 - 400) == 0.0f);
    GF_EXPECT (maxSampleJump (off) < 0.06f);
    GF_EXPECT (! o.isActive());
}

GF_TEST (Scrub, extremes_are_finite_and_nothing_allocates)
{
    const RampHistory h (3.0, true);

    for (float width : { 10.0f, 500.0f })
    {
        for (float pos : { 0.0f, 1.0f })
        {
            fx::ScrubVoice v;
            v.prepare (kSr, 2);
            v.setWindowMs (250.0f);                          // window shorter than the 500 ms width
            const auto out = run (v, h, 24000, 64, [&] (int) { return settings (true, pos, width); });
            GF_EXPECT (allFinite (out));
            GF_EXPECT (maxAbs (out) < 2.0f);
        }
    }

    fx::ScrubVoice v;
    v.prepare (kSr, 2);
    juce::AudioBuffer<float> out (2, 64);
    out.clear();
    v.render (out, 0, 64, h.history, h.anchor, settings (true, 0.5f, 80.0f), 1.0f);       // warm up

    AllocGuard guard;

    for (int i = 0; i < 600; ++i)
    {
        out.clear();
        v.render (out, 0, 64, h.history, h.anchor, settings (true, (float) (i % 10) * 0.1f, 80.0f), 1.0f);
    }

    GF_EXPECT (guard.count() == 0);
}

GF_TEST (Scrub, parameters_are_performance_state_and_round_trip)
{
    const auto perf = performanceParameterIDs();
    GF_EXPECT (perf.contains ("scrubOn") && perf.contains ("scrubPos"));
    GF_EXPECT (! randomisableParameterIDs().contains ("scrubOn"));

    GapFillAudioProcessor a;
    setParams (a, { { "scrubOn", 1.0f }, { "scrubPos", 12.5f }, { "scrubWidth", 333.0f } });
    juce::MemoryBlock block;
    a.getStateInformation (block);
    GapFillAudioProcessor b;
    b.setStateInformation (block.getData(), (int) block.getSize());

    for (const char* id : { "scrubOn", "scrubPos", "scrubWidth" })
        GF_EXPECT (std::abs (a.getValueTreeState().getRawParameterValue (id)->load()
                             - b.getValueTreeState().getRawParameterValue (id)->load()) < 1.0e-4f);
}
