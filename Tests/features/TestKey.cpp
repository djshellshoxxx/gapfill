// F-X03 Automatic key detection (docs/dev/features/F-X03).

#include "../support/TestRig.h"
#include "../support/AllocGuard.h"

#include "features/context/ContextState.h"
#include "features/keydetect/AutoKey.h"
#include "features/keydetect/ChromaTracker.h"
#include "features/keydetect/KeyEstimator.h"
#include "features/keydetect/KeyStateTree.h"

#include <chrono>
#include <cstdio>
#include <cmath>
#include <vector>

using namespace gapfill;
using namespace gapfill::keydetect;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;

    /** Melody drawn from the key's Krumhansl-Kessler weights (squared, so the key is clear) with the given timbre:
        0 sine-ish, 1 six partials, 2 six partials detuned +30 cents. */
    std::vector<float> keyMusic (double sr, double seconds, int tonic, int mode, int timbre, std::uint32_t seed = 1)
    {
        const auto n = (size_t) (sr * seconds);
        std::vector<float> out (n, 0.0f);
        juce::Random rnd ((juce::int64) seed);

        float weights[12], total = 0.0f;

        for (int d = 0; d < 12; ++d)
        {
            const float p = profileValue (tonic + 12 * mode, (tonic + d) % 12);
            weights[d] = p * p;
            total += weights[d];
        }

        double t = 0.0;

        while (t < seconds)
        {
            float pick = rnd.nextFloat() * total;
            int degree = 0;

            for (; degree < 11; ++degree)
            {
                pick -= weights[degree];

                if (pick <= 0.0f)
                    break;
            }

            const int pc = (tonic + degree) % 12;
            const int midi = 48 + pc + 12 * rnd.nextInt (3);
            const double dur = 0.2 + 0.3 * rnd.nextDouble();
            double f = 440.0 * std::pow (2.0, (midi - 69) / 12.0);

            if (timbre == 2)
                f *= std::pow (2.0, 30.0 / 1200.0);

            const int partials = timbre == 0 ? 2 : 6;
            const auto start = (size_t) (t * sr);
            const auto len = (size_t) ((dur + 0.15) * sr);

            for (size_t i = 0; i < len && start + i < n; ++i)
            {
                const double tt = (double) i / sr;
                const double env = std::min (1.0, tt / 0.01) * std::exp (-tt / (dur * 1.2));
                double v = 0.0;

                for (int h = 1; h <= partials; ++h)
                {
                    const double a = timbre == 0 ? (h == 1 ? 1.0 : 0.05) : 1.0 / h;
                    v += a * std::sin (6.283185307179586 * f * h * tt);
                }

                out[start + i] += (float) (0.18 * env * v);
            }

            t += dur;
        }

        return out;
    }

    void feed (AutoKey& ak, const std::vector<float>& x, const KeySettings& s, int block = 512)
    {
        for (size_t pos = 0; pos < x.size(); pos += (size_t) block)
            ak.process (x.data() + pos, (int) std::min<size_t> ((size_t) block, x.size() - pos), s);
    }

    KeySettings autoSettings (float window = 12.0f, bool hold = false)
    {
        KeySettings s;
        s.autoOn = true;
        s.windowSeconds = window;
        s.minConfidence01 = 0.4f;
        s.hold = hold;
        return s;
    }

    std::vector<float> noise (double sr, double seconds, float gain, std::uint32_t seed = 9)
    {
        std::vector<float> out ((size_t) (sr * seconds));
        juce::Random rnd ((juce::int64) seed);

        for (auto& v : out)
            v = gain * (rnd.nextFloat() * 2.0f - 1.0f);

        return out;
    }

    bool acceptable (int expected, const KeyEstimate& e)
    {
        if (! e.valid)
            return false;

        const int et = expected % 12, em = expected / 12;
        const int got = e.tonic;

        if (got == et && (int) e.mode == em)
            return true;

        const int relative = em == 0 ? (et + 9) % 12 : (et + 3) % 12;

        return ((got == relative && (int) e.mode != em) || (got == (et + 7) % 12 && (int) e.mode == em)
                || (got == (et + 5) % 12 && (int) e.mode == em));
    }
}

