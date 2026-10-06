#include "TestFramework.h"
#include "TestSignals.h"

#include "dsp/DspUtils.h"
#include "dsp/AnalysisEngine.h"
#include "dsp/SpectralSmear.h"
#include "dsp/FillVoice.h"
#include "dsp/FillEngine.h"

using namespace gapfill;
using namespace gftest;

namespace
{
    AnalysisSettings defaultAnalysisSettings()
    {
        AnalysisSettings s;
        s.mode = DetMode::Transient;
        s.source = DetSource::Main;
        s.sensitivity01 = 0.55f;
        s.gapThresh01 = 0.55f;
        s.protectHoldMs = 30.0f;
        s.retrigGuardMs = 30.0f;
        s.gapReleaseMs = 50.0f;
        return s;
    }

    /** Fills a SourceHistory with a ramp so reads are trivially verifiable. */
    void fillHistoryWithRamp (SourceHistory& history, int numSamples, int blockSize = 128)
    {
        juce::AudioBuffer<float> block (2, blockSize);
        int written = 0;

        while (written < numSamples)
        {
            const auto n = juce::jmin (blockSize, numSamples - written);

            for (int i = 0; i < n; ++i)
            {
                block.setSample (0, i, (float) (written + i));
                block.setSample (1, i, (float) (written + i) * -1.0f);
            }

            history.write (block, 0, n);
            written += n;
        }
    }
}

//==============================================================================
GF_TEST (SourceHistory, ramp_read_is_exact_inside_window)
{
    SourceHistory history;
    history.prepare (2, 4096);
    fillHistoryWithRamp (history, 10000);

    GF_EXPECT (history.getWritePosition() == 10000);

    // Newest sample
    GF_EXPECT_NEAR (history.read (0, 9999.0), 9999.0, 0.001);
    GF_EXPECT_NEAR (history.read (1, 9999.0), -9999.0, 0.001);

    // Somewhere in the middle of the valid window
    GF_EXPECT_NEAR (history.read (0, 8000.0), 8000.0, 0.001);

    // Fractional read interpolates
    GF_EXPECT_NEAR (history.read (0, 8000.5), 8000.5, 0.001);
}

GF_TEST (SourceHistory, reads_outside_window_are_clamped_not_garbage)
{
    SourceHistory history;
    history.prepare (2, 4096);
    fillHistoryWithRamp (history, 10000);

    // Far in the past: clamped to the oldest valid sample, never wrapped garbage.
    const auto oldest = history.read (0, 0.0);
    GF_EXPECT (std::isfinite (oldest));
    GF_EXPECT (oldest >= 10000.0f - 4096.0f && oldest <= 10000.0f);

    // In the future: clamped to the newest sample.
    GF_EXPECT_NEAR (history.read (0, 50000.0), 9999.0, 1.0);
}

GF_TEST (SourceHistory, wraparound_is_continuous_across_many_writes)
{
    SourceHistory history;
    history.prepare (2, 1024);

    // Write far more than the buffer holds, in awkward block sizes.
    juce::AudioBuffer<float> block (2, 37);
    int written = 0;

    for (int iteration = 0; iteration < 300; ++iteration)
    {
        for (int i = 0; i < 37; ++i)
        {
            block.setSample (0, i, std::sin (0.01f * (float) (written + i)));
            block.setSample (1, i, std::cos (0.01f * (float) (written + i)));
        }

        history.write (block, 0, 37);
        written += 37;
    }

    // The most recent 900 samples must match the generator exactly.
    for (int back = 1; back <= 900; ++back)
    {
        const auto pos = written - back;
        GF_EXPECT_NEAR (history.read (0, (double) pos), std::sin (0.01f * (float) pos), 1.0e-5);
        GF_EXPECT_NEAR (history.read (1, (double) pos), std::cos (0.01f * (float) pos), 1.0e-5);
    }
}

