// F-G01 Glitch rack: DSP core tests (portable core, no processor).
// Covers: neutrality, block-size invariance, sample rates, extreme values,
// allocation-free processing, and the musical contract of every module.

#include "../TestFramework.h"
#include "../support/AllocGuard.h"

#include "features/glitch/GlitchRack.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

using namespace gapfill::glitch;
using namespace gftest;

namespace
{
    struct Stereo
    {
        std::vector<float> l, r;
        explicit Stereo (int n = 0) : l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f) {}
        int size() const { return (int) l.size(); }
    };

    /** Deterministic, sample-unique test noise (LCG), different per channel. */
    Stereo noise (int n, float level = 0.5f, std::uint32_t seed = 1)
    {
        Stereo s (n);
        auto x = seed;
        for (int i = 0; i < n; ++i)
        {
            x = x * 1664525u + 1013904223u;
            s.l[(std::size_t) i] = level * ((float) (x >> 8) / 8388608.0f - 1.0f);
            x = x * 1664525u + 1013904223u;
            s.r[(std::size_t) i] = level * ((float) (x >> 8) / 8388608.0f - 1.0f);
        }
        return s;
    }

    Stereo sine (int n, double hz, double sr, float level = 0.5f)
    {
        Stereo s (n);
        for (int i = 0; i < n; ++i)
            s.l[(std::size_t) i] = s.r[(std::size_t) i] = level * (float) std::sin (2.0 * kPi * hz * i / sr);
        return s;
    }

    Timing timing (double sr, double bpm = 120.0, bool playing = true)
    {
        Timing t;
        t.sampleRate = sr;
        t.playing = playing;
        t.samplesPerBeat = 60.0 / bpm * sr;
        return t;
    }

    /** Renders through a fresh rack in blocks; the transport advances per block like a host. */
    Stereo render (const Stereo& in, const Settings& s, double sr, int block, double bpm = 120.0,
                   const std::function<void (int, Settings&)>& perBlock = {})
    {
        GlitchRack rack;
        rack.prepare (sr);
        auto out = in;
        auto t = timing (sr, bpm);
        auto local = s;

        for (int start = 0; start < in.size(); start += block)
        {
            const auto n = std::min (block, in.size() - start);
            if (perBlock)
                perBlock (start, local);
            t.ppqAtStart = (double) start / t.samplesPerBeat;
            rack.process (out.l.data() + start, out.r.data() + start, n, local, t);
        }
        return out;
    }

    bool identical (const Stereo& a, const Stereo& b)
    {
        return a.size() == b.size() && std::memcmp (a.l.data(), b.l.data(), a.l.size() * sizeof (float)) == 0
            && std::memcmp (a.r.data(), b.r.data(), a.r.size() * sizeof (float)) == 0;
    }

    double rms (const std::vector<float>& x, int from, int count)
    {
        double acc = 0.0;
        for (int i = from; i < from + count; ++i)
            acc += (double) x[(std::size_t) i] * x[(std::size_t) i];
        return std::sqrt (acc / std::max (1, count));
    }

    Settings everythingOn()
    {
        Settings s;
        s.on = true;
        s.swing01 = 0.3f;
        s.timeVar01 = 0.2f;
        s.stutMode = ClockMode::Tempo;
        s.stutProb01 = 0.7f;
        s.stutReverse01 = 0.3f;
        s.stutVar01 = 0.4f;
        s.stutPick01 = 0.3f;
        s.stutGate01 = 0.7f;
        s.stutDecay01 = 0.2f;
        s.revMode = ClockMode::Tempo;
        s.revDiv = (int) Div::D8;
        s.revProb01 = 0.5f;
        s.gateMode = ClockMode::Tempo;
        s.gatePattern = GatePattern::Tresillo;
        s.gateVar01 = 0.2f;
        s.gateStereo01 = 0.5f;
        s.filtMode = FilterMode::LowPass;
        s.filtShape = LfoShape::RandomStep;
        s.filtRes01 = 0.6f;
        s.filtStereoDeg = 90.0f;
        s.phMix01 = 0.8f;
        s.phFeedback = 0.7f;
        s.phStages = 8;
        s.panInvert01 = 0.3f;
        s.flipMode = FlipMode::InvertRight;
        return s;
    }

    bool finiteAndBounded (const Stereo& x, float bound)
    {
        for (int i = 0; i < x.size(); ++i)
            if (! std::isfinite (x.l[(std::size_t) i]) || ! std::isfinite (x.r[(std::size_t) i])
                || std::abs (x.l[(std::size_t) i]) > bound || std::abs (x.r[(std::size_t) i]) > bound)
                return false;
        return true;
    }
}