//==============================================================================
GF_TEST (Key, profiles_sum_and_rotation_correct)
{
    GF_EXPECT_NEAR (profileValue (0, 0), 6.35, 1.0e-6);                 // C major, tonic
    GF_EXPECT_NEAR (profileValue (1, 1), 6.35, 1.0e-6);                 // C# major, tonic
    GF_EXPECT_NEAR (profileValue (2, 9), 5.19, 1.0e-6);                 // D major, the fifth (A)
    GF_EXPECT_NEAR (profileValue (12 + 9, 9), 6.33, 1.0e-6);            // A minor, tonic
    GF_EXPECT_NEAR (profileValue (12 + 9, 0), 5.38, 1.0e-6);            // A minor, minor third (C)

    double major = 0.0, minor = 0.0;

    for (int pc = 0; pc < 12; ++pc)
    {
        major += profileValue (0, pc);
        minor += profileValue (12, pc);
    }

    GF_EXPECT_NEAR (major, 6.35 + 2.23 + 3.48 + 2.33 + 4.38 + 4.09 + 2.52 + 5.19 + 2.39 + 3.66 + 2.29 + 2.88, 1.0e-4);
    GF_EXPECT_NEAR (minor, 6.33 + 2.68 + 3.52 + 5.38 + 2.60 + 3.53 + 2.54 + 4.75 + 3.98 + 2.69 + 3.34 + 3.17, 1.0e-4);
}

GF_TEST (Key, pearson_known_values)
{
    const float a[4] = { 1, 2, 3, 4 }, b[4] = { 2, 4, 6, 8 }, c[4] = { 4, 3, 2, 1 }, k[4] = { 5, 5, 5, 5 };
    GF_EXPECT_NEAR (pearson (a, b, 4), 1.0, 1.0e-6);
    GF_EXPECT_NEAR (pearson (a, c, 4), -1.0, 1.0e-6);
    GF_EXPECT (pearson (a, k, 4) == 0.0f);
}

GF_TEST (Key, chroma_pure_tones_land_in_right_pitch_class_and_fold_octaves)
{
    for (int pc = 0; pc < 12; ++pc)
        for (int octave : { 3, 4, 5 })
        {
            ChromaTracker t;
            t.prepare (kSr);
            const double f = 440.0 * std::pow (2.0, ((12 * octave + pc + 12) - 69) / 12.0);

            std::vector<float> x ((size_t) (kSr * 3.0));

            for (size_t i = 0; i < x.size(); ++i)
                x[i] = 0.3f * (float) (std::sin (6.2831853 * f * (double) i / kSr) + 0.4 * std::sin (6.2831853 * 2.0 * f * (double) i / kSr));

            t.process (x.data(), (int) x.size());

            int best = 0;

            for (int i = 1; i < 12; ++i)
                if (t.chroma()[i] > t.chroma()[best])
                    best = i;

            GF_EXPECT_MSG (best == pc, "pc " + juce::String (pc) + " octave " + juce::String (octave) + " got " + juce::String (best));
        }
}

GF_TEST (Key, chroma_detuned_30_cents_still_correct)
{
    for (int pc = 0; pc < 12; ++pc)
    {
        ChromaTracker t;
        t.prepare (kSr);
        const double f = 440.0 * std::pow (2.0, ((60 + pc) - 69) / 12.0 + 30.0 / 1200.0);
        std::vector<float> x ((size_t) (kSr * 6.0));

        for (size_t i = 0; i < x.size(); ++i)
            x[i] = 0.3f * (float) std::sin (6.2831853 * f * (double) i / kSr);

        t.process (x.data(), (int) x.size());
        int best = 0;

        for (int i = 1; i < 12; ++i)
            if (t.chroma()[i] > t.chroma()[best])
                best = i;

        GF_EXPECT (best == pc);
        GF_EXPECT (std::abs (t.tuningCents()) <= 50.0f && t.tuningCents() > 5.0f);
    }
}

