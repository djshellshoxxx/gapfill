#include "../support/AllocGuard.h"
#include "../support/TestRig.h"
#include "../support/VoiceRig.h"
#include "features/formant/FormantPool.h"
#include "features/formant/Vowels.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iostream>
#include <limits>

using namespace gapfill;
using namespace gapfill::formant;
using namespace gftest;

namespace
{
    FormantSetup active (float shift = 0.0f, int vowel = 0)
    {
        FormantSetup setup;
        setup.enabled = true;
        setup.amount01 = 1.0f;
        setup.shiftSt = shift;
        setup.vowel = vowel;
        return setup;
    }

    juce::AudioBuffer<float> process (const juce::AudioBuffer<float>& input, double sr, const FormantSetup& setup)
    {
        FormantProcessor processor;
        processor.prepare (sr);
        GF_EXPECT (processor.claim (setup));
        juce::AudioBuffer<float> out (2, input.getNumSamples() + processor.latencySamples());
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            float l = i < input.getNumSamples() ? input.getSample (0, i) : 0.0f;
            float r = i < input.getNumSamples() ? input.getSample (1, i) : 0.0f;
            processor.process (l, r);
            out.setSample (0, i, l);
            out.setSample (1, i, r);
        }
        return out;
    }

    juce::AudioBuffer<float> saw (double sr)
    {
        auto signal = makeSilence (2, (int) sr);
        for (int i = 0; i < signal.getNumSamples(); ++i)
        {
            double value = 0.0;
            for (int harmonic = 1; harmonic <= 32; ++harmonic)
                value += std::sin (juce::MathConstants<double>::twoPi * 220.0 * harmonic * i / sr) / harmonic;
            signal.setSample (0, i, (float) (0.25 * value));
            signal.setSample (1, i, (float) (0.25 * value));
        }
        return signal;
    }

    juce::AudioBuffer<float> pink (double sr)
    {
        auto signal = makeNoise (sr, 0.75, 0.1f);
        for (int ch = 0; ch < 2; ++ch)
        {
            float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
            for (int i = 0; i < signal.getNumSamples(); ++i)
            {
                const float white = signal.getSample (ch, i);
                b0 = 0.99886f * b0 + white * 0.0555179f;
                b1 = 0.99332f * b1 + white * 0.0750759f;
                b2 = 0.96900f * b2 + white * 0.1538520f;
                b3 = 0.86650f * b3 + white * 0.3104856f;
                b4 = 0.55000f * b4 + white * 0.5329522f;
                b5 = -0.7616f * b5 - white * 0.0168980f;
                signal.setSample (ch, i, 0.11f * (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f));
                b6 = white * 0.115926f;
            }
        }
        return signal;
    }

    double fundamental (const juce::AudioBuffer<float>& signal, double sr, int from)
    {
        const int nominal = (int) std::lround (sr / 220.0), count = 8192;
        auto correlation = [&] (int lag)
        {
            double result = 0.0;
            for (int i = from; i < from + count; ++i)
                result += (double) signal.getSample (0, i) * signal.getSample (0, i + lag);
            return result;
        };
        int peak = nominal;
        double best = correlation (peak);
        for (int lag = nominal - 3; lag <= nominal + 3; ++lag)
            if (const auto score = correlation (lag); score > best) { best = score; peak = lag; }
        const double left = correlation (peak - 1), right = correlation (peak + 1);
        const double fractional = 0.5 * (left - right) / (left - 2.0 * best + right);
        return sr / (peak + fractional);
    }

    double centroid (const juce::AudioBuffer<float>& signal, double sr, int from)
    {
        constexpr int n = 8192;
        juce::dsp::FFT fft (13);
        std::vector<float> values ((std::size_t) n * 2, 0.0f);
        for (int i = 0; i < n; ++i)
            values[(std::size_t) i] = signal.getSample (0, from + i)
                * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / n));
        fft.performFrequencyOnlyForwardTransform (values.data());
        double sum = 0, weighted = 0;
        for (int k = 1; k <= n / 2; ++k)
        {
            sum += values[(std::size_t) k];
            weighted += values[(std::size_t) k] * sr * k / n;
        }
        return weighted / std::max (sum, 1.0e-30);
    }
}

GF_TEST (Formant, sample_rate_table_and_exact_identity_latency)
{
    for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const int latency = FormantProcessor::sizeForRate (sr);
        const auto input = makeNoise (sr, 0.15, 0.1f);
        const auto out = process (input, sr, active());
        double error = 0.0, energy = 0.0;
        for (int i = latency; i < input.getNumSamples(); ++i)
        {
            const double x = input.getSample (0, i), y = out.getSample (0, i + latency);
            error += (x - y) * (x - y);
            energy += x * x;
        }
        GF_EXPECT (10.0 * std::log10 (std::max (error, 1.0e-30) / energy) < -80.0);
    }
}

