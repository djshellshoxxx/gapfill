#if defined (GF_CONTEXT_STANDALONE)
 #include "ContextStandalone.h"
 #include <iostream>
#else
 #include "../TestFramework.h"
 #include "../support/AllocGuard.h"
#endif
#include "../../Source/features/harmonic/psola/Voice.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>
using namespace gapfill::harmonic::psola;
namespace
{
    std::vector<float> tone (double sr, double f0, int count)
    {
        std::vector<float> result ((std::size_t) count);
        for (int i = 0; i < count; ++i) result[(std::size_t) i] = (float) (0.3 * std::cos (6.283185307179586 * f0 * i / sr));
        return result;
    }
    Setup configured (float f0 = 220)
    {
        Setup setup;
        setup.voiced = true; setup.f0Hz = f0; setup.clarity = 1;
        setup.settings.enabled = true; setup.settings.intervals = IntervalSet::Major; setup.settings.voices = 4;
        setup.settings.motion01 = setup.settings.scatter01 = 0;
        return setup;
    }
    std::vector<float> render (Voice& voice, int count, bool mono = true)
    {
        std::vector<float> result ((std::size_t) count);
        for (int i = 0; i < count; ++i)
        {
            float l = 0, r = 0; voice.render (l, r);
            GF_EXPECT (std::isfinite (l) && std::isfinite (r) && std::abs (l) <= 2 && std::abs (r) <= 2);
            if (mono) GF_EXPECT (l == r);
            result[(std::size_t) i] = 0.5f * (l + r);
        }
        return result;
    }
    double amplitude (const std::vector<float>& x, double hz, double sr, int first)
    {
        const double coefficient = 2 * std::cos (6.283185307179586 * hz / sr);
        double a = 0, b = 0;
        for (std::size_t i = (std::size_t) first; i < x.size(); ++i)
        {
            const double next = x[i] + coefficient * a - b;
            b = a; a = next;
        }
        return std::sqrt (std::max (0.0, a*a + b*b - coefficient*a*b)) / (double) (x.size() - (std::size_t) first);
    }
}
GF_TEST (PitchSyncVoice, pure_sine_major_peaks_are_within_ten_cents_at_four_rates)
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto source = tone (sr, 220, (int) (sr * 0.32));
        Capture capture { source.data(), nullptr, (int) source.size(), 1 };
        PitchMarks finder; finder.prepare (sr);
        const auto marks = finder.find (capture, 220);
        GF_EXPECT (marks.ready());
        Voice voice;
        GF_EXPECT (voice.configure (configured(), capture, marks.epochs, sr, 7, (int) sr));
        const auto output = render (voice, (int) sr);
        const int first = (int) (sr * 0.2);
        double minimumPeak = 1e9, largestBetween = 0;
        for (double target : { 220.0, 275.0, 330.0, 440.0 })
        {
            double best = 0; int bestCents = -30;
            for (int cents = -30; cents <= 30; ++cents)
            {
                const double value = amplitude (output, target * std::exp2 (cents / 1200.0), sr, first);
                if (value > best) { best = value; bestCents = cents; }
            }
            std::printf (" PSOLA sr%.0f target%.0f peak%dc amplitude%.5f\n", sr, target, bestCents, best);
            GF_EXPECT (std::abs (bestCents) <= 10);
            minimumPeak = std::min (minimumPeak, best);
        }
        for (double between : { 250.0, 300.0, 385.0, 500.0 })
            largestBetween = std::max (largestBetween, amplitude (output, between, sr, first));
        GF_EXPECT (minimumPeak > 4 * largestBetween && minimumPeak > 0.005);
    }
}
GF_TEST (PitchSyncVoice, adaptive_octave_does_not_cancel_and_dc_is_removed)
{
    auto source = tone (48000, 240, 15360);
    Capture capture { source.data(), nullptr, (int) source.size(), 1 };
    PitchMarks finder; finder.prepare (48000);
    const auto marks = finder.find (capture, 240);
    auto setup = configured (240);
    setup.settings.intervals = IntervalSet::Octaves; setup.settings.voices = 2;
    Voice voice;
    GF_EXPECT (voice.configure (setup, capture, marks.epochs, 48000, 9, 48000));
    const auto output = render (voice, 48000);
    GF_EXPECT (amplitude (output, 480, 48000, 12000) > 0.02);
    double dc = 0;
    for (std::size_t i = 24000; i < output.size(); ++i) dc += output[i];
    GF_EXPECT (std::abs (dc / 24000) < 1e-5);
}
GF_TEST (PitchSyncVoice, registry_motion_and_scatter_defaults_keep_the_major_pitch_targets)
{
    auto source = tone (48000, 220, 15360);
    Capture capture { source.data(), nullptr, (int) source.size(), 1 };
    PitchMarks finder; finder.prepare (48000);
    const auto marks = finder.find (capture, 220);
    auto setup = configured(); setup.settings.motion01 = 0.3f; setup.settings.scatter01 = 0.2f;
    Voice voice; GF_EXPECT (voice.configure (setup, capture, marks.epochs, 48000, 7, 96000));
    const auto output = render (voice, 96000);
    for (double target : { 220.0, 275.0, 330.0, 440.0 })
    {
        double best = 0; int bestCents = -30;
        for (int cents = -30; cents <= 30; ++cents)
        {
            const auto score = amplitude (output, target * std::exp2 (cents / 1200.0), 48000, 12000);
            if (score > best) { best = score; bestCents = cents; }
        }
        std::printf (" default PSOLA target%.0f: peak%dc\n", target, bestCents);
        GF_EXPECT (std::abs (bestCents) <= 10);
    }
}
GF_TEST (PitchSyncVoice, zero_mix_skips_processing_and_partial_mix_is_owned_by_the_adapter)
{
    auto source = tone (48000, 220, 15360);
    Capture capture { source.data(), nullptr, (int) source.size(), 1 };
    PitchMarks finder; finder.prepare (48000);
    const auto marks = finder.find (capture, 220);
    auto setup = configured(); setup.settings.mix01 = 0;
    Voice skipped;
    GF_EXPECT (! skipped.configure (setup, capture, marks.epochs, 48000, 7, 4096) && ! skipped.active());
    setup.settings.mix01 = 1;
    Voice full; GF_EXPECT (full.configure (setup, capture, marks.epochs, 48000, 7, 4096));
    setup.settings.mix01 = 0.25f;
    Voice partial; GF_EXPECT (partial.configure (setup, capture, marks.epochs, 48000, 7, 4096));
    GF_EXPECT (partial.mixWeight() == 0.25f && full.mixWeight() == 1);
    GF_EXPECT (render (partial, 4096) == render (full, 4096)); // both are unweighted PSOLA
}
GF_TEST (PitchSyncVoice, unison_output_is_highly_periodic)
{
    auto source = tone (48000, 196, 15360);
    Capture capture { source.data(), nullptr, (int) source.size(), 1 };
    PitchMarks finder; finder.prepare (48000);
    const auto marks = finder.find (capture, 196);
    auto setup = configured (196); setup.settings.voices = 1;
    Voice voice; GF_EXPECT (voice.configure (setup, capture, marks.epochs, 48000, 7, 24000));
    const auto output = render (voice, 24000);
    const int lag = (int) std::lround (48000.0 / 196);
    double cross = 0, a = 0, b = 0;
    for (int i = 4000; i + lag < 24000; ++i)
    {
        const double x = output[(std::size_t) i], y = output[(std::size_t) (i + lag)];
        cross += x*y; a += x*x; b += y*y;
    }
    GF_EXPECT (a > 0 && b > 0 && cross / std::sqrt (a*b) >= 0.8);
}
GF_TEST (PitchSyncVoice, inactive_is_bit_identical_and_invalid_capture_never_starts)
{
    Voice voice;
    float l = 0.234567f, r = -0.765432f;
    voice.render (l, r);
    GF_EXPECT (l == 0.234567f && r == -0.765432f);
    GF_EXPECT (! voice.configure ({}, {}, {}, 48000, 1, 1000));
    auto source = tone (48000, 220, 15360);
    PitchMarks finder; finder.prepare (48000);
    auto marks = finder.find ({ source.data(), nullptr, (int) source.size(), 1 }, 220);
    marks.epochs.positions[0] = -1;
    GF_EXPECT (! voice.configure (configured(), { source.data(), nullptr, (int) source.size(), 1 }, marks.epochs, 48000, 1, 1000));
}
GF_TEST (PitchSyncVoice, seed_is_repeatable_extremes_are_bounded_and_deadline_is_exact)
{
    auto source = tone (48000, 220, 15360);
    Capture capture { source.data(), source.data(), (int) source.size(), 2 };
    PitchMarks finder; finder.prepare (48000);
    const auto marks = finder.find (capture, 220);
    auto setup = configured(); setup.settings.motion01 = setup.settings.scatter01 = 1;
    setup.settings.tone = -1; setup.settings.snap01 = 1; setup.scale = { 0x0ab5, 0 };
    Voice a, b;
    GF_EXPECT (a.configure (setup, capture, marks.epochs, 48000, 345, 8000));
    GF_EXPECT (b.configure (setup, capture, marks.epochs, 48000, 345, 8000));
    for (int i = 0; i < 8000; ++i)
    {
        float al = 0, ar = 0, bl = 0, br = 0;
        a.render (al, ar); b.render (bl, br);
        GF_EXPECT (al == bl && ar == br && std::isfinite (al) && std::isfinite (ar));
        GF_EXPECT (a.activeGrains() <= kGrainCap && b.activeGrains() <= kGrainCap);
        if (i < 7999) GF_EXPECT (a.active());
    }
    GF_EXPECT (! a.active() && a.activeGrains() == 0 && a.stateBytes() <= 4096);
    float l = 0, r = 0; a.render (l, r);
    GF_EXPECT (l == 0 && r == 0);
}
GF_TEST (PitchSyncVoice, mono_is_equal_and_a_silent_stereo_channel_does_not_leak)
{
    auto source = tone (48000, 220, 15360);
    const std::vector<float> silence (15360, 0);
    PitchMarks finder; finder.prepare (48000);
    const auto marks = finder.find ({ source.data(), nullptr, (int) source.size(), 1 }, 220);
    Voice voice;
    GF_EXPECT (voice.configure (configured(), { silence.data(), source.data(), 15360, 2 }, marks.epochs, 48000, 4, 4096));
    double rightEnergy = 0;
    for (int i = 0; i < 4096; ++i)
    {
        float l = 0, r = 0; voice.render (l, r);
        GF_EXPECT (l == 0 && std::isfinite (r)); rightEnergy += (double) r * r;
    }
    GF_EXPECT (rightEnergy > 0.1);
}
GF_TEST (PitchSyncVoice, source_fault_is_silent_without_switching_to_another_engine_mid_fill)
{
    auto source = tone (48000, 220, 15360);
    Capture capture { source.data(), nullptr, (int) source.size(), 1 };
    PitchMarks finder; finder.prepare (48000);
    const auto marks = finder.find (capture, 220);
    Voice voice; GF_EXPECT (voice.configure (configured(), capture, marks.epochs, 48000, 1, 1024));
    std::fill (source.begin(), source.end(), std::numeric_limits<float>::quiet_NaN());
    float l = 0, r = 0; voice.render (l, r);
    GF_EXPECT (l == 0 && r == 0 && voice.faultCount() == 1 && voice.active());
    for (int i = 1; i < 1024; ++i) { l = r = 0; voice.render (l, r); GF_EXPECT (l == 0 && r == 0); }
    GF_EXPECT (! voice.active());
}
#if ! defined (GF_CONTEXT_STANDALONE)
GF_TEST (PitchSyncVoice, configuration_and_rendering_do_not_allocate_on_audio)
{
    auto source = tone (48000, 220, 15360);
    Capture capture { source.data(), nullptr, (int) source.size(), 1 };
    PitchMarks finder; finder.prepare (48000);
    const auto marks = finder.find (capture, 220);
    Voice voice; bool ready = false; long allocations = 0;
    {
        gftest::AllocGuard guard;
        ready = voice.configure (configured(), capture, marks.epochs, 48000, 1, 20000);
        for (int i = 0; i < 20000; ++i) { float l = 0, r = 0; voice.render (l, r); }
        voice.reset(); allocations = guard.count();
    }
    GF_EXPECT (ready && allocations == 0);
}
#endif
#if defined (GF_CONTEXT_STANDALONE)
int main()
{
    int failures = 0;
    for (const auto& [name, fn] : contexttest::cases())
        try { fn(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failures; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
    return failures != 0 ? 1 : 0;
}
#endif