GF_TEST (Key, tonal_gate_rejects_noise_and_drums)
{
    ChromaTracker t;
    t.prepare (kSr);
    const auto drums = drumLoop (kSr, 120.0, 12);
    t.process (drums.getReadPointer (0), drums.getNumSamples());
    GF_EXPECT_MSG (t.tonalMass() < 0.1f, "drum mass " + juce::String (t.tonalMass()));

    ChromaTracker n;
    n.prepare (kSr);
    const auto w = noise (kSr, 20.0, 0.3f);
    n.process (w.data(), (int) w.size());
    GF_EXPECT_MSG (n.tonalMass() < 0.05f, "noise mass " + juce::String (n.tonalMass()));

    ChromaTracker s;
    s.prepare (kSr);
    std::vector<float> silence ((size_t) (kSr * 10.0), 0.0f);
    s.process (silence.data(), (int) silence.size());
    GF_EXPECT (s.tonalMass() < 1.0e-3f);
}

GF_TEST (Key, key_24_keys_x_3_timbres_accuracy)
{
    int exact = 0, close = 0, total = 0;

    for (int key = 0; key < 24; ++key)
        for (int timbre = 0; timbre < 3; ++timbre)
        {
            AutoKey ak;
            ak.prepare (kSr);
            feed (ak, keyMusic (kSr, 25.0, key % 12, key / 12, timbre, (std::uint32_t) (key * 3 + timbre + 1)), autoSettings());
            const auto e = ak.estimate();
            ++total;
            exact += e.valid && e.keyIndex() == key;
            close += acceptable (key, e);
        }

    GF_EXPECT_MSG (exact * 10 >= total * 9, "exact " + juce::String (exact) + " of " + juce::String (total));
    GF_EXPECT_MSG (close == total, "acceptable " + juce::String (close) + " of " + juce::String (total));
}

GF_TEST (Key, percussion_noise_silence_invalid)
{
    const auto drums = drumLoop (kSr, 120.0, 16);
    std::vector<float> drumMono ((size_t) drums.getNumSamples());
    std::copy (drums.getReadPointer (0), drums.getReadPointer (0) + drums.getNumSamples(), drumMono.begin());

    std::vector<float> clicks ((size_t) (kSr * 30.0), 0.0f);

    for (size_t i = 0; i < clicks.size(); i += (size_t) (kSr * 0.25))
        clicks[i] = 0.9f;

    const std::vector<std::vector<float>> sources { drumMono, noise (kSr, 30.0, 0.3f), std::vector<float> ((size_t) (kSr * 30.0), 0.0f), clicks };

    int evaluations = 0, validOnes = 0;

    for (const auto& src : sources)
    {
        AutoKey ak;
        ak.prepare (kSr);
        const auto s = autoSettings();

        for (size_t pos = 0; pos + 48000 <= src.size(); pos += 48000)
        {
            ak.process (src.data() + pos, 48000, s);
            ++evaluations;
            validOnes += ak.estimate().valid ? 1 : 0;
        }
    }

    GF_EXPECT_MSG (validOnes * 20 < evaluations, "valid " + juce::String (validOnes) + " of " + juce::String (evaluations));
}

GF_TEST (Key, time_to_lock_and_no_flipflop_under_noise_bursts)
{
    const auto music = keyMusic (kSr, 60.0, 0, 0, 1, 77);

    // Lock time: a stable C major, polled every half second.
    {
        AutoKey ak;
        ak.prepare (kSr);
        double lockedAt = -1.0;

        for (int i = 0; i < 120; ++i)
        {
            ak.process (music.data() + (size_t) i * 24000, 24000, autoSettings());

            if (lockedAt < 0.0 && ak.estimate().valid && ak.estimate().keyIndex() == 0)
                lockedAt = (i + 1) * 0.5;
        }

        GF_EXPECT_MSG (lockedAt > 0.0 && lockedAt <= 8.0, "lock time " + juce::String (lockedAt));
    }

    // Stability: noise bursts every 5 s for 60 s change the key at most once.
    {
        auto withBursts = music;
        juce::Random rnd (5);

        for (size_t start = (size_t) (kSr * 3.0); start + 12000 < withBursts.size(); start += (size_t) (kSr * 5.0))
            for (size_t i = 0; i < 12000; ++i)
                withBursts[start + i] += 0.5f * (rnd.nextFloat() * 2.0f - 1.0f);

        AutoKey ak;
        ak.prepare (kSr);
        feed (ak, withBursts, autoSettings());
        GF_EXPECT_MSG (ak.hysteresis().changes() <= 1, "changes " + juce::String (ak.hysteresis().changes()));
        GF_EXPECT (ak.estimate().valid && ak.estimate().keyIndex() == 0);
    }
}