//==============================================================================
GF_TEST (GlitchDsp, grid_swing_moves_every_second_step_and_variation_stays_monotonic)
{
    const Grid straight (0.0, 0.0, 1, 1);
    const Grid swung (1.0, 0.0, 1, 1);
    GF_EXPECT_NEAR (straight.startOf (1), 1.0, 1e-12);
    GF_EXPECT_NEAR (swung.startOf (1), 1.5, 1e-12);
    GF_EXPECT_NEAR (swung.startOf (2), 2.0, 1e-12);
    GF_EXPECT (swung.locate (1.4).step == 0 && swung.locate (1.6).step == 1);
    GF_EXPECT_NEAR (swung.locate (0.1).length, 1.5, 1e-12);
    GF_EXPECT_NEAR (swung.locate (1.6).length, 0.5, 1e-12);
    // Negative (pre-roll) positions: the pair [-2, 0) splits at -0.5 with 75 % swing.
    GF_EXPECT (swung.locate (-0.6).step == -2 && swung.locate (-0.4).step == -1);

    const Grid varied (1.0, 1.0, 7, 3);
    for (std::int64_t k = -200; k < 200; ++k)
    {
        GF_EXPECT (varied.startOf (k + 1) > varied.startOf (k));
        GF_EXPECT (std::abs (varied.startOf (k) - swung.startOf (k)) <= 0.1 + 1e-12);
        const auto mid = 0.5 * (varied.startOf (k) + varied.startOf (k + 1));
        GF_EXPECT (varied.locate (mid).step == k);
    }
}

GF_TEST (GlitchDsp, rack_off_or_all_modules_off_is_bit_identical)
{
    const auto in = noise (48000);
    Settings off;                                  // defaults: rack off
    auto offButModules = everythingOn();
    offButModules.on = false;                      // modules set, rack off
    Settings onNoModules;
    onNoModules.on = true;                         // rack on, every module off

    for (const auto* s : { &off, &offButModules, &onNoModules })
        GF_EXPECT (identical (render (in, *s, 48000.0, 512), in));
}

GF_TEST (GlitchDsp, output_is_block_size_invariant)
{
    const auto in = noise (96000);
    const auto s = everythingOn();
    const auto reference = render (in, s, 48000.0, 512);

    for (const auto block : { 1, 37, 64, 997, 4096 })
        GF_EXPECT_MSG (identical (render (in, s, 48000.0, block), reference), "block " + juce::String (block));
}

GF_TEST (GlitchDsp, every_sample_rate_and_extreme_setting_stays_finite_and_bounded)
{
    for (const auto sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto extreme = everythingOn();
        extreme.stutDiv = (int) Div::D64;
        extreme.stutRepeats = 16;
        extreme.stutSmoothMs = 0.5f;
        extreme.gateDiv = (int) Div::D64;
        extreme.gateAttackMs = 0.1f;
        extreme.gateReleaseMs = 0.1f;
        extreme.filtMode = FilterMode::BandPass;
        extreme.filtRes01 = 1.0f;
        extreme.filtMinHz = 20.0f;
        extreme.filtMaxHz = 20000.0f;
        extreme.filtSync = false;
        extreme.filtHz = 20.0f;
        extreme.phFeedback = 0.95f;
        extreme.phStages = 12;
        extreme.phCentreHz = 5000.0f;
        extreme.phDepth01 = 1.0f;

        const auto n = (int) (sr * 1.5);
        GF_EXPECT_MSG (finiteAndBounded (render (noise (n, 4.0f), extreme, sr, 333), 8.0f), "noise at " + juce::String (sr));

        auto negative = extreme;
        negative.phFeedback = -0.95f;
        negative.stutMode = ClockMode::Free;
        negative.stutMs = 10.0f;
        Stereo impulse (n);
        impulse.l[100] = impulse.r[100] = 1.0f;
        GF_EXPECT_MSG (finiteAndBounded (render (impulse, negative, sr, 1024), 8.0f), "impulse at " + juce::String (sr));

        const auto silence = render (Stereo (n), extreme, sr, 256);
        GF_EXPECT_MSG (finiteAndBounded (silence, 1.0e-12f), "silence stays silent at " + juce::String (sr));
    }
}