GF_TEST (SourceHistory, copyTo_matches_interpolated_reads)
{
    SourceHistory history;
    history.prepare (2, 8192);
    fillHistoryWithRamp (history, 20000);

    juce::AudioBuffer<float> dest (2, 512);
    history.copyTo (dest, 19000, 512);

    for (int i = 0; i < 512; ++i)
    {
        GF_EXPECT_NEAR (dest.getSample (0, i), 19000.0 + i, 0.001);
        GF_EXPECT_NEAR (dest.getSample (1, i), -(19000.0 + i), 0.001);
    }

    // A request that reaches past the newest sample is zero-padded, not garbage.
    juce::AudioBuffer<float> dest2 (2, 512);
    history.copyTo (dest2, 19800, 512);

    for (int i = 0; i < 200; ++i)
        GF_EXPECT_NEAR (dest2.getSample (0, i), 19800.0 + i, 0.001);

    for (int i = 210; i < 512; ++i)
        GF_EXPECT_NEAR (dest2.getSample (0, i), 0.0, 0.001);
}

//==============================================================================
GF_TEST (EventQueue, ordering_and_discard)
{
    EventQueue<16> q;
    q.clear();

    for (int i = 0; i < 8; ++i)
        q.push (i * 100);

    GF_EXPECT (q.nextAtOrAfter (0) == 0);
    GF_EXPECT (q.nextAtOrAfter (150) == 200);
    GF_EXPECT (q.nextAtOrAfter (701) == -1);
    GF_EXPECT (q.nextAtOrAfter (800) == -1);

    q.discardBefore (400);
    GF_EXPECT (q.nextAtOrAfter (0) == 400);

    // Overflow keeps the newest entries rather than corrupting the queue.
    for (int i = 0; i < 100; ++i)
        q.push (10000 + i);

    GF_EXPECT (q.nextAtOrAfter (0) >= 10000);
    GF_EXPECT (q.nextAtOrAfter (10099) == 10099);
}

GF_TEST (Rng, deterministic_and_seed_sensitive)
{
    Rng a, b, c;
    a.seed (1234);
    b.seed (1234);
    c.seed (1235);

    bool sameAB = true, sameAC = true;

    for (int i = 0; i < 64; ++i)
    {
        const auto va = a.nextFloat();
        const auto vb = b.nextFloat();
        const auto vc = c.nextFloat();

        if (va != vb) sameAB = false;
        if (va != vc) sameAC = false;

        GF_EXPECT (va >= 0.0f && va < 1.0f);
    }

    GF_EXPECT_MSG (sameAB, "the same seed must produce the same sequence");
    GF_EXPECT_MSG (! sameAC, "different seeds must produce different sequences");
}

//==============================================================================
GF_TEST (BandSplitter, three_bands_preserve_energy)
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        BandSplitter split;
        split.prepare (sr, 1);
        split.setCrossovers (180.0f, 3500.0f);

        auto input = makeNoise (sr, 0.5, 0.3f, 1);
        double inputEnergy = 0.0, sumEnergy = 0.0;

        const auto* d = input.getReadPointer (0);
        const auto skip = (int) (sr * 0.05);   // let the filters settle

        for (int i = 0; i < input.getNumSamples(); ++i)
        {
            float lo, mi, hi;
            split.processMono (d[i], lo, mi, hi);

            if (i < skip)
                continue;

            const auto sum = lo + mi + hi;
            inputEnergy += (double) d[i] * d[i];
            sumEnergy += (double) sum * sum;

            GF_EXPECT (std::isfinite (sum));
        }

        // Linkwitz-Riley reconstruction is all-pass, so energy must be preserved.
        const auto ratio = sumEnergy / juce::jmax (1.0e-9, inputEnergy);
        GF_EXPECT_MSG (ratio > 0.95 && ratio < 1.05,
                       "band sum energy ratio at " + juce::String (sr) + " Hz = " + juce::String (ratio));
    }
}