GF_TEST (Formant, click_offset_and_pre_echo)
{
    for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const int latency = FormantProcessor::sizeForRate (sr), at = latency * 3;
        auto setup = active (7.0f, 1);
        setup.preserve01 = 0.0f;
        const auto input = makeImpulse (2, latency * 8, at);
        const auto out = process (input, sr, setup);
        int peak = 0;
        for (int i = 1; i < out.getNumSamples(); ++i)
            if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, peak))) peak = i;
        GF_EXPECT (std::abs (peak - at - latency) <= 1);
        GF_EXPECT (maxAbs (out, 0, peak) < std::pow (10.0f, -35.0f / 20.0f));
    }
}

GF_TEST (Formant, pitch_preserved_within_three_cents_and_centroid_rises)
{
    for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto input = saw (sr);
        const int latency = FormantProcessor::sizeForRate (sr);
        const auto out = process (input, sr, active (7.0f));
        const double hz = fundamental (out, sr, 8192 + latency);
        const double cents = 1200.0 * std::log2 (hz / 220.0);
        const double ratio = centroid (out, sr, 8192 + latency) / centroid (input, sr, 8192);
        std::cout << "  formant sr=" << sr << " pitch cents=" << cents << " centroid ratio=" << ratio << std::endl;
        GF_EXPECT (std::abs (cents) <= 3.0);
        const double expectedIncrease = std::exp2 (7.0 / 12.0) - 1.0;
        GF_EXPECT (ratio >= 1.0 + 0.3 * expectedIncrease && ratio <= 1.0 + 0.6 * expectedIncrease);
    }
}

GF_TEST (Formant, pink_noise_level_stays_within_one_point_five_db)
{
    double worst = 0.0;
    juce::String context;
    for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto input = pink (sr);
        const int latency = FormantProcessor::sizeForRate (sr), from = latency * 4;
        const int count = input.getNumSamples() - from;
        for (int vowel = 0; vowel <= 5; ++vowel)
        {
            auto setup = active (vowel % 2 == 0 ? -12.0f : 12.0f, vowel);
            setup.tiltDbPerOct = vowel % 2 == 0 ? -6.0f : 6.0f;
            setup.preserve01 = vowel == 0 ? 1.0f : 0.0f;
            const auto out = process (input, sr, setup);
            const double db = 20.0 * std::log10 (rmsOf (out, from + latency, count) / rmsOf (input, from, count));
            if (std::abs (db) > worst)
            {
                worst = std::abs (db);
                context = "sr=" + juce::String (sr) + " vowel=" + juce::String (vowel) + " dB=" + juce::String (db);
            }
        }
    }
    std::cout << "  worst pink-noise level: " << context << std::endl;
    GF_EXPECT_MSG (worst <= 1.5, context);
}

GF_TEST (Formant, pool_is_neutral_when_off_and_falls_back_when_short_or_busy)
{
    FormantPool pool;
    pool.prepare (48000.0);
    GF_EXPECT (pool.request ({}, 10000, 1000) == nullptr && pool.claimCount() == 0);
    GF_EXPECT (pool.request (active(), 1000, 500) == nullptr && pool.skippedShort() == 1);
    auto* first = pool.request (active(), 10000, 1000);
    auto* second = pool.request (active(), 10000, 1000);
    GF_EXPECT (first != nullptr && second != nullptr && first != second);
    GF_EXPECT (pool.request (active(), 10000, 1000) == nullptr && pool.skippedBusy() == 1);
    first->release();
    GF_EXPECT (pool.request (active(), 10000, 1000) == first);
    pool.reset();
    GF_EXPECT (pool.claimCount() == 0 && pool.skippedShort() == 0 && pool.skippedBusy() == 0);
}

GF_TEST (Formant, phase_coherence_and_non_finite_recovery)
{
    auto input = makeNoise (48000.0, 0.2, 0.1f);
    for (int i = 0; i < input.getNumSamples(); ++i) input.setSample (1, i, -input.getSample (0, i));
    const auto out = process (input, 48000.0, active (7.0f, 2));
    for (int i = 0; i < out.getNumSamples(); ++i)
        GF_EXPECT_NEAR (out.getSample (0, i), -out.getSample (1, i), 1.0e-5);
    FormantProcessor processor;
    processor.prepare (48000.0);
    GF_EXPECT (processor.claim (active()));
    float left = std::numeric_limits<float>::quiet_NaN(), right = 0.0f;
    processor.process (left, right);
    GF_EXPECT (left == 0.0f && right == 0.0f && processor.faultCount() == 1);
}