GF_TEST (GlitchDsp, processing_never_allocates)
{
    GlitchRack rack;
    rack.prepare (48000.0);
    auto s = everythingOn();
    auto t = timing (48000.0);
    auto x = noise (48000);
    AllocGuard guard;

    for (int start = 0; start < x.size(); start += 512)
    {
        s.stab = start > 10000 && start < 30000;     // STAB press and release inside the guarded section
        t.ppqAtStart = start / t.samplesPerBeat;
        rack.process (x.l.data() + start, x.r.data() + start, std::min (512, x.size() - start), s, t);
    }

    GF_EXPECT (guard.count() == 0);
}

//==============================================================================
GF_TEST (GlitchDsp, stutter_repeats_the_captured_slice_on_the_tempo_grid)
{
    // 120 bpm at 48 kHz: a 1/16 slice is exactly 6000 samples, a 4-slice window 24000.
    const auto in = noise (96000);
    Settings s;
    s.on = true;
    s.stutMode = ClockMode::Tempo;
    s.stutProb01 = 1.0f;
    s.stutRepeats = 4;
    s.stutSmoothMs = 0.5f;            // 24-sample crossfades
    const auto out = render (in, s, 48000.0, 512);

    // The window starting at 24000 captures 24000..29999 and repeats it three times.
    bool repeated = true;
    for (int unit = 1; unit < 4; ++unit)
        for (int i = 100; i < 5900; ++i)
            repeated = repeated && out.l[(std::size_t) (24000 + unit * 6000 + i)] == in.l[(std::size_t) (24000 + i)]
                                && out.r[(std::size_t) (24000 + unit * 6000 + i)] == in.r[(std::size_t) (24000 + i)];
    GF_EXPECT (repeated);

    // The captured (first) slice itself plays live.
    bool live = true;
    for (int i = 100; i < 5900; ++i)
        live = live && out.l[(std::size_t) (24000 + i)] == in.l[(std::size_t) (24000 + i)];
    GF_EXPECT (live);
}

GF_TEST (GlitchDsp, stutter_reverse_gate_and_decay_shape_the_repeats)
{
    const auto in = noise (96000);
    Settings s;
    s.on = true;
    s.stutMode = ClockMode::Tempo;
    s.stutProb01 = 1.0f;
    s.stutRepeats = 2;
    s.stutSmoothMs = 0.5f;
    s.stutReverse01 = 1.0f;
    const auto reversed = render (in, s, 48000.0, 512);
    // Window at 12000: slice 12000..17999, repeat at 18000.. plays it backwards.
    bool backwards = true;
    for (int i = 100; i < 5900; ++i)
        backwards = backwards && reversed.l[(std::size_t) (18000 + i)] == in.l[(std::size_t) (12000 + 5999 - i)];
    GF_EXPECT (backwards);

    s.stutReverse01 = 0.0f;
    s.stutGate01 = 0.5f;
    s.stutDecay01 = 0.5f;
    const auto gated = render (in, s, 48000.0, 512);
    GF_EXPECT_NEAR (gated.l[18000 + 1000], 0.5f * in.l[12000 + 1000], 1e-6f);   // decay: half level
    GF_EXPECT (rms (gated.l, 18000 + 3100, 2800) < 1.0e-9);                     // gate closed after 50 %
}

GF_TEST (GlitchDsp, stutter_probability_zero_and_free_mode)
{
    const auto in = noise (48000);
    Settings s;
    s.on = true;
    s.stutMode = ClockMode::Tempo;
    s.stutProb01 = 0.0f;
    GF_EXPECT (identical (render (in, s, 48000.0, 256), in));

    s.stutMode = ClockMode::Free;
    s.stutProb01 = 1.0f;
    s.stutMs = 50.0f;                 // 2400 samples, window of 4 = 9600
    s.stutSmoothMs = 0.5f;
    const auto out = render (in, s, 48000.0, 100);
    bool repeated = true;
    for (int i = 100; i < 2300; ++i)
        repeated = repeated && out.l[(std::size_t) (9600 + 2400 + i)] == in.l[(std::size_t) (9600 + i)];
    GF_EXPECT (repeated);
}

