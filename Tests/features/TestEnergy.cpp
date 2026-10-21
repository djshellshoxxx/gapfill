// F-L22 Musical energy tracking (docs/dev/12 F-L22, docs/dev/features/F-L22).

#include "../support/TestRig.h"
#include "../support/AllocGuard.h"

#include "features/context/EnergyTracker.h"
#include "features/effective/Effective.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace gapfill;
using namespace gftest;
using context::EnergyLevel;
using context::EnergyTracker;

namespace
{
    /** Deterministic synthetic music. `layers` bit0 hats, bit1 kick, bit2 snare+bass, bit3 pad. */
    std::vector<float> music (double sr, double seconds, int layers, float gain = 1.0f, std::uint32_t seed = 3)
    {
        const auto n = (size_t) (sr * seconds);
        std::vector<float> out (n, 0.0f);
        juce::Random rnd ((juce::int64) seed);
        float hpState = 0.0f;

        for (size_t i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            float x = 0.0f;

            if (layers & 1)                                  // hats: 8 per second, 25 ms noise bursts
            {
                const double ph = std::fmod (t * 8.0, 1.0) / 8.0;
                const float env = ph < 0.025 ? (float) std::exp (-ph * 160.0) : 0.0f;
                const float noise = rnd.nextFloat() * 2.0f - 1.0f;
                hpState += 0.6f * (noise - hpState);
                x += 0.12f * env * (noise - hpState);
            }

            if (layers & 2)                                  // kick: 2 per second
            {
                const double ph = std::fmod (t * 2.0, 1.0) / 2.0;
                x += 0.5f * (float) (std::exp (-ph * 18.0) * std::sin (6.2831853 * (50.0 + 80.0 * std::exp (-ph * 40.0)) * ph));
            }

            if (layers & 4)                                  // snare on 2 and 4, sustained bass
            {
                const double ph = std::fmod (t * 1.0 + 0.5, 1.0);
                x += 0.3f * (rnd.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-ph * 20.0);
                x += 0.25f * (float) std::sin (6.2831853 * 55.0 * t);
            }

            if (layers & 8)                                  // pad
                x += 0.15f * (float) (std::sin (6.2831853 * 220.0 * t) + std::sin (6.2831853 * 277.2 * t) + std::sin (6.2831853 * 329.6 * t));

            out[i] = x * gain;
        }

        return out;
    }

    void feed (EnergyTracker& t, const std::vector<float>& x, int block)
    {
        for (size_t pos = 0; pos < x.size(); pos += (size_t) block)
            t.process (x.data() + pos, (int) std::min<size_t> ((size_t) block, x.size() - pos));
    }

    EngineSettings withEnergy (int level, float adapt, int target)
    {
        EngineSettings s;
        s.density01 = 0.5f;
        s.intensity01 = 0.5f;
        s.perf.energy = { adapt, target, level };
        return s;
    }
}

//==============================================================================
GF_TEST (Energy, frames_block_size_invariant)
{
    const double sr = 48000.0;
    const auto x = music (sr, 40.0, 1 | 2 | 4);

    EnergyTracker a, b, c;
    a.prepare (sr); b.prepare (sr); c.prepare (sr);
    feed (a, x, 64);
    feed (b, x, 4096);

    // Irregular host blocks.
    juce::Random rnd (9);
    size_t pos = 0;

    while (pos < x.size())
    {
        const auto n = (size_t) (1 + rnd.nextInt (3000));
        const auto m = std::min (n, x.size() - pos);
        c.process (x.data() + pos, (int) m);
        pos += m;
    }

    for (auto* t : { &b, &c })
    {
        GF_EXPECT (std::memcmp (&a.lastFrame(), &t->lastFrame(), sizeof (context::FrameFeatures)) == 0);
        GF_EXPECT (a.energy() == t->energy());
        GF_EXPECT (a.level() == t->level());
        GF_EXPECT (a.levelChanges() == t->levelChanges());
    }
}

GF_TEST (Energy, warmup_is_neutral_and_reset_relearns)
{
    const double sr = 48000.0;
    EnergyTracker t;
    t.prepare (sr);
    feed (t, music (sr, 9.0, 15), 512);
    GF_EXPECT (! t.warmedUp());
    GF_EXPECT (t.level() == EnergyLevel::Mid);
    feed (t, music (sr, 3.0, 15), 512);
    GF_EXPECT (t.warmedUp());

    t.reset();
    GF_EXPECT (! t.warmedUp() && t.level() == EnergyLevel::Mid && t.levelChanges() == 0);
    GF_EXPECT (t.frameLength() == 24000);
}