GF_TEST (Formant, vowel_peaks_follow_tabulated_first_two_resonances)
{
    constexpr int size = 4096;
    const auto input = makeNoise (48000.0, 0.75, 0.1f);
    for (int vowel = 1; vowel <= 5; ++vowel)
    {
        auto setup = active (0.0f, vowel);
        setup.preserve01 = 0.0f;
        const auto out = process (input, 48000.0, setup);
        juce::dsp::FFT transform (12);
        std::vector<float> values ((std::size_t) size * 2), power ((std::size_t) size / 2 + 1, 0.0f);
        for (int from = size; from + size < out.getNumSamples(); from += size / 2)
        {
            for (int i = 0; i < size; ++i)
                values[(std::size_t) i] = out.getSample (0, from + i)
                    * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / size));
            std::fill (values.begin() + size, values.end(), 0.0f);
            transform.performFrequencyOnlyForwardTransform (values.data());
            for (int k = 0; k <= size / 2; ++k) power[(std::size_t) k] += values[(std::size_t) k] * values[(std::size_t) k];
        }
        for (int resonance = 0; resonance < 2; ++resonance)
        {
            const double expected = vowelFormants (vowel)[(std::size_t) resonance];
            const int first = (int) std::floor (expected * 0.8 * size / 48000.0);
            const int last = (int) std::ceil (expected * 1.2 * size / 48000.0);
            int peak = first;
            for (int k = first; k <= last; ++k)
                if (power[(std::size_t) k] > power[(std::size_t) peak]) peak = k;
            const double observed = (double) peak * 48000.0 / size;
            GF_EXPECT_MSG (std::abs (observed / expected - 1.0) <= 0.1,
                           "vowel=" + juce::String (vowel) + " F" + juce::String (resonance + 1) + " Hz=" + juce::String (observed));
        }
    }
}

GF_TEST (Formant, processing_and_pool_claims_do_not_allocate)
{
    FormantPool pool;
    pool.prepare (192000.0);
    long allocations = 0;
    {
        AllocGuard guard;
        auto* processor = pool.request (active (12.0f, 4), 40000, 1000);
        for (int i = 0; i < 20000; ++i)
        {
            float l = 0.1f * std::sin ((float) i * 0.13f), r = l;
            processor->process (l, r);
        }
        processor->release();
        allocations = guard.count();
    }
    GF_EXPECT (allocations == 0);
}

GF_TEST (Formant, extreme_controls_are_bounded_and_repeatable)
{
    const auto input = makeNoise (48000.0, 0.2, 0.1f);
    auto setup = active (12.0f, 5);
    setup.tiltDbPerOct = -6.0f;
    setup.preserve01 = 0.0f;
    setup.detail = 2;
    const auto a = process (input, 48000.0, setup);
    const auto b = process (input, 48000.0, setup);
    GF_EXPECT (buffersIdentical (a, b) && allFinite (a));
    GF_EXPECT (maxAbs (a) < 3.98f);
    auto silence = makeSilence (2, 4096);
    const auto quiet = process (silence, 48000.0, setup);
    GF_EXPECT (maxAbs (quiet) == 0.0f);
}

GF_TEST (Formant, independent_channels_and_dc_stay_finite)
{
    auto signal = makeNoise (48000.0, 0.2, 0.1f);
    signal.clear (0, 0, signal.getNumSamples());
    auto setup = active (7.0f, 3);
    setup.preserve01 = 0.5f;
    const auto out = process (signal, 48000.0, setup);
    float leaked = 0.0f;
    for (int i = 0; i < out.getNumSamples(); ++i) leaked = std::max (leaked, std::abs (out.getSample (0, i)));
    GF_EXPECT (leaked < 1.0e-5f && rmsOf (out, 2048, 4096, 1) > 0.001);
    for (int i = 0; i < signal.getNumSamples(); ++i)
    {
        signal.setSample (0, i, 0.25f);
        signal.setSample (1, i, i % 2 == 0 ? 0.25f : -0.25f);
    }
    const auto dc = process (signal, 48000.0, setup);
    GF_EXPECT (allFinite (dc) && maxAbs (dc) < 3.98f);
}

GF_TEST (Perf, formant_two_processors_cpu_budget)
{
    // The real host guards processBlock once, not once per voice sample.
    juce::ScopedNoDenormals noDenormals;
    std::array<FormantProcessor, 2> processors;
    auto worst = active (12.0f, 2);
    worst.preserve01 = 0.5f;
    worst.tiltDbPerOct = 6.0f;
    worst.detail = 2;
    for (auto& processor : processors)
    {
        processor.prepare (48000.0);
        GF_EXPECT (processor.claim (worst));
    }
    const auto input = makeNoise (48000.0, 1.0, 0.1f);
    const auto started = juce::Time::getHighResolutionTicks();
    const auto cpuStarted = std::clock();
    float sink = 0.0f;
    for (int i = 0; i < input.getNumSamples(); ++i)
        for (auto& processor : processors)
        {
            float l = input.getSample (0, i), r = input.getSample (1, i);
            processor.process (l, r);
            sink += l;
        }
    const double wall = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - started);
    const double elapsed = (double) (std::clock() - cpuStarted) / CLOCKS_PER_SEC;
    std::cout << "  two formant processors CPU=" << elapsed * 100.0 << "% of one core, wall=" << wall << " s, checksum=" << sink << std::endl;
    if (juce::SystemStats::getEnvironmentVariable ("GAPFILL_PERF_ENFORCE", "0") == "1")
        GF_EXPECT (elapsed < 0.03);
}