GF_TEST (Key, modulation_tracks_after_window)
{
    auto x = keyMusic (kSr, 20.0, 0, 0, 1, 31);
    const auto y = keyMusic (kSr, 30.0, 2, 0, 1, 32);              // D major from t = 20 s
    x.insert (x.end(), y.begin(), y.end());

    AutoKey ak;
    ak.prepare (kSr);
    double switchedAt = -1.0;

    for (size_t i = 0; i * 24000 + 24000 <= x.size(); ++i)
    {
        ak.process (x.data() + i * 24000, 24000, autoSettings());

        if (switchedAt < 0.0 && (double) (i + 1) * 0.5 > 20.0 && ak.estimate().valid && ak.estimate().keyIndex() == 2)
            switchedAt = (double) (i + 1) * 0.5;
    }

    GF_EXPECT_MSG (switchedAt > 0.0 && switchedAt - 20.0 <= 12.0 + 3.0, "switched at " + juce::String (switchedAt));
}

GF_TEST (Key, relative_key_low_confidence_when_ambiguous)
{
    float pure[12], mixed[12];

    for (int pc = 0; pc < 12; ++pc)
    {
        pure[pc] = profileValue (0, pc);
        mixed[pc] = 0.5f * profileValue (0, pc) + 0.5f * profileValue (12 + 9, pc);       // C major + A minor
    }

    const auto a = evaluate (pure, 1.0f, 0.4f);
    const auto b = evaluate (mixed, 1.0f, 0.4f);
    GF_EXPECT (a.valid && a.best == 0 && a.confidence > 0.9f);
    GF_EXPECT_MSG (b.confidence < a.confidence * 0.7f, "ambiguous " + juce::String (b.confidence));

    // Little tonal material heard so far: low confidence whatever the shape.
    const auto c = evaluate (pure, 0.05f, 0.4f);
    GF_EXPECT (! c.valid);
}

GF_TEST (Key, hold_freezes_and_resolver_precedence_table)
{
    AutoKey ak;
    ak.prepare (kSr);
    feed (ak, keyMusic (kSr, 25.0, 0, 0, 1, 3), autoSettings());
    GF_EXPECT (ak.estimate().valid && ak.estimate().keyIndex() == 0);

    // Hold copies the valid estimate; later music in another key does not move it.
    feed (ak, keyMusic (kSr, 5.0, 7, 0, 1, 4), autoSettings (12.0f, true));
    GF_EXPECT (ak.heldKey().valid && ak.heldKey().keyIndex() == 0);
    feed (ak, keyMusic (kSr, 40.0, 7, 0, 1, 5), autoSettings (12.0f, true));
    GF_EXPECT (ak.heldKey().keyIndex() == 0);

    // Releasing Hold forgets the copy.
    feed (ak, std::vector<float> (4800, 0.0f), autoSettings (12.0f, false));
    GF_EXPECT (! ak.heldKey().valid);

    KeyEstimate heldE; heldE.tonic = 9; heldE.mode = 1; heldE.confidence = 80; heldE.valid = true;
    KeyEstimate autoE; autoE.tonic = 2; autoE.mode = 0; autoE.confidence = 90; autoE.valid = true;
    const KeyEstimate none;

    // Manual: Auto off, manual scale set (root 5, type 3).
    auto r = resolveKey (5, 3, false, false, heldE, autoE);
    GF_EXPECT (r.source == KeySource::Manual && r.rootPc == 5 && r.scaleType == 3);
    r = resolveKey (5, 0, false, false, heldE, autoE);
    GF_EXPECT (r.source == KeySource::None && r.rootPc == -1);

    // Auto on: held wins when Hold, else detected, else manual, else none.
    r = resolveKey (5, 3, true, true, heldE, autoE);
    GF_EXPECT (r.source == KeySource::Held && r.rootPc == 9 && r.scaleType == 2);
    r = resolveKey (5, 3, true, false, heldE, autoE);
    GF_EXPECT (r.source == KeySource::Auto && r.rootPc == 2 && r.scaleType == 1);
    r = resolveKey (5, 3, true, true, none, autoE);
    GF_EXPECT (r.source == KeySource::Auto);
    r = resolveKey (5, 3, true, false, none, none);
    GF_EXPECT (r.source == KeySource::Manual && r.rootPc == 5);
    r = resolveKey (5, 0, true, false, none, none);
    GF_EXPECT (r.source == KeySource::None);
}