//==============================================================================
GF_TEST (GlitchDsp, stab_finite_plays_its_repeats_then_returns_live_however_long_it_is_held)
{
    const auto in = noise (96000);
    Settings s;
    s.on = true;
    s.stabDiv = (int) Div::D16;       // 6000 samples at 120 bpm
    s.stabRepeats = 2;
    s.stabReleaseMs = 5.0f;
    const auto out = render (in, s, 48000.0, 1000, 120.0, [] (int start, Settings& st) { st.stab = start >= 10000; });

    bool repeats = true;
    for (int r = 1; r <= 2; ++r)
        for (int i = 200; i < 5800; ++i)
            repeats = repeats && out.l[(std::size_t) (10000 + r * 6000 + i)] == in.l[(std::size_t) (10000 + i)];
    GF_EXPECT (repeats);

    // Held for the whole render, but finite: live again after 3 units plus the release fade.
    bool live = true;
    for (int i = 10000 + 18000 + 400; i < 60000; ++i)
        live = live && out.l[(std::size_t) i] == in.l[(std::size_t) i];
    GF_EXPECT (live);
}

GF_TEST (GlitchDsp, stab_infinite_repeats_until_released_with_fixed_memory)
{
    const auto sr = 48000.0;
    const auto total = (int) (sr * 12);
    const auto in = noise (total);
    Settings s;
    s.on = true;
    s.stabInfinite = true;
    s.stabDiv = (int) Div::D8;        // 12000 samples
    s.stabReleaseMs = 5.0f;
    const auto releaseAt = (int) (sr * 10);

    GlitchRack rack;
    rack.prepare (sr);
    auto out = in;
    auto t = timing (sr);
    long allocations = 0;

    for (int start = 0; start < total; start += 512)
    {
        const auto n = std::min (512, total - start);
        s.stab = start >= 1024 && start < releaseAt;
        t.ppqAtStart = start / t.samplesPerBeat;
        AllocGuard guard;
        rack.process (out.l.data() + start, out.r.data() + start, n, s, t);
        allocations += guard.count();
    }

    GF_EXPECT (allocations == 0);

    // Still repeating the slice captured at 1024 nine seconds later.
    const auto late = 1024 + 12000 * 38;   // unit 38 ends at 468824, before the release at 480000
    bool repeating = true;
    for (int i = 200; i < 11800; ++i)
        repeating = repeating && out.l[(std::size_t) (late + i)] == in.l[(std::size_t) (1024 + i)];
    GF_EXPECT (repeating);

    bool live = true;
    for (int i = releaseAt + 512 + 400; i < total; ++i)
        live = live && out.l[(std::size_t) i] == in.l[(std::size_t) i];
    GF_EXPECT (live);
}

GF_TEST (GlitchDsp, stab_retrigger_grabs_a_fresh_slice)
{
    const auto in = noise (96000);
    Settings s;
    s.on = true;
    s.stabInfinite = true;
    s.stabDiv = (int) Div::D16;
    const auto out = render (in, s, 48000.0, 1000, 120.0, [] (int start, Settings& st)
    {
        st.stab = (start >= 5000 && start < 30000) || start >= 31000;   // release, then press again at 31000
    });
    bool fresh = true;
    for (int i = 200; i < 5800; ++i)
        fresh = fresh && out.l[(std::size_t) (31000 + 6000 + i)] == in.l[(std::size_t) (31000 + i)];
    GF_EXPECT (fresh);
}

//==============================================================================
GF_TEST (GlitchDsp, reverse_plays_each_segment_backwards_one_segment_late)
{
    const auto in = noise (96000);
    Settings s;
    s.on = true;
    s.revMode = ClockMode::Tempo;
    s.revDiv = (int) Div::D4;         // 24000 samples
    s.revProb01 = 1.0f;
    s.revMix01 = 1.0f;
    s.revFadeMs = 1.0f;               // 48 samples
    const auto out = render (in, s, 48000.0, 777);

    bool backwards = true;
    for (int i = 100; i < 23900; ++i)
        backwards = backwards && out.l[(std::size_t) (24000 + i)] == in.l[(std::size_t) (23999 - i)];
    GF_EXPECT (backwards);

    // Edges fade: the boundary sample is silent, the first segment (nothing before it) is forwards.
    GF_EXPECT (std::abs (out.l[48000]) < 1.0e-6f);
    GF_EXPECT (out.l[5000] == in.l[5000]);

    s.revProb01 = 0.0f;
    GF_EXPECT (identical (render (in, s, 48000.0, 512), in));
}

