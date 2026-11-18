#include "../TestFramework.h"
#include "../TestSignals.h"
#include "../support/AllocGuard.h"
#include "features/tapedamage/TapeDamage.h"

#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace gapfill;
using namespace gapfill::tapedamage;
using namespace gftest;

namespace
{
    fx::TapeExtraSettings baseSettings()
    {
        fx::TapeExtraSettings settings;
        settings.amount01 = settings.mix01 = 1.0f;
        settings.wow01 = settings.flutter01 = settings.sat01 = settings.dropDensity01
            = settings.splice01 = settings.instab01 = 0.0f;
        return settings;
    }

    juce::AudioBuffer<float> render (TapeDamage& stage, const juce::AudioBuffer<float>& input, int block = 64)
    {
        auto output = makeSilence (2, input.getNumSamples());
        for (int pos = 0; pos < input.getNumSamples(); pos += block)
            for (int i = pos; i < std::min (input.getNumSamples(), pos + block); ++i)
            {
                auto l = input.getSample (0, i), r = input.getSample (1, i);
                stage.process (l, r);
                output.setSample (0, i, l);
                output.setSample (1, i, r);
            }
        return output;
    }

    bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < 2; ++ch)
            if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * (std::size_t) a.getNumSamples())) return false;
        return true;
    }
}

GF_TEST (TapeDamage, four_ms_latency_neutral_amount_and_absent_replay)
{
    for (const auto sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        TapeDamage stage;
        stage.prepare (sr);
        auto input = makeImpulse (2, (int) (sr * 0.1), 0);
        fx::TapeExtraSettings off;
        GF_EXPECT (! stage.begin (off, 42, input.getNumSamples(), 0));
        GF_EXPECT (identical (input, render (stage, input)));
        GF_EXPECT (! stage.begin (baseSettings(), 42, input.getNumSamples(), 0, {}, true));
        GF_EXPECT (stage.metadata() == TapeMeta {} && stage.latencySamples() == 0);
        GF_EXPECT (stage.begin (baseSettings(), 42, input.getNumSamples(), 0));
        const auto latency = (int) std::ceil (sr * 0.004);
        GF_EXPECT (stage.latencySamples() == latency && stage.sourceInputSamples() == input.getNumSamples() - latency);
        const auto wet = render (stage, input);
        int peak = 0;
        for (int i = 0; i < wet.getNumSamples(); ++i)
            if (std::abs (wet.getSample (0, i)) > std::abs (wet.getSample (0, peak))) peak = i;
        GF_EXPECT (peak == latency);
        for (int i = 0; i < latency; ++i) GF_EXPECT (wet.getSample (0, i) == 0.0f);
    }
}

GF_TEST (TapeDamage, short_gap_and_zero_mix_use_no_internal_delay)
{
    TapeDamage stage;
    stage.prepare (48000.0);
    auto settings = baseSettings();
    GF_EXPECT (! stage.begin (settings, 1, 1000, 0) && stage.skippedShort());
    GF_EXPECT (stage.metadata().version == 1 && stage.metadata().chosen == 0 && stage.latencySamples() == 0);
    GF_EXPECT (! stage.begin (settings, 1, 4000, 3900) && stage.skippedShort());
    settings.mix01 = 0.0f;
    GF_EXPECT (! stage.begin (settings, 1, 48000, 0) && stage.latencySamples() == 0);
}

GF_TEST (TapeDamage, canonical_metadata_replay_and_chunking_are_bit_exact)
{
    const auto input = makeNoise (48000.0, 1.0, 0.3f);
    fx::TapeExtraSettings settings;
    settings.amount01 = 1.0f;
    settings.wow01 = 0.345678f;
    settings.instab01 = 0.456789f;
    TapeDamage first;
    first.prepare (48000.0);
    GF_EXPECT (first.begin (settings, 0x123456789ull, input.getNumSamples(), 0));
    const auto meta = first.metadata();
    std::array<std::uint8_t, kTapeMetaBytes> bytes {};
    GF_EXPECT (encodeTapeMeta (meta, bytes));
    TapeMeta decoded;
    GF_EXPECT (decodeTapeMeta (bytes, decoded));
    const auto reference = render (first, input);
    for (const auto block : { 1, 13, 512, 8192 })
    {
        TapeDamage replay;
        replay.prepare (48000.0);
        GF_EXPECT (replay.begin ({}, 999, input.getNumSamples(), 0, decoded, true));
        GF_EXPECT (identical (reference, render (replay, input, block)));
    }
    decoded.chosen = 0;
    GF_EXPECT (! first.begin (settings, 123, input.getNumSamples(), 0, decoded, true));
    GF_EXPECT (first.metadata() == decoded && first.latencySamples() == 0);
}