GF_TEST (Key, warm_start_replaced_by_evidence_unless_held)
{
    KeyEstimate warm; warm.tonic = 6; warm.mode = 1; warm.confidence = 70; warm.valid = true;       // F# minor

    AutoKey ak;
    ak.prepare (kSr);
    ak.requestWarmStart (warm);
    ak.process (nullptr, 0, autoSettings());
    GF_EXPECT (ak.estimate().valid && ak.estimate().keyIndex() == 6 + 12);                          // shown immediately

    feed (ak, keyMusic (kSr, 40.0, 0, 0, 1, 8), autoSettings());
    GF_EXPECT (ak.estimate().valid && ak.estimate().keyIndex() == 0);                               // replaced by evidence

    AutoKey held;
    held.prepare (kSr);
    held.requestWarmStart (warm);
    feed (held, keyMusic (kSr, 40.0, 0, 0, 1, 8), autoSettings (12.0f, true));
    GF_EXPECT (held.heldKey().valid && held.heldKey().keyIndex() == 6 + 12);                        // authoritative under Hold
}

GF_TEST (Key, block_size_invariance_estimates_identical)
{
    const auto x = keyMusic (kSr, 25.0, 4, 1, 1, 12);
    AutoKey a, b;
    a.prepare (kSr);
    b.prepare (kSr);
    feed (a, x, autoSettings(), 64);
    feed (b, x, autoSettings(), 4096);

    GF_EXPECT (pack (a.estimate()) == pack (b.estimate()));

    for (int i = 0; i < 12; ++i)
        GF_EXPECT (a.tracker().chroma()[i] == b.tracker().chroma()[i]);

    GF_EXPECT (a.tracker().tonalMass() == b.tracker().tonalMass());
}

GF_TEST (Key, sample_rate_44_48_96_192)
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        AutoKey ak;
        ak.prepare (sr);
        feed (ak, keyMusic (sr, 25.0, 0, 0, 1, 2), autoSettings(), 1024);
        GF_EXPECT_MSG (ak.estimate().valid && ak.estimate().keyIndex() == 0, "sr " + juce::String (sr));
    }
}

GF_TEST (Key, inactive_has_zero_cost_and_no_state_change)
{
    AutoKey ak;
    ak.prepare (kSr);
    KeySettings off;                                                  // autoOn = false
    feed (ak, keyMusic (kSr, 10.0, 0, 0, 1, 2), off);

    GF_EXPECT (ak.tracker().framesAnalysed() == 0);
    GF_EXPECT (! ak.isRunning());
    GF_EXPECT (! ak.estimate().valid);
}

GF_TEST (Key, worst_hop_cost_is_reported_and_bounded)
{
    ChromaTracker t;
    t.prepare (kSr);
    const auto x = keyMusic (kSr, 6.0, 0, 0, 1, 2);
    t.process (x.data(), (int) x.size());

    double worst = 0.0;

    for (int i = 0; i < 50; ++i)
    {
        const auto t0 = std::chrono::steady_clock::now();
        t.analyseFrame();
        const auto us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
        worst = std::max (worst, us);
    }

    std::printf ("         worst analyseFrame: %.1f us (target <= 60 us on a quiet machine)\n", worst);
    GF_EXPECT (worst < 3000.0);                                       // sanity bound on a shared CI VM
}

GF_TEST (Key, context_resolves_the_scale_to_the_detected_key)
{
    context::ContextState ctx;
    ctx.prepare (kSr);
    AnalysisEngine analysis;
    analysis.prepare (kSr, 512);

    const auto x = keyMusic (kSr, 30.0, 2, 1, 1, 6);                  // D minor
    EngineSettings s;
    s.scaleLock.set = scale::makeScaleSet (7, 0);                     // manual: G, Off
    s.scaleLock.amount01 = 1.0f;
    s.perf.key = autoSettings();

    for (size_t pos = 0; pos + 512 <= x.size(); pos += 512)
    {
        auto copy = s;
        ctx.onChunk (x.data() + pos, 512, analysis, (std::int64_t) pos, copy);
        s.scaleLock.set = scale::makeScaleSet (7, 0);                 // each chunk starts from the gathered manual setting
        s.perf.key = autoSettings();

        if (pos + 512 + 512 > x.size())
        {
            GF_EXPECT (copy.scaleLock.set.root == 2 && copy.scaleLock.set.type == 2);       // D Natural Minor
            GF_EXPECT (copy.scaleLock.active());
        }
    }

    // Auto off: the manual setting is untouched.
    EngineSettings manual;
    manual.scaleLock.set = scale::makeScaleSet (7, 1);
    ctx.onChunk (x.data(), 512, analysis, 0, manual);
    GF_EXPECT (manual.scaleLock.set.root == 7 && manual.scaleLock.set.type == 1);
}