GF_TEST (GlitchDsp, microgator_gates_on_the_grid_at_every_rate_and_block_size)
{
    for (const auto sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const auto block : { 64, 997 })
        {
            const auto step = (int) std::lround (0.125 * 60.0 / 120.0 * sr);   // 1/32 at 120 bpm
            const auto n = step * 16;
            const auto in = sine (n, 1000.0, sr);
            Settings s;
            s.on = true;
            s.gateMode = ClockMode::Tempo;
            s.gateDiv = (int) Div::D32;
            s.gateLen01 = 0.5f;
            s.gateDepth01 = 1.0f;
            s.gateAttackMs = 0.1f;
            s.gateReleaseMs = 0.1f;
            const auto out = render (in, s, sr, block);
            const auto tag = juce::String (sr) + "/" + juce::String (block);

            for (int k = 4; k < 15; ++k)
            {
                const auto open = rms (out.l, k * step + step / 8, step / 4);
                const auto closed = rms (out.l, k * step + step / 2 + step / 8, step / 4);
                GF_EXPECT_MSG (open > 0.3 && closed < 0.003, tag + " step " + juce::String (k));
            }
        }
}

GF_TEST (GlitchDsp, microgator_pattern_depth_swing_and_stereo_offset)
{
    const auto sr = 48000.0;
    const auto step = 3000;                              // 1/32 at 120 bpm
    const auto in = sine (step * 32, 1000.0, sr);
    Settings s;
    s.on = true;
    s.gateMode = ClockMode::Tempo;
    s.gateDiv = (int) Div::D32;
    s.gateLen01 = 1.0f;
    s.gateAttackMs = 0.1f;
    s.gateReleaseMs = 0.1f;
    s.gatePattern = GatePattern::Offbeat;
    s.gateDepth01 = 0.5f;
    const auto off = render (in, s, sr, 512);
    GF_EXPECT_NEAR (rms (off.l, 16 * step + 500, 2000), 0.5 * rms (in.l, 16 * step + 500, 2000), 0.02);   // even: closed to 50 %
    GF_EXPECT_NEAR (rms (off.l, 17 * step + 500, 2000), rms (in.l, 17 * step + 500, 2000), 0.02);         // odd: open

    // Swing 100 %: the odd step starts at 1.5 steps. Straight pattern, 50 % length, full depth.
    s.gatePattern = GatePattern::Straight;
    s.gateDepth01 = 1.0f;
    s.gateLen01 = 0.5f;
    s.swing01 = 1.0f;
    const auto swung = render (in, s, sr, 512);
    const auto pair = 16 * step;                          // an even step start
    GF_EXPECT (rms (swung.l, pair + 100, 2000) > 0.3);    // open 0 .. 0.75 step
    GF_EXPECT (rms (swung.l, pair + 3200, 1200) < 0.003); // still in the long first step, closed
    GF_EXPECT (rms (swung.l, pair + 4600, 500) > 0.3);    // the swung second step opened at 1.5 steps

    // Stereo offset 100 %: right gate half a step later.
    s.swing01 = 0.0f;
    s.gateStereo01 = 1.0f;
    const auto wide = render (in, s, sr, 512);
    GF_EXPECT (rms (wide.l, pair + 200, 1000) > 0.3 && rms (wide.r, pair + 200, 1000) < 0.003);
}

//==============================================================================
GF_TEST (GlitchDsp, sweep_filter_modes_have_the_expected_response_at_every_rate)
{
    for (const auto sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto n = (int) sr;
        Settings s;
        s.on = true;
        s.filtDepth01 = 0.0f;         // fixed at the minimum cutoff
        s.filtMinHz = 1000.0f;
        s.filtMaxHz = 8000.0f;
        s.filtRes01 = 0.0f;
        const auto tag = juce::String (sr);
        const auto level = [&] (FilterMode mode, double hz)
        {
            s.filtMode = mode;
            const auto out = render (sine (n, hz, sr), s, sr, 512);
            return rms (out.l, n / 2, n / 4) / (0.5 / std::sqrt (2.0));
        };

        GF_EXPECT_MSG (level (FilterMode::LowPass, 100.0) > 0.95 && level (FilterMode::LowPass, 10000.0) < 0.03, "LP " + tag);
        GF_EXPECT_MSG (level (FilterMode::HighPass, 10000.0) > 0.95 && level (FilterMode::HighPass, 100.0) < 0.03, "HP " + tag);
        GF_EXPECT_MSG (std::abs (level (FilterMode::BandPass, 1000.0) - 1.0) < 0.05, "BP " + tag);
    }
}