GF_TEST (Energy, ladder_rises_and_ends_high)
{
    const double sr = 48000.0;
    EnergyTracker t;
    t.prepare (sr);

    std::vector<int> atStepEnd;
    const int layers[] = { 1, 1 | 2, 1 | 2 | 4, 1 | 2 | 4 | 8 };
    const float gains[] = { 0.4f, 0.7f, 1.0f, 1.3f };

    for (int step = 0; step < 4; ++step)
    {
        feed (t, music (sr, 12.0, layers[step], gains[step], (std::uint32_t) (step + 1)), 512);
        atStepEnd.push_back ((int) t.level());
    }

    GF_EXPECT_MSG (atStepEnd[0] != 2, "hats alone must not read High");
    GF_EXPECT (atStepEnd[3] == 2);

    for (size_t i = 1; i < atStepEnd.size(); ++i)
        GF_EXPECT (atStepEnd[i] >= atStepEnd[i - 1]);

    GF_EXPECT (t.levelChanges() >= 1 && t.levelChanges() <= 3);
}

GF_TEST (Energy, hysteresis_no_flicker_on_a_stationary_loop)
{
    const double sr = 48000.0;
    EnergyTracker t;
    t.prepare (sr);

    // The same loop with +-3 dB random level changes every second, 3 minutes.
    juce::Random rnd (21);

    for (int sec = 0; sec < 180; ++sec)
    {
        const float gain = std::pow (10.0f, (rnd.nextFloat() * 6.0f - 3.0f) / 20.0f);
        feed (t, music (sr, 1.0, 1 | 2 | 4, gain, 3), 512);
    }

    GF_EXPECT_MSG (t.levelChanges() <= 2, "changes: " + juce::String (t.levelChanges()));
}

GF_TEST (Energy, min_dwell_respected)
{
    const double sr = 48000.0;
    EnergyTracker t;
    t.prepare (sr);
    const auto loud = music (sr, 6.0, 1 | 2 | 4 | 8, 1.2f);
    const auto quiet = music (sr, 6.0, 1, 0.3f);

    int frames = 0, lastChangeFrame = -1000, minGap = 1000, prevChanges = 0;
    const int half = t.frameLength();

    for (int cycle = 0; cycle < 12; ++cycle)
        for (const auto* part : { &loud, &quiet })
            for (size_t pos = 0; pos + (size_t) half <= part->size(); pos += (size_t) half)
            {
                t.process (part->data() + pos, half);
                ++frames;

                if (t.levelChanges() != prevChanges)
                {
                    prevChanges = t.levelChanges();

                    if (lastChangeFrame >= 0)
                        minGap = std::min (minGap, frames - lastChangeFrame);

                    lastChangeFrame = frames;
                }
            }

    GF_EXPECT (t.levelChanges() >= 2);
    GF_EXPECT_MSG (minGap >= 8, "frames between changes: " + juce::String (minGap));          // 8 x 0.5 s = 4 s
}

GF_TEST (Energy, range_floor_prevents_noise_stretch)
{
    // A flat history maps to the middle; a 0.3 dB wobble cannot be stretched to the whole scale.
    GF_EXPECT_NEAR (EnergyTracker::normalise (-20.0f, -20.0f, -20.0f, 6.0f), 0.5, 1.0e-6);
    GF_EXPECT (std::abs (EnergyTracker::normalise (-19.7f, -20.0f, -19.6f, 6.0f) - 0.5f) < 0.06f);
    GF_EXPECT (EnergyTracker::normalise (100.0f, 0.0f, 10.0f, 6.0f) == 1.0f);
    GF_EXPECT (EnergyTracker::normalise (-100.0f, 0.0f, 10.0f, 6.0f) == 0.0f);
}

GF_TEST (Energy, silence_stays_low_no_nan)
{
    const double sr = 48000.0;
    EnergyTracker t;
    t.prepare (sr);
    feed (t, std::vector<float> ((size_t) (sr * 60.0), 0.0f), 512);

    GF_EXPECT (std::isfinite (t.energy()) && std::isfinite (t.lastFrame().rmsDb));
    GF_EXPECT (t.level() == EnergyLevel::Low);

    // Garbage input never produces NaN state.
    std::vector<float> bad ((size_t) (sr * 2.0), 0.0f);
    bad[100] = 1.0e9f;
    feed (t, bad, 512);
    GF_EXPECT (std::isfinite (t.energy()));
}

GF_TEST (Energy, sample_rate_sweep)
{
    int levels[4];
    int i = 0;

    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        EnergyTracker t;
        t.prepare (sr);
        GF_EXPECT (t.frameLength() == (int) std::lround (0.5 * sr));

        feed (t, music (sr, 14.0, 1, 0.3f), 1024);
        feed (t, music (sr, 14.0, 1 | 2 | 4 | 8, 1.3f), 1024);
        levels[i++] = (int) t.level();
    }

    for (int l : levels)
        GF_EXPECT (l == 2);
}