GF_TEST (Key, state_roundtrip_and_bad_subtree_dropped)
{
    // Nothing is written while Auto key and Hold are off.
    GapFillAudioProcessor fresh;
    GF_EXPECT (! serialiseKeyTree (fresh).isValid());
    GF_EXPECT (pv (fresh.getValueTreeState().getRawParameterValue ("scaleFollow")) == 0.0f);        // old states: Auto off
    GF_EXPECT_NEAR (pv (fresh.getValueTreeState().getRawParameterValue ("fx03_window")), 12.0, 1.0e-6);
    GF_EXPECT_NEAR (pv (fresh.getValueTreeState().getRawParameterValue ("fx03_minConf")), 40.0, 1.0e-6);

    auto make = [] (const char* tonic, const char* mode, const char* conf)
    {
        juce::ValueTree t ("Key");
        t.setProperty ("tonic", tonic, nullptr);
        t.setProperty ("mode", mode, nullptr);
        t.setProperty ("conf", conf, nullptr);
        return t;
    };

    GF_EXPECT (validateKeyTree (make ("9", "1", "87")));
    GF_EXPECT (! validateKeyTree (make ("12", "1", "87")));
    GF_EXPECT (! validateKeyTree (make ("9", "2", "87")));
    GF_EXPECT (! validateKeyTree (make ("9", "1", "101")));
    GF_EXPECT (! validateKeyTree (make ("x", "1", "87")));
    GF_EXPECT (! validateKeyTree (juce::ValueTree ("Other")));

    // A held, detected key is saved and restores as the held key in a new instance.
    GapFillAudioProcessor a;
    setParams (a, { { "scaleFollow", 1.0f }, { "fx03_hold", 1.0f } });
    a.getContextState().autoKey().requestWarmStart ([] { KeyEstimate e; e.tonic = 9; e.mode = 1; e.confidence = 87; e.valid = true; return e; }());
    TestPlayHead ph;
    RenderOptions o;
    o.playHead = &ph;
    renderOffline (a, juce::AudioBuffer<float> (2, 2048), 512, kSr, o);

    const auto tree = serialiseKeyTree (a);
    GF_EXPECT (tree.isValid() && validateKeyTree (tree));
    GF_EXPECT ((int) tree.getProperty ("tonic") == 9 && (int) tree.getProperty ("mode") == 1);

    GapFillAudioProcessor b;
    setParams (b, { { "scaleFollow", 1.0f }, { "fx03_hold", 1.0f } });
    applyKeyTree (b, tree);
    renderOffline (b, juce::AudioBuffer<float> (2, 2048), 512, kSr, o);
    GF_EXPECT (b.getContextState().autoKey().heldKey().valid && b.getContextState().autoKey().heldKey().keyIndex() == 9 + 12);
}

GF_TEST (Key, audio_thread_does_not_allocate_while_detecting)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "scaleFollow", 1.0f } });
    TestPlayHead ph;
    RenderOptions o;
    o.playHead = &ph;
    renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, goldenBars), 512, goldenSampleRate, o);   // warm up

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block (2, 512);
    const auto music = keyMusic (kSr, 12.0, 0, 0, 1, 3);

    AllocGuard guard;

    for (size_t pos = 0; pos + 512 <= music.size(); pos += 512)
    {
        for (int ch = 0; ch < 2; ++ch)
            std::copy (music.begin() + (long) pos, music.begin() + (long) pos + 512, block.getWritePointer (ch));

        proc.processBlock (block, midi);
    }

    GF_EXPECT (guard.count() == 0);
}