GF_TEST (TapeDamage, wow_requested_pitch_depth_and_delay_budget)
{
    for (const auto sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const auto amount : { 0.3f, 1.0f })
        {
            TapeDamage stage;
            stage.prepare (sr);
            auto settings = baseSettings();
            settings.wow01 = amount;
            GF_EXPECT (stage.begin (settings, 7, (int) (sr * 3.0), 0));
            float previous = 0.0f;
            double peakCents = 0.0, delayPeak = 0.0;
            for (int sample = 0; sample < (int) (sr * 3.0); ++sample)
            {
                float l = 0.0f, r = 0.0f;
                stage.process (l, r);
                const auto delay = stage.lastWowDelaySamples();
                if (sample > (int) (sr * 0.1))
                    peakCents = std::max (peakCents, std::abs (1200.0 * std::log2 (1.0 - (double) (delay - previous))));
                delayPeak = std::max (delayPeak, std::abs ((double) delay));
                previous = delay;
            }
            std::printf ("         Tape wow sr%.0f depth%.1fc measured%.3fc rate%.3fHz\n", sr, amount*40, peakCents, stage.wowFrequencyHz());
            GF_EXPECT (std::abs (peakCents / (amount * 40.0) - 1.0) <= 0.1);
            GF_EXPECT (stage.wowFrequencyHz() >= 0.5f && stage.wowFrequencyHz() <= 2.0f);
            GF_EXPECT (delayPeak <= sr * 0.0024 * 1.001);
        }
}

GF_TEST (TapeDamage, flutter_requested_rms_pitch_depth)
{
    double energy = 0.0;
    std::uint64_t count = 0;
    for (std::uint64_t seed = 1; seed <= 32; ++seed)
    {
        TapeDamage stage;
        stage.prepare (48000.0);
        auto settings = baseSettings();
        settings.flutter01 = 1.0f;
        GF_EXPECT (stage.begin (settings, seed, 48000 * 4, 0));
        float previous = 0.0f;
        for (int sample = 0; sample < 48000 * 4; ++sample)
        {
            float l = 0.0f, r = 0.0f;
            stage.process (l, r);
            const auto delay = stage.lastFlutterDelaySamples();
            if (sample > 24000)
            {
                const auto cents = 1200.0 * std::log2 (1.0 - (double) (delay - previous));
                energy += cents*cents;
                ++count;
            }
            previous = delay;
        }
    }
    const auto rms = std::sqrt (energy / (double) count);
    std::printf ("         Tape flutter32seed RMS pitch %.4fc, target15c\n", rms);
    GF_EXPECT (std::abs (rms / 15.0 - 1.0) <= 0.1);
}

GF_TEST (TapeDamage, event_table_ramps_bounds_and_allocation_free_processing)
{
    TapeDamage stage;
    stage.prepare (192000.0);
    auto settings = baseSettings();
    settings.wow01 = settings.flutter01 = settings.sat01 = settings.dropDensity01
        = settings.dropDepth01 = settings.splice01 = settings.instab01 = 1.0f;
    AllocGuard guard;
    GF_EXPECT (stage.begin (settings, 32, 192000 * 2, 0));
    GF_EXPECT (stage.eventCount() <= TapeDamage::kMaxEvents);
    for (int i = 0; i < stage.eventCount(); ++i)
    {
        const auto& event = stage.eventAt (i);
        GF_EXPECT (event.end > event.start && event.ramp >= 192);
        if (i > 0) GF_EXPECT (event.start >= stage.eventAt (i-1).start);
    }
    for (int i = 0; i < 192000 * 2; ++i)
    {
        float l = i % 2 ? -0.5f : 0.5f, r = -l;
        stage.process (l, r);
        GF_EXPECT (std::isfinite (l) && std::isfinite (r) && std::abs (l) <= 3.98f && std::abs (r) <= 3.98f);
        GF_EXPECT (std::abs (stage.lastDriftDelaySamples()) <= 192000.0f * 0.00035f * 1.001f);
        GF_EXPECT (stage.lastReadDelaySamples() >= 192000.0f * 0.0005f - 0.01f);
        GF_EXPECT (stage.lastReadDelaySamples() <= 192000.0f * 0.0075f + 0.01f);
    }
    GF_EXPECT (guard.count() == 0);
}