GF_TEST (GlitchDsp, sweep_lfo_covers_min_to_max_and_follows_the_bar)
{
    Sweep sweep;
    const auto sr = 48000.0;
    sweep.prepare (sr);
    Settings s;
    s.filtMode = FilterMode::LowPass;
    s.filtShape = LfoShape::RampUp;
    s.filtDiv = (int) Div::Bar1;      // 4 beats = 96000 samples at 120 bpm
    s.filtMinHz = 200.0f;
    s.filtMaxHz = 8000.0f;
    BeatClock clock;
    clock.begin (true, 0.0, 24000.0);
    double lo = 1e9, hi = 0.0, atQuarter = 0.0;

    for (int i = 0; i < 96000; ++i)
    {
        float l = 0.0f, r = 0.0f;
        const auto c = clock.next();
        sweep.process (s, timing (sr), c, l, r);
        lo = std::min (lo, sweep.getCutoffLeft());
        hi = std::max (hi, sweep.getCutoffLeft());
        if (i == 24000)
            atQuarter = sweep.getCutoffLeft();
    }

    GF_EXPECT_NEAR (lo, 200.0, 1.0);
    GF_EXPECT (hi > 7900.0 && hi <= 8000.0);
    GF_EXPECT_NEAR (atQuarter, 200.0 * std::pow (40.0, 0.25), 2.0);   // exponential sweep, a quarter of the way
}

GF_TEST (GlitchDsp, phaser_notches_where_the_allpass_chain_is_out_of_phase)
{
    const auto sr = 48000.0;
    Settings s;
    s.on = true;
    s.phMix01 = 1.0f;
    s.phDepth01 = 0.0f;               // static notches
    s.phCentreHz = 1000.0f;
    s.phStages = 4;
    s.phFeedback = 0.0f;
    const auto level = [&] (double hz)
    {
        const auto out = render (sine ((int) sr, hz, sr), s, sr, 512);
        return rms (out.l, 24000, 12000) / (0.5 / std::sqrt (2.0));
    };

    // Four stages at fc: -90 deg each at fc (in phase, peak); -45 deg each at fc * tan (22.5 deg) (notch).
    GF_EXPECT (level (1000.0) > 0.95);
    GF_EXPECT (level (1000.0 * std::tan (kPi / 8.0)) < 0.05);
}

GF_TEST (GlitchDsp, polarity_pan_inversion_and_mono_fold)
{
    const auto sr = 48000.0;
    Stereo in (4800);
    for (int i = 0; i < in.size(); ++i)
    {
        in.l[(std::size_t) i] = 0.5f;
        in.r[(std::size_t) i] = 0.25f;
    }

    Settings s;
    s.on = true;
    s.polLeft = true;
    auto out = render (in, s, sr, 128);
    GF_EXPECT_NEAR (out.l[4000], -0.5f, 1e-6f);
    GF_EXPECT_NEAR (out.r[4000], 0.25f, 1e-6f);

    s.polLeft = false;
    s.panInvert01 = 1.0f;            // swap = mirror image
    out = render (in, s, sr, 128);
    GF_EXPECT_NEAR (out.l[4000], 0.25f, 1e-6f);
    GF_EXPECT_NEAR (out.r[4000], 0.5f, 1e-6f);

    // Mono bus: one-sided inversion cancels in the fold, both sides do not.
    GlitchRack rack;
    rack.prepare (sr);
    std::vector<float> mono (4800, 0.5f);
    Settings one;
    one.on = true;
    one.polLeft = true;
    for (int start = 0; start < 4800; start += 480)
        rack.process (mono.data() + start, nullptr, 480, one, timing (sr));
    GF_EXPECT_NEAR (mono[4000], 0.0f, 1e-6f);

    GlitchRack both;
    both.prepare (sr);
    std::vector<float> mono2 (4800, 0.5f);
    one.polRight = true;
    for (int start = 0; start < 4800; start += 480)
        both.process (mono2.data() + start, nullptr, 480, one, timing (sr));
    GF_EXPECT_NEAR (mono2[4000], -0.5f, 1e-6f);
}