GF_TEST (BandSplitter, kick_energy_lands_in_the_low_band)
{
    constexpr double sr = 48000.0;
    BandSplitter split;
    split.prepare (sr, 1);
    split.setCrossovers (180.0f, 3500.0f);

    auto input = makeLowKicksWithOpenHighs (sr, 1.0, 120.0, 1);
    double lowE = 0.0, midE = 0.0, highE = 0.0;
    const auto* d = input.getReadPointer (0);

    for (int i = 0; i < input.getNumSamples(); ++i)
    {
        float lo, mi, hi;
        split.processMono (d[i], lo, mi, hi);
        lowE += (double) lo * lo;
        midE += (double) mi * mi;
        highE += (double) hi * hi;
    }

    GF_EXPECT_MSG (lowE > midE * 50.0, "48 Hz kicks must dominate the low band");
    GF_EXPECT_MSG (lowE > highE * 500.0, "48 Hz kicks must barely reach the high band");
}

//==============================================================================
GF_TEST (AnalysisEngine, detects_synthetic_transients_at_the_right_positions)
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        AnalysisEngine engine;
        engine.prepare (sr, 512);

        const double bpm = 120.0;
        const auto beat = (int) (60.0 / bpm * sr);
        auto loop = makeDrumLoop (sr, 3.0, bpm, 1);

        auto settings = defaultAnalysisSettings();
        std::vector<float> mono ((size_t) loop.getNumSamples());

        for (int i = 0; i < loop.getNumSamples(); ++i)
            mono[(size_t) i] = loop.getSample (0, i);

        engine.processBlock (mono.data(), nullptr, loop.getNumSamples(), settings);

        // Every beat (and every off-beat hat) should be found, within one hop of
        // its true position plus the detector's short attack window.
        int matched = 0;
        const auto tolerance = (std::int64_t) (sr * 0.02);

        for (int beatIndex = 1; beatIndex * beat < loop.getNumSamples() - beat; ++beatIndex)
        {
            const auto expected = (std::int64_t) beatIndex * beat;
            const auto found = engine.nextMainEvent (expected - tolerance);

            if (found >= 0 && std::abs (found - expected) <= tolerance)
                ++matched;
        }

        GF_EXPECT_MSG (matched >= 3, "at " + juce::String (sr) + " Hz only matched "
                                        + juce::String (matched) + " beats");
    }
}

GF_TEST (AnalysisEngine, gap_confidence_is_high_between_hits_and_low_on_them)
{
    constexpr double sr = 48000.0;
    AnalysisEngine engine;
    engine.prepare (sr, 512);

    const double bpm = 100.0;
    const auto beat = (int) (60.0 / bpm * sr);

    // Kicks with long clean gaps.
    auto loop = makeSilence (1, (int) (sr * 3.0));

    for (int b = 0; b * beat < loop.getNumSamples(); ++b)
        addDrumHit (loop, b * beat, sr, 55.0f, 0.05f, 0.9f, 0.05f);

    auto settings = defaultAnalysisSettings();
    std::vector<float> mono ((size_t) loop.getNumSamples());

    for (int i = 0; i < loop.getNumSamples(); ++i)
        mono[(size_t) i] = loop.getSample (0, i);

    engine.processBlock (mono.data(), nullptr, loop.getNumSamples(), settings);

    // Sample the confidence right on a hit and in the middle of a gap.
    const auto onHit = engine.frameAt ((std::int64_t) (4 * beat + sr * 0.004)).conf;
    const auto inGap = engine.frameAt ((std::int64_t) (4 * beat + beat / 2)).conf;

    GF_EXPECT_MSG (onHit < 0.3f, "confidence on the hit was " + juce::String (onHit));
    GF_EXPECT_MSG (inGap > 0.7f, "confidence in the gap was " + juce::String (inGap));
}