GF_TEST (Perf, tape_four_active_instances_cpu_budget)
{
    constexpr int frames = 48000, warm = 4800, repeats = 12;
    std::array<TapeDamage, 4> stages;
    for (auto& stage : stages) stage.prepare (48000.0);
    auto settings = baseSettings();
    settings.wow01 = settings.flutter01 = settings.sat01 = settings.dropDensity01
        = settings.dropDepth01 = settings.splice01 = settings.instab01 = 1.0f;
    auto input = makeSilence (2, frames + warm);
    for (int i = 0; i < input.getNumSamples(); ++i)
    {
        input.setSample (0, i, 0.25f * (float) std::sin (i * 0.13) + 0.15f * (float) std::sin (i * 0.031));
        input.setSample (1, i, 0.21f * (float) std::cos (i * 0.017));
    }
    double cpuSeconds = 0.0, checksum = 0.0;
    juce::ScopedNoDenormals denormals;
    for (int repeat = 0; repeat < repeats; ++repeat)
    {
        for (std::size_t voice = 0; voice < stages.size(); ++voice)
            GF_EXPECT (stages[voice].begin (settings, 100 + voice, frames + warm, 0));
        for (int i = 0; i < warm; ++i)
            for (auto& stage : stages)
            {
                auto l = input.getSample (0, i), r = input.getSample (1, i);
                stage.process (l, r);
                checksum += l + r;
            }
        const auto start = std::clock();
        for (int i = warm; i < frames + warm; ++i)
            for (auto& stage : stages)
            {
                auto l = input.getSample (0, i), r = input.getSample (1, i);
                stage.process (l, r);
                checksum += l + r;
            }
        cpuSeconds += (double) (std::clock() - start) / CLOCKS_PER_SEC;
    }
    const auto ratio = cpuSeconds / repeats;
    std::printf ("         Four active Tape Damage instances CPU %.4f%% of one core checksum %.8g\n", ratio*100.0, checksum);
    if (const auto* enforce = std::getenv ("GAPFILL_PERF_ENFORCE"); enforce != nullptr && juce::String (enforce) == "1")
        GF_EXPECT (ratio <= 0.006);
}

GF_TEST (TapeDamage, saturation_gain_dc_and_silence_are_bounded)
{
    for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        TapeDamage stage;
        stage.prepare (sr);
        auto settings = baseSettings(); settings.sat01 = 1.0f;
        const int count = (int) sr;
        for (const float amplitude : { 0.001f, 0.1f, 0.5f, 1.0f })
        {
            GF_EXPECT (stage.begin (settings, 112, count, 0));
            double peak = 0, inputEnergy = 0, outputEnergy = 0, mean = 0;
            for (int i = 0; i < count; ++i)
            {
                const float input = amplitude * (float) std::sin (6.283185307179586 * 1000 * i / sr);
                float l = input, r = input;
                stage.process (l, r);
                GF_EXPECT (std::isfinite (l) && l == r);
                if (i > count / 2)
                {
                    peak = std::max (peak, std::abs ((double) l));
                    inputEnergy += (double) input * input; outputEnergy += (double) l * l;
                    mean += l;
                }
            }
            GF_EXPECT (peak <= amplitude * 1.412538);
            GF_EXPECT (outputEnergy <= inputEnergy * 1.995263);
            GF_EXPECT (std::abs (mean / (count - count / 2 - 1)) < 1e-3);
        }
        GF_EXPECT (stage.begin (settings, 13, count, 0));
        double finalDc = 0;
        for (int i = 0; i < count; ++i)
        {
            float l = 0.5f, r = -0.5f; stage.process (l, r);
            if (i > count * 3 / 4) finalDc = std::max (finalDc, std::abs ((double) l));
        }
        GF_EXPECT (finalDc < 1e-3);
        settings.wow01 = settings.flutter01 = settings.instab01 = settings.dropDensity01 = settings.splice01 = 1;
        GF_EXPECT (stage.begin (settings, 13, count, 0));
        for (int i = 0; i < count; ++i)
        {
            float l = 0, r = 0; stage.process (l, r);
            GF_EXPECT (l == 0 && r == 0);
        }
    }
}

GF_TEST (TapeDamage, dropout_and_splice_edges_obey_a_signal_derived_step_bound)
{
    for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const bool splice : { false, true })
        {
            const int count = (int) sr;
            double maximumStep = 0;
            int events = 0;
            for (std::uint64_t seed = 1; seed <= 16; ++seed)
            {
                TapeDamage stage; stage.prepare (sr);
                auto settings = baseSettings(); settings.dropDepth01 = 1;
                if (splice) settings.splice01 = 1; else settings.dropDensity01 = 1;
                GF_EXPECT (stage.begin (settings, seed, count, 0));
                events += stage.eventCount();
                TapeDamage same; same.prepare (sr);
                GF_EXPECT (same.begin (settings, seed, count, 0));
                GF_EXPECT (stage.eventCount() == same.eventCount());
                for (int e = 0; e < stage.eventCount(); ++e) GF_EXPECT (stage.eventAt (e) == same.eventAt (e));
                float previous = 0;
                for (int i = 0; i < count; ++i)
                {
                    float l = 0.5f * (float) std::sin (6.283185307179586 * 500 * i / sr), r = l;
                    stage.process (l, r);
                    if (i > (int) (sr * 0.05)) maximumStep = std::max (maximumStep, std::abs ((double) l - previous));
                    previous = l;
                }
            }
            // A500Hz,0.5-amplitude source contributes at most2*A*sin(pi*f/sr)
            // per sample. Equal-power taps add at most sqrt(2); two 1ms crossfade
            // derivatives contribute at most pi*A/ramp. The10Hz DC guard and
            // interpolation roundoff have a small explicit0.1%-amplitude margin.
            const auto ramp = std::ceil (sr * 0.001);
            const auto bound = 2 * 0.5 * std::sin (3.141592653589793 * 500 / sr) * 1.414214
                             + 3.141592653589793 * 0.5 / ramp + 0.0005;
            std::printf ("         Tape %s sr%.0f max-step %.6f bound %.6f events %d\n", splice ? "splice" : "drop", sr, maximumStep, bound, events);
            GF_EXPECT (events > 0 && maximumStep <= bound);
        }
}