GF_TEST (GlitchDsp, rhythmic_flip_and_switching_never_jump)
{
    const auto sr = 48000.0;
    Stereo in (96000);
    for (int i = 0; i < in.size(); ++i)
        in.l[(std::size_t) i] = in.r[(std::size_t) i] = 0.5f;

    Settings s;
    s.on = true;
    s.flipMode = FlipMode::InvertBoth;
    s.flipDiv = (int) Div::D16;
    s.flipProb01 = 0.5f;
    const auto out = render (in, s, sr, 300, 120.0, [] (int start, Settings& st)
    {
        st.gateMode = start > 40000 && start < 60000 ? ClockMode::Tempo : ClockMode::Off;   // engage / disengage
    });

    float maxStep = 0.0f;
    int flips = 0;
    for (int i = 1; i < out.size(); ++i)
    {
        maxStep = std::max (maxStep, std::abs (out.l[(std::size_t) i] - out.l[(std::size_t) i - 1]));
        flips += (out.l[(std::size_t) i] < 0.0f) != (out.l[(std::size_t) i - 1] < 0.0f) ? 1 : 0;
    }

    GF_EXPECT (flips > 4);                    // it does flip on some steps
    GF_EXPECT (maxStep < 0.06f);              // DC 0.5: rack, flip glide and gate attack all ramp
}

GF_TEST (GlitchDsp, transport_stop_seek_and_tempo_change_stay_continuous_and_finite)
{
    const auto sr = 48000.0;
    GlitchRack rack;
    rack.prepare (sr);
    auto s = everythingOn();
    auto x = noise ((int) sr * 6);
    auto t = timing (sr);
    double ppq = 0.0;

    for (int start = 0, b = 0; start < x.size(); start += 480, ++b)
    {
        t.playing = (b / 50) % 2 == 0;                         // start/stop every 0.5 s
        t.samplesPerBeat = 60.0 / (b < 300 ? 120.0 : 174.0) * sr;   // tempo change
        if (b == 120)
            ppq = 64.0;                                        // seek
        t.ppqAtStart = ppq;
        s.stab = b > 200 && b < 260;
        rack.process (x.l.data() + start, x.r.data() + start, 480, s, t);
        ppq += 480.0 / t.samplesPerBeat;
    }

    GF_EXPECT (finiteAndBounded (x, 8.0f));
}

GF_TEST (GlitchDsp, bypass_and_switch_off_fade_out_then_become_bit_identical)
{
    const auto sr = 48000.0;
    const auto in = noise (48000);
    Settings s = everythingOn();
    const auto out = render (in, s, sr, 480, 120.0, [] (int start, Settings& st) { st.on = start < 20000; });

    // 20 ms fade after the switch-off block, then the input passes untouched.
    bool identicalTail = true;
    for (int i = 20160 + 960 + 1; i < 48000; ++i)
        identicalTail = identicalTail && out.l[(std::size_t) i] == in.l[(std::size_t) i];
    GF_EXPECT (identicalTail);

    float maxJump = 0.0f;
    for (int i = 20160; i < 20160 + 960; ++i)
        maxJump = std::max (maxJump, std::abs ((out.l[(std::size_t) i] - in.l[(std::size_t) i])
                                               - (out.l[(std::size_t) i - 1] - in.l[(std::size_t) i - 1])));
    GF_EXPECT (maxJump < 1.0f);
}

GF_TEST (GlitchDsp, cpu_budget_every_module_on)
{
    const auto sr = 48000.0;
    auto x = noise ((int) sr * 10);
    GlitchRack rack;
    rack.prepare (sr);
    const auto s = everythingOn();
    auto t = timing (sr);
    const auto begin = std::chrono::steady_clock::now();

    for (int start = 0; start < x.size(); start += 512)
    {
        t.ppqAtStart = start / t.samplesPerBeat;
        rack.process (x.l.data() + start, x.r.data() + start, std::min (512, x.size() - start), s, t);
    }

    const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - begin).count();
    std::printf ("    glitch rack, every module on: 10 s of 48 kHz stereo in %.1f ms (%.0fx real time)\n",
                 seconds * 1000.0, 10.0 / std::max (1.0e-9, seconds));
    // Generous bound: also holds under sanitizers on a slow runner.
    GF_EXPECT (seconds < 5.0);
}