GF_TEST (AnalysisEngine, spectral_mode_finds_space_in_the_high_band_while_the_low_is_busy)
{
    constexpr double sr = 48000.0;
    AnalysisEngine engine;
    engine.prepare (sr, 512);

    auto settings = defaultAnalysisSettings();
    settings.mode = DetMode::Spectral;

    auto loop = makeLowKicksWithOpenHighs (sr, 3.0, 140.0, 1);
    std::vector<float> mono ((size_t) loop.getNumSamples());

    for (int i = 0; i < loop.getNumSamples(); ++i)
        mono[(size_t) i] = loop.getSample (0, i);

    engine.processBlock (mono.data(), nullptr, loop.getNumSamples(), settings);

    const auto beat = (std::int64_t) (60.0 / 140.0 * sr);
    const auto& onKick = engine.frameAt (6 * beat + (std::int64_t) (sr * 0.005));

    GF_EXPECT_MSG (onKick.bandConf[0] < 0.4f,
                   "low band should be busy on the kick, was " + juce::String (onKick.bandConf[0]));
    GF_EXPECT_MSG (onKick.bandConf[2] > 0.6f,
                   "high band should be available, was " + juce::String (onKick.bandConf[2]));
}

GF_TEST (AnalysisEngine, silence_produces_no_events_and_finite_state)
{
    constexpr double sr = 48000.0;
    AnalysisEngine engine;
    engine.prepare (sr, 512);

    auto settings = defaultAnalysisSettings();
    settings.sensitivity01 = 1.0f;   // maximum sensitivity

    std::vector<float> silence ((size_t) (sr * 2.0), 0.0f);
    engine.processBlock (silence.data(), nullptr, (int) silence.size(), settings);

    GF_EXPECT (engine.nextMainEvent (0) == -1);
    GF_EXPECT (std::isfinite (engine.getCurrentConfidence()));
    GF_EXPECT (engine.frameAt (1000).conf >= 0.0f && engine.frameAt (1000).conf <= 1.0f);
}

GF_TEST (AnalysisEngine, sidechain_can_drive_detection_when_main_is_static)
{
    constexpr double sr = 48000.0;
    AnalysisEngine engine;
    engine.prepare (sr, 512);

    auto settings = defaultAnalysisSettings();
    settings.source = DetSource::Sidechain;
    settings.sidechainActive = true;

    // Main: a continuous pad (no transients at all).
    // Sidechain: a clear drum pattern.
    const auto n = (int) (sr * 2.0);
    std::vector<float> main ((size_t) n), side ((size_t) n);

    for (int i = 0; i < n; ++i)
        main[(size_t) i] = 0.4f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) i / (float) sr);

    auto scBuffer = makeDrumLoop (sr, 2.0, 120.0, 1);

    for (int i = 0; i < n; ++i)
        side[(size_t) i] = scBuffer.getSample (0, i);

    engine.processBlock (main.data(), side.data(), n, settings);

    GF_EXPECT_MSG (engine.nextSidechainEvent (0) >= 0, "sidechain events should have been detected");
    GF_EXPECT_MSG (engine.nextDetectionEvent (0) >= 0, "detection lane should follow the sidechain");
}

//==============================================================================
GF_TEST (SpectralSmear, steady_input_does_not_build_up_and_stays_finite)
{
    for (double sr : { 44100.0, 48000.0, 192000.0 })
    {
        SpectralSmear smear;
        smear.prepare (sr, 2);
        smear.setCharacter (0.8f, 0.2f, 7);

        const auto n = (int) (sr * 3.0);
        float maxOut = 0.0f;
        double phase = 0.0;
        const auto inc = juce::MathConstants<double>::twoPi * 440.0 / sr;

        for (int i = 0; i < n; ++i)
        {
            const auto in = 0.5f * (float) std::sin (phase);
            phase += inc;

            float l = 0.0f, r = 0.0f;
            smear.process (in, in, l, r);

            GF_EXPECT (std::isfinite (l) && std::isfinite (r));

            if (i > (int) (sr * 0.2))
                maxOut = juce::jmax (maxOut, std::abs (l), std::abs (r));
        }

        GF_EXPECT_MSG (maxOut < 2.0f, "spectral output grew to " + juce::String (maxOut)
                                        + " at " + juce::String (sr) + " Hz");
        GF_EXPECT_MSG (maxOut > 0.05f, "spectral output was silent at " + juce::String (sr) + " Hz");
    }
}