GF_TEST (TapeDamage, saturation_render_matches_the_level_compensated_tanh_oracle)
{
    for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        constexpr int count = 20000;
        std::vector<float> input ((std::size_t) count);
        for (int i = 0; i < count; ++i)
            input[(std::size_t) i] = 0.4f * (float) std::sin (i * 0.71) + 0.2f * (float) std::cos (i * 0.017);
        TapeDamage stage;
        stage.prepare (sr);
        auto settings = baseSettings(); settings.sat01 = 1;
        GF_EXPECT (stage.begin (settings, 211, count, 0));
        const int delay = stage.latencySamples();
        const float emphasis = (float) std::exp (-6.283185307179586 * 3000 / sr);
        const float dc = (float) std::exp (-6.283185307179586 * 10 / sr);
        float previousInput = 0, deemphasised = 0, previousDeemphasis = 0, highpassed = 0;
        double maximumError = 0;
        for (int i = 0; i < count; ++i)
        {
            float l = input[(std::size_t) i], r = l;
            stage.process (l, r);
            if (i >= delay)
            {
                const auto delayed = input[(std::size_t) (i - delay)];
                const auto preemphasised = delayed - emphasis * previousInput;
                previousInput = delayed;
                const auto shaped = (float) std::tanh ((double) std::clamp (6 * preemphasised, -5.0f, 5.0f)) / 6;
                deemphasised = shaped + emphasis * deemphasised;
                highpassed = deemphasised - previousDeemphasis + dc * highpassed;
                previousDeemphasis = deemphasised;
            }
            maximumError = std::max (maximumError, std::abs ((double) l - highpassed));
        }
        std::printf ("         Tape tanh oracle sr%.0f maximum absolute error %.8g\n", sr, maximumError);
        GF_EXPECT (maximumError < 2e-4);
    }
}

GF_TEST (TapeDamage, live_carrier_send_keeps_raw_mix_replay_and_zero_gate_separate)
{
    auto settings = baseSettings();
    settings.mix01 = 0.8f; settings.sat01 = 0.7f; settings.wow01 = 0.3f;
    const auto input = makeNoise (48000, 0.2, 0.3f);
    TapeDamage original, replay, muted;
    original.prepare (48000); replay.prepare (48000); muted.prepare (48000);
    GF_EXPECT (original.begin (settings, 772, input.getNumSamples(), 0, {}, false, 0.25f));
    const auto decision = original.metadata();
    GF_EXPECT (decision.chosen == 1 && decision.values[Mix] == 8000);
    GF_EXPECT (replay.begin ({}, 999, input.getNumSamples(), 0, decision, true, 0.25f));
    GF_EXPECT (identical (render (original, input, 1), render (replay, input, 13)));
    GF_EXPECT (! muted.begin ({}, 111, input.getNumSamples(), 0, decision, true, 0.0f));
    GF_EXPECT (muted.metadata() == decision && muted.latencySamples() == 0 && ! muted.isActive());
    GF_EXPECT (identical (input, render (muted, input)));
    GF_EXPECT (! muted.begin (settings, 111, input.getNumSamples(), 0, {}, false, 0.0f));
    GF_EXPECT (muted.metadata() == TapeMeta {} && muted.latencySamples() == 0);
    GF_EXPECT (! muted.begin (settings, 111, input.getNumSamples(), 0, {}, false,
                             std::numeric_limits<float>::quiet_NaN()));
    GF_EXPECT (muted.metadata() == TapeMeta {});
    GF_EXPECT (muted.begin (settings, 772, input.getNumSamples(), 0, decision, true, 1.0f));
    GF_EXPECT (muted.metadata() == decision && muted.latencySamples() == 192);
    muted.reset();
    GF_EXPECT (muted.latencySamples() == 0 && muted.metadata() == TapeMeta {});
}