GF_TEST (Energy, offsets_table_target_filter_and_identity)
{
    using Effective::TransportInfo;
    TransportInfo ts;

    // Low: more fills, softer.
    auto low = withEnergy (0, 1.0f, 2);
    Effective::applyEnergy (low, 0.0, ts);
    GF_EXPECT_NEAR (low.density01, 0.65, 1.0e-6);
    GF_EXPECT_NEAR (low.intensity01, 0.40, 1.0e-6);

    // High: fewer fills, bolder.
    auto high = withEnergy (2, 1.0f, 2);
    Effective::applyEnergy (high, 0.0, ts);
    GF_EXPECT_NEAR (high.density01, 0.35, 1.0e-6);
    GF_EXPECT_NEAR (high.intensity01, 0.65, 1.0e-6);

    // Scaled by the amount.
    auto half = withEnergy (2, 0.5f, 2);
    Effective::applyEnergy (half, 0.0, ts);
    GF_EXPECT_NEAR (half.density01, 0.425, 1.0e-6);

    // Target filter.
    auto dOnly = withEnergy (2, 1.0f, 0);
    Effective::applyEnergy (dOnly, 0.0, ts);
    GF_EXPECT_NEAR (dOnly.density01, 0.35, 1.0e-6);
    GF_EXPECT_NEAR (dOnly.intensity01, 0.5, 1.0e-6);

    auto iOnly = withEnergy (2, 1.0f, 1);
    Effective::applyEnergy (iOnly, 0.0, ts);
    GF_EXPECT_NEAR (iOnly.density01, 0.5, 1.0e-6);
    GF_EXPECT_NEAR (iOnly.intensity01, 0.65, 1.0e-6);

    // Clamped to [0, 1].
    auto edge = withEnergy (0, 1.0f, 2);
    edge.density01 = 0.95f;
    edge.intensity01 = 0.05f;
    Effective::applyEnergy (edge, 0.0, ts);
    GF_EXPECT (edge.density01 == 1.0f && edge.intensity01 == 0.0f);

    // Identity: adapt 0, or level Mid, leaves the settings byte-for-byte untouched.
    for (auto s : { withEnergy (0, 0.0f, 2), withEnergy (2, 0.0f, 2), withEnergy (1, 1.0f, 2) })
    {
        const auto before = s;
        Effective::applyEnergy (s, 0.0, ts);
        GF_EXPECT (std::memcmp (&s, &before, sizeof (EngineSettings)) == 0);
    }

    // The full decision stage is the identity at neutral settings as well.
    EngineSettings neutral;
    const auto copy = neutral;
    Effective::applyAtDecision (neutral, 0.0, ts);
    GF_EXPECT (std::memcmp (&neutral, &copy, sizeof (EngineSettings)) == 0);
}

GF_TEST (Energy, identity_when_zero_and_parameters)
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

    const auto a = render ({ { "fill", 100.0f }, { "density", 100.0f } });
    const auto b = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "energyAdapt", 0.0f }, { "energyTarget", 0.0f } });

    GF_EXPECT (a.getNumSamples() == b.getNumSamples());
    GF_EXPECT (std::memcmp (a.getReadPointer (0), b.getReadPointer (0), sizeof (float) * (size_t) a.getNumSamples()) == 0);

    GapFillAudioProcessor fresh;
    GF_EXPECT (pv (fresh.getValueTreeState().getRawParameterValue ("energyAdapt")) == 0.0f);
    GF_EXPECT (pi (fresh.getValueTreeState().getRawParameterValue ("energyTarget")) == 2);
    GF_EXPECT (fresh.getContextState().energyLevelForDisplay() == -1);

    const auto random = randomisableParameterIDs();
    GF_EXPECT (! random.contains ("energyAdapt") && ! random.contains ("energyTarget"));
}

GF_TEST (Energy, processor_publishes_the_level_when_following)
{
    const double sr = 48000.0;
    GapFillAudioProcessor proc;
    setParams (proc, { { "energyAdapt", 100.0f } });

    auto quiet = music (sr, 14.0, 1, 0.3f);
    auto loud = music (sr, 14.0, 1 | 2 | 4 | 8, 1.3f);
    quiet.insert (quiet.end(), loud.begin(), loud.end());

    juce::AudioBuffer<float> in (2, (int) quiet.size());

    for (int ch = 0; ch < 2; ++ch)
        std::memcpy (in.getWritePointer (ch), quiet.data(), sizeof (float) * quiet.size());

    TestPlayHead ph;
    RenderOptions o;
    o.playHead = &ph;
    renderOffline (proc, in, 512, sr, o);

    GF_EXPECT (proc.getContextState().energyLevelForDisplay() == 2);
}

GF_TEST (Energy, audio_thread_does_not_allocate_while_following)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "energyAdapt", 80.0f } });
    TestPlayHead ph;
    RenderOptions o;
    o.playHead = &ph;
    renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, goldenBars), 512, goldenSampleRate, o);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block (2, 512);
    block.clear();

    AllocGuard guard;

    for (int i = 0; i < 400; ++i)
        proc.processBlock (block, midi);

    GF_EXPECT (guard.count() == 0);
}