//==============================================================================
GF_TEST (FillVoice, every_algorithm_is_finite_bounded_and_click_free)
{
    constexpr double sr = 48000.0;

    SourceHistory history;
    history.prepare (2, (int) (sr * 6.0));

    // Fill the history with real drum-like material.
    auto source = makeDrumLoop (sr, 3.0, 120.0, 2);
    history.write (source, 0, source.getNumSamples());

    SpectralSmear spectral;
    spectral.prepare (sr, 2);

    for (int algoIndex = 0; algoIndex < numAlgos; ++algoIndex)
    {
        for (float intensity : { 0.0f, 0.5f, 1.0f })
        {
            for (float complexity : { 0.0f, 0.5f, 1.0f })
            {
                FillVoice voice;
                voice.prepare (sr, 2);

                VoiceSetup setup;
                setup.algo = (Algo) algoIndex;
                setup.sampleRate = sr;
                setup.lengthSamples = (int) (sr * 0.4);
                setup.fadeInSamples = (int) (sr * 0.004);
                setup.fadeOutSamples = (int) (sr * 0.02);
                setup.amp = 0.8f;
                setup.intensity01 = intensity;
                setup.complexity01 = complexity;
                setup.chaos01 = 0.5f;
                setup.sourceAnchor = source.getNumSamples() - 1;
                setup.sinceEventSamples = (int) (sr * 0.05);
                setup.stutterSeconds = 0.0625f;
                setup.delaySeconds = 0.125f;
                setup.rngSeed = (std::uint64_t) (algoIndex * 977 + 13);

                spectral.release();
                const auto started = voice.start (setup, history, &spectral);
                GF_EXPECT_MSG (started, juce::String ("voice failed to start: ") + algoName ((Algo) algoIndex));

                juce::AudioBuffer<float> wet (2, (int) (sr * 0.6));
                wet.clear();

                int pos = 0;

                while (pos < wet.getNumSamples() && voice.isActive())
                {
                    const auto n = juce::jmin (64, wet.getNumSamples() - pos);
                    voice.render (wet, pos, n, history);
                    pos += n;
                }

                const juce::String label = juce::String (algoName ((Algo) algoIndex))
                                            + " int=" + juce::String (intensity, 1)
                                            + " cpx=" + juce::String (complexity, 1);

                GF_EXPECT_MSG (allFinite (wet), label + ": produced non-finite output");
                GF_EXPECT_MSG (maxAbs (wet) < 4.0f, label + ": peaked at " + juce::String (maxAbs (wet)));

                // The envelope must start and end at silence: no slice-boundary clicks.
                GF_EXPECT_MSG (std::abs (wet.getSample (0, 0)) < 1.0e-4f, label + ": non-silent first sample");

                const auto tail = maxAbs (wet, wet.getNumSamples() - 64, 64);
                GF_EXPECT_MSG (tail < 1.0e-3f, label + ": did not fade out (tail " + juce::String (tail) + ")");

                // No slice boundary, loop point or envelope step may show up as a
                // jump that stands out from the material around it. The limit
                // sits above the staircase a bit-crushed voice makes on purpose
                // (worst measured 2.5) and well below a real discontinuity - the
                // coherent-phase tick this caught in the spectral smear read 10.
                GF_EXPECT_MSG (maxRelativeJump (wet) < 4.0f,
                               label + ": click, relative jump " + juce::String (maxRelativeJump (wet)));
                GF_EXPECT_MSG (maxSampleJump (wet) < 4.0f,
                               label + ": sample jump " + juce::String (maxSampleJump (wet)));
            }
        }
    }
}

GF_TEST (FillVoice, requestStop_fades_out_instead_of_cutting)
{
    constexpr double sr = 48000.0;

    SourceHistory history;
    history.prepare (2, (int) (sr * 6.0));
    auto source = makeDrumLoop (sr, 2.0, 120.0, 2);
    history.write (source, 0, source.getNumSamples());

    SpectralSmear spectral;
    spectral.prepare (sr, 2);

    FillVoice voice;
    voice.prepare (sr, 2);

    VoiceSetup setup;
    setup.algo = Algo::Freeze;
    setup.sampleRate = sr;
    setup.lengthSamples = (int) (sr * 1.0);
    setup.fadeInSamples = (int) (sr * 0.003);
    setup.fadeOutSamples = (int) (sr * 0.02);
    setup.amp = 0.9f;
    setup.intensity01 = 0.6f;
    setup.sourceAnchor = source.getNumSamples() - 1;
    setup.rngSeed = 99;

    GF_EXPECT (voice.start (setup, history, &spectral));

    juce::AudioBuffer<float> wet (2, (int) (sr * 0.5));
    wet.clear();

    // Render 100 ms, then ask it to stop over 10 ms.
    const auto firstPart = (int) (sr * 0.1);
    voice.render (wet, 0, firstPart, history);

    const auto fadeSamples = (int) (sr * 0.01);
    voice.requestStop (fadeSamples);
    voice.render (wet, firstPart, wet.getNumSamples() - firstPart, history);

    GF_EXPECT (allFinite (wet));
    GF_EXPECT_MSG (! voice.isActive(), "voice should have finished after the kill fade");

    // Level after the fade must be silent, and the fade itself must be gradual.
    const auto afterFade = maxAbs (wet, firstPart + fadeSamples + 16, 512);
    GF_EXPECT_MSG (afterFade < 1.0e-4f, "audio continued after the kill fade: " + juce::String (afterFade));
    GF_EXPECT_MSG (maxSampleJump (wet) < 1.0f, "kill fade produced a click");
}

//==============================================================================
GF_TEST (FillEngine, minimum_gap_prevents_fills_in_short_spaces)
{
    constexpr double sr = 48000.0;

    auto runWithMinGap = [&] (float minGapMs)
    {
        AnalysisEngine analysis;
        SourceHistory history;
        FillEngine engine;

        analysis.prepare (sr, 512);
        history.prepare (2, (int) (sr * 6.0));
        engine.prepare (sr, 512, 2);

        // Fast pattern: the spaces between hits are about 65 ms.
        auto loop = makeSilence (2, (int) (sr * 4.0));
        const auto step = (int) (sr * 0.1);

        for (int pos = 0; pos + step < loop.getNumSamples(); pos += step)
            addDrumHit (loop, pos, sr, 180.0f, 0.008f, 0.8f, 0.5f);

        EngineSettings settings;
        settings.fill01 = 1.0f;
        settings.density01 = 1.0f;
        settings.minGapMs = minGapMs;
        settings.maxFillMs = 200.0f;
        settings.protNext01 = 0.5f;
        settings.lookaheadMs = 40.0f;

        for (int i = 0; i < numAlgos; ++i)
            settings.weights[i] = 0.7f;

        auto analysisSettings = defaultAnalysisSettings();
        analysisSettings.sensitivity01 = 0.65f;

        // A pattern this fast only ever has short gaps, so the detector has to
        // be told to confirm them quickly - otherwise nothing reaches the
        // minimum-gap check that this test is actually about.
        analysisSettings.protectHoldMs = 10.0f;
        analysisSettings.gapReleaseMs = 10.0f;

        juce::AudioBuffer<float> wet (2, 512);
        std::vector<float> mono (512);

        const auto lookaheadSamples = (std::int64_t) (settings.lookaheadMs * 0.001 * sr);
        std::int64_t processed = 0;

        for (int pos = 0; pos + 512 <= loop.getNumSamples(); pos += 512)
        {
            juce::AudioBuffer<float> block (2, 512);

            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, loop, ch, pos, 512);

            for (int i = 0; i < 512; ++i)
                mono[(size_t) i] = 0.5f * (block.getSample (0, i) + block.getSample (1, i));

            history.write (block, 0, 512);
            analysis.processBlock (mono.data(), nullptr, 512, analysisSettings);

            const auto outStart = processed - lookaheadSamples;
            wet.clear();

            if (outStart >= 0)
            {
                analysis.discardEventsBefore (outStart);
                engine.process (wet, 0, 512, outStart, analysis, history, settings, nullptr);
            }

            processed += 512;
        }

        return engine.getFillsStarted();
    };

    const auto withShortMinGap = runWithMinGap (20.0f);
    const auto withLongMinGap  = runWithMinGap (300.0f);

    GF_EXPECT_MSG (withShortMinGap > 0, "a 20 ms minimum gap should allow fills in 60 ms spaces");
    GF_EXPECT_MSG (withLongMinGap == 0,
                   "a 300 ms minimum gap must block every 60 ms space, got "
                     + juce::String (withLongMinGap) + " fills");
}


//==============================================================================
GF_TEST (AnalysisEngine, protect_hold_delays_gap_confidence_after_a_hit)
{
    constexpr double sr = 48000.0;
    constexpr int hop = AnalysisEngine::hopSize;

    auto confidenceAfter80ms = [] (float protectHoldMs)
    {
        LaneDetector lane;
        lane.prepare (sr, hop);
        lane.configure (DetMode::Transient);

        AnalysisSettings settings;
        settings.mode = DetMode::Transient;
        settings.sensitivity01 = 1.0f;
        settings.gapThresh01 = 1.0f;
        settings.protectHoldMs = protectHoldMs;
        settings.retrigGuardMs = 5.0f;
        settings.gapReleaseMs = 5.0f;

        bool sawEvent = false;
        const int hops = (int) std::ceil (0.080 * sr / hop);

        for (int h = 0; h < hops; ++h)
        {
            for (int i = 0; i < hop; ++i)
                lane.pushSample (h == 0 && i == 0 ? 1.0f : 0.0f);

            lane.updateHop (settings, 0.0f);
            sawEvent = sawEvent || lane.eventThisHop;
        }

        GF_EXPECT (sawEvent);
        return lane.conf;
    };

    const auto noHold = confidenceAfter80ms (0.0f);
    const auto longHold = confidenceAfter80ms (200.0f);

    GF_EXPECT_MSG (noHold > longHold + 0.30f,
                   "protectHold did not materially delay gap confidence: "
                     + juce::String (noHold) + " vs " + juce::String (longHold));
}

GF_TEST (AnalysisEngine, retrigger_guard_suppresses_close_transients)
{
    constexpr double sr = 48000.0;
    constexpr int hop = AnalysisEngine::hopSize;
    constexpr int secondHitSample = 1440; // 30 ms at 48 kHz

    auto countEvents = [] (float retrigGuardMs)
    {
        LaneDetector lane;
        lane.prepare (sr, hop);
        lane.configure (DetMode::Transient);

        AnalysisSettings settings;
        settings.mode = DetMode::Transient;
        settings.sensitivity01 = 1.0f;
        settings.gapThresh01 = 0.6f;
        settings.protectHoldMs = 0.0f;
        settings.retrigGuardMs = retrigGuardMs;
        settings.gapReleaseMs = 10.0f;

        int eventCount = 0;
        constexpr int totalSamples = 3200;

        for (int base = 0; base < totalSamples; base += hop)
        {
            for (int i = 0; i < hop; ++i)
            {
                const auto sample = base + i;
                lane.pushSample (sample == 0 || sample == secondHitSample ? 1.0f : 0.0f);
            }

            lane.updateHop (settings, 0.0f);

            if (lane.eventThisHop)
                ++eventCount;
        }

        return eventCount;
    };

    const auto shortGuard = countEvents (5.0f);
    const auto longGuard = countEvents (100.0f);

    GF_EXPECT_MSG (shortGuard >= 2,
                   "short retrigger guard missed the second transient: "
                     + juce::String (shortGuard));
    GF_EXPECT_MSG (longGuard == 1,
                   "long retrigger guard should suppress the second transient: "
                     + juce::String (longGuard));
}
