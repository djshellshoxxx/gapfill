// F-D03 Quality modes and adaptive quality (docs/dev/12 F-D03).

#include "../support/TestRig.h"
#include "../support/VoiceRig.h"

#include "dsp/SpectralSmear.h"
#include "features/cloud/CloudMode.h"
#include "features/fixit/Fixit.h"
#include "features/quality/Quality.h"

#include <cmath>
#include <cstring>

using namespace gapfill;
using namespace gftest;
using namespace gapfill::quality;

namespace
{
    constexpr double kSr = 48000.0;

    /** Drives the governor with a CPU series sampled every 10 ms; returns the level changes (time, new level). */
    struct Trace
    {
        std::vector<std::pair<double, int>> changes;
        int final = 0;
        double endTime = 0.0;
    };

    template <typename Series>
    Trace drive (AutoGovernor& g, double seconds, Series&& cpuAt, std::uint32_t overruns = 0)
    {
        Trace t;
        int last = g.level();

        for (double now = 0.0; now < seconds; now += 0.01)
        {
            const auto level = g.update (0.01, cpuAt (now), overruns);

            if (level != last)
            {
                t.changes.emplace_back (now, level);
                last = level;
            }
        }

        t.final = g.level();
        t.endTime = seconds;
        return t;
    }
}

GF_TEST (Quality, table_full_equals_the_baseline_constants_and_lower_levels_only_reduce)
{
    GF_EXPECT (kCaps[Full].voices == FillEngine::numVoices);
    GF_EXPECT (kCaps[Full].cloudGrains == fx::kCloudMaxGrains);
    GF_EXPECT (kCaps[Full].tailScale == 1.0f && ! kCaps[Full].spectralLower && ! kCaps[Full].spectralCardsOff);

    GF_EXPECT (kCaps[Balanced].voices == 3 && kCaps[Balanced].cloudGrains == 32 && kCaps[Balanced].tailScale == 0.5f);
    GF_EXPECT (kCaps[Lite].voices == 2 && kCaps[Lite].cloudGrains == 16 && kCaps[Lite].tailScale == 0.25f && kCaps[Lite].spectralCardsOff);

    for (int i = 1; i < NumLevels; ++i)
        GF_EXPECT (kCaps[i].voices <= kCaps[i - 1].voices && kCaps[i].cloudGrains <= kCaps[i - 1].cloudGrains
                   && kCaps[i].tailScale <= kCaps[i - 1].tailScale);

    GF_EXPECT (&capsFor (-3) == &kCaps[0] && &capsFor (9) == &kCaps[NumLevels - 1]);
}

GF_TEST (Quality, auto_steps_down_after_three_hot_seconds_and_respects_the_dwell)
{
    AutoGovernor g;
    const auto t = drive (g, 20.0, [] (double) { return 0.90f; });

    GF_EXPECT (t.changes.size() == 2);
    GF_EXPECT (t.changes[0].second == 1 && t.changes[0].first >= 2.9 && t.changes[0].first <= 3.2);
    GF_EXPECT (t.changes[1].second == 2);
    GF_EXPECT_MSG (t.changes[1].first - t.changes[0].first >= 10.0 && t.changes[1].first - t.changes[0].first <= 13.5,
                   "second step after " + juce::String (t.changes[1].first - t.changes[0].first));
    GF_EXPECT (t.final == 2);                                            // never beyond Lite
}

GF_TEST (Quality, auto_steps_up_only_after_twenty_calm_seconds_one_level_at_a_time)
{
    AutoGovernor g;
    drive (g, 14.0, [] (double) { return 0.95f; });                      // down to Lite
    GF_EXPECT (g.level() == 2);

    const auto t = drive (g, 70.0, [] (double) { return 0.30f; });
    GF_EXPECT (t.changes.size() == 2);
    GF_EXPECT (t.changes[0].second == 1 && t.changes[0].first >= 19.5 && t.changes[0].first <= 21.0);
    GF_EXPECT (t.changes[1].second == 0 && t.changes[1].first - t.changes[0].first >= 19.5);
}

GF_TEST (Quality, auto_does_not_flap_on_a_noisy_load_around_the_thresholds)
{
    // 5 minutes of a load that swings across both thresholds every 1.5 s: never 3 s above 85 %, never 20 s below 55 %.
    AutoGovernor g;
    int changes = 0;
    int last = g.level();
    juce::Random rng (21);

    for (double now = 0.0; now < 300.0; now += 0.01)
    {
        const auto swing = 0.5f + 0.45f * std::sin ((float) now * 2.0f * juce::MathConstants<float>::pi / 3.0f);
        const auto cpu = juce::jlimit (0.0f, 1.0f, swing + 0.03f * (rng.nextFloat() - 0.5f));
        const auto level = g.update (0.01, cpu, 0);

        if (level != last)
        {
            ++changes;
            last = level;
        }
    }

    GF_EXPECT_MSG (changes == 0, "changes " + juce::String (changes));

    // a load that sits just above the hot threshold changes at most once per dwell
    AutoGovernor h;
    const auto t = drive (h, 300.0, [] (double now) { return 0.86f + 0.02f * (float) std::sin (now); });
    GF_EXPECT (t.changes.size() <= 2);                                   // Full -> Balanced -> Lite, then it stays
}

GF_TEST (Quality, two_deadline_overruns_within_five_seconds_step_down_one_does_not)
{
    AutoGovernor one;
    one.update (1.0, 0.3f, 0);
    one.update (0.5, 0.3f, 1);
    for (int i = 0; i < 100; ++i) one.update (0.1, 0.3f, 1);
    GF_EXPECT (one.level() == 0);

    AutoGovernor two;
    two.update (1.0, 0.3f, 0);
    two.update (0.5, 0.3f, 1);
    two.update (2.0, 0.3f, 1);
    two.update (0.01, 0.3f, 2);
    GF_EXPECT (two.level() == 1);

    AutoGovernor spread;
    spread.update (1.0, 0.3f, 0);
    spread.update (0.5, 0.3f, 1);
    spread.update (6.0, 0.3f, 1);
    spread.update (0.01, 0.3f, 2);
    GF_EXPECT (spread.level() == 0);                                     // 6 s apart: no trigger
}

GF_TEST (Quality, overruns_before_auto_was_engaged_do_not_count)
{
    AutoGovernor g;
    g.update (0.01, 0.2f, 40);                                           // 40 historic overruns, first call seeds
    g.update (0.01, 0.2f, 40);
    GF_EXPECT (g.level() == 0);
}

GF_TEST (Quality, controller_applies_only_when_idle_unless_pending_too_long)
{
    QualityController c;

    // manual Lite requested while a voice is sounding: nothing changes, then a forced fade after 1 s
    auto s = c.update (ModeLite, false, 0.5, 0.1f, 0, false);
    GF_EXPECT (s.level == Full && ! s.changed && ! s.forceFade);
    s = c.update (ModeLite, false, 0.4, 0.1f, 0, false);
    GF_EXPECT (s.level == Full && ! s.changed);
    s = c.update (ModeLite, false, 0.2, 0.1f, 0, false);
    GF_EXPECT (s.level == Lite && s.changed && s.forceFade);

    // back to Full: applied at the first idle moment, no forced fade
    s = c.update (ModeFull, false, 0.01, 0.1f, 0, false);
    GF_EXPECT (s.level == Lite && ! s.changed);
    s = c.update (ModeFull, false, 0.01, 0.1f, 0, true);
    GF_EXPECT (s.level == Full && s.changed && ! s.forceFade);

    // nothing pending: nothing happens
    s = c.update (ModeFull, false, 0.01, 0.1f, 0, true);
    GF_EXPECT (! s.changed && ! s.forceFade);
}

GF_TEST (Quality, auto_is_full_when_rendering_offline_and_frozen)
{
    QualityController c;

    for (int i = 0; i < 2000; ++i)
    {
        const auto s = c.update (ModeAuto, true, 0.01, 0.99f, 0, true);
        GF_EXPECT (s.level == Full);
    }

    // going back to real time starts from Full: the offline hot load was never counted
    auto s = c.update (ModeAuto, false, 0.01, 0.99f, 0, true);
    GF_EXPECT (s.level == Full);
    GF_EXPECT (c.autoLevel() == Full);
}

GF_TEST (Quality, footer_text)
{
    char buf[64];
    GF_EXPECT (std::string (footerText (ModeFull, Full, buf, 64)).empty());
    GF_EXPECT (std::string (footerText (ModeLite, Lite, buf, 64)) == "Quality: Lite");
    GF_EXPECT (std::string (footerText (ModeAuto, Balanced, buf, 64)) == "Quality: Balanced (auto)");
    GF_EXPECT (std::string (footerText (ModeAuto, Full, buf, 64)) == "Quality: Full (auto)");
}

//==============================================================================
namespace
{
    juce::AudioBuffer<float> renderQuality (std::initializer_list<std::pair<const char*, float>> params,
                                            const std::function<void (int, GapFillAudioProcessor&)>& onBlock = {},
                                            int* appliedOut = nullptr, int* latencyOut = nullptr, int bars = 3)
    {
        GapFillAudioProcessor proc;
        setParams (proc, params);
        TestPlayHead ph;
        ph.bpm = 150.0;
        RenderOptions o;
        o.playHead = &ph;
        o.onBlock = [&] (int idx, int) { if (onBlock) onBlock (idx, proc); };
        auto r = renderOffline (proc, drumLoop (kSr, 150.0, bars), 512, kSr, o);

        if (appliedOut != nullptr)
            *appliedOut = proc.getQualityController().appliedLevel();

        if (latencyOut != nullptr)
        {
            proc.flushLatencyReportForTests();
            *latencyOut = proc.getLatencySamples();
        }

        return std::move (r.out);
    }
}

GF_TEST (Quality, parameter_is_registered_full_by_default_and_never_randomised)
{
    GapFillAudioProcessor proc;
    GF_EXPECT (proc.getValueTreeState().getRawParameterValue ("qualityMode")->load() == 0.0f);
    GF_EXPECT (performanceParameterIDs().contains ("qualityMode"));
    GF_EXPECT (! randomisableParameterIDs().contains ("qualityMode"));
}

GF_TEST (Quality, full_is_the_baseline_and_the_level_in_force_is_reported)
{
    int applied = -1;
    const auto a = renderQuality ({ { "fill", 100.0f }, { "density", 100.0f } }, {}, &applied);
    const auto b = renderQuality ({ { "fill", 100.0f }, { "density", 100.0f }, { "qualityMode", 0.0f } });
    GF_EXPECT (applied == Full);
    GF_EXPECT (buffersIdentical (a, b));
}

GF_TEST (Quality, lite_and_balanced_apply_at_the_first_idle_moment_and_keep_the_latency)
{
    for (int mode : { 1, 2 })
    {
        int applied = -1, latency = -1, fullLatency = -1;
        renderQuality ({ { "fill", 100.0f }, { "density", 100.0f }, { "qualityMode", (float) mode } }, {}, &applied, &latency);
        renderQuality ({ { "fill", 100.0f }, { "density", 100.0f } }, {}, nullptr, &fullLatency);
        GF_EXPECT_MSG (applied == mode, "mode " + juce::String (mode) + " applied " + juce::String (applied));
        GF_EXPECT (latency == fullLatency && latency > 0);
    }
}

GF_TEST (Quality, a_mid_render_switch_is_finite_and_ends_at_the_requested_level)
{
    int applied = -1;
    const auto out = renderQuality ({ { "fill", 100.0f }, { "density", 100.0f }, { "complexity", 100.0f } },
                                    [] (int idx, GapFillAudioProcessor& p)
                                    {
                                        if (idx == 60)
                                            setParams (p, { { "qualityMode", 2.0f } });
                                    }, &applied, nullptr, 4);

    GF_EXPECT (applied == Lite);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < out.getNumSamples(); ++i)
            GF_EXPECT (std::isfinite (out.getSample (ch, i)));
}

GF_TEST (Quality, auto_behaves_as_full_in_a_non_realtime_render)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "qualityMode", 3.0f }, { "fill", 100.0f } });
    proc.setNonRealtime (true);
    TestPlayHead ph;
    RenderOptions o;
    o.playHead = &ph;
    renderOffline (proc, drumLoop (kSr, 150.0, 2), 512, kSr, o);
    GF_EXPECT (proc.getQualityController().appliedLevel() == Full);
    proc.setNonRealtime (false);
}

GF_TEST (Quality, state_roundtrip_keeps_the_mode)
{
    GapFillAudioProcessor a;
    setParams (a, { { "qualityMode", 3.0f } });
    juce::MemoryBlock state;
    a.getStateInformation (state);
    GapFillAudioProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    GF_EXPECT (b.getValueTreeState().getRawParameterValue ("qualityMode")->load() == 3.0f);
}

GF_TEST (Quality, cards_are_disabled_in_lite_and_tails_and_grains_follow_the_level)
{
    GapFillAudioProcessor proc;
    EngineSettings s;
    s.fx.formant.amount01 = 1.0f;
    s.fx.spfreeze.amount01 = 1.0f;
    s.fx.convo.amount01 = 1.0f;
    s.fx.resonator.amount01 = 1.0f;

    auto enabled = [&] (fx::CardId id)
    {
        auto& chain = proc.getFxChain();

        for (int i = 0; i < chain.numBusCards(); ++i)
            if (chain.busCard (i)->id() == id)
                return chain.busCard (i)->enabled (s.fx);

        GF_EXPECT (false);                                                // the card must exist
        return false;
    };

    prepareProcessor (proc, kSr, 512, false);
    quality::applyToSettings (s, Full);
    GF_EXPECT (enabled (fx::CardId::Formant) && enabled (fx::CardId::SpFreeze) && enabled (fx::CardId::Convo) && enabled (fx::CardId::Resonator));
    GF_EXPECT (s.fx.tailCapScale == 1.0f && s.fx.cloud.maxGrains == 48 && s.perf.qualityLevel == Full);

    quality::applyToSettings (s, Balanced);
    GF_EXPECT (enabled (fx::CardId::Formant) && s.fx.tailCapScale == 0.5f && s.fx.cloud.maxGrains == 32);

    quality::applyToSettings (s, Lite);
    GF_EXPECT (! enabled (fx::CardId::Formant) && ! enabled (fx::CardId::SpFreeze) && ! enabled (fx::CardId::Convo));
    GF_EXPECT (enabled (fx::CardId::Resonator));                                   // the cheap cards stay
    GF_EXPECT (s.fx.tailCapScale == 0.25f && s.fx.cloud.maxGrains == 16 && s.perf.qualityLevel == Lite);
}

GF_TEST (Quality, the_cloud_grain_cap_follows_the_level)
{
    VoiceRig rig;

    for (int cap : { 48, 32, 16 })
    {
        fx::CloudSettings c { 1.0f, 1000.0f, 250.0f, 600.0f, 0.4f, 100.0f, 0.6f };
        c.maxGrains = cap;
        FillVoice voice;
        voice.prepare (kSr, 2);
        auto setup = rig.setup (Algo::Granular, 1500.0, 5);
        setup.modes.cloud = true;
        setup.fxStart.settings.cloud = c;
        setup.fxStart.settings.cloud.amount01 = 1.0f;
        rig.spectral.release();
        GF_EXPECT (voice.start (setup, rig.history, &rig.spectral));

        juce::AudioBuffer<float> wet (2, 64);
        int worst = 0;

        for (int block = 0; block < 900; ++block)
        {
            wet.clear();
            voice.render (wet, 0, 64, rig.history);
            worst = std::max (worst, voice.getCloudMode().activeGrains());
        }

        GF_EXPECT_MSG (worst <= cap && worst > 0, "cap " + juce::String (cap) + " worst " + juce::String (worst));
    }
}

GF_TEST (Quality, the_spectral_smear_runs_one_order_lower_and_stays_bounded)
{
    for (bool lower : { false, true })
    {
        SpectralSmear smear;
        smear.prepare (kSr, 2);
        smear.setLowerOrder (lower);
        GF_EXPECT (smear.latencyNow() == (lower ? SpectralSmear::latency() / 2 : SpectralSmear::latency()));

        juce::Random rng (4);
        float maxOut = 0.0f;
        bool finite = true;

        for (int i = 0; i < 48000; ++i)
        {
            const auto x = 0.3f * (rng.nextFloat() * 2.0f - 1.0f);
            float l, r;
            smear.process (x, x, l, r);
            maxOut = std::max (maxOut, std::max (std::abs (l), std::abs (r)));
            finite = finite && std::isfinite (l) && std::isfinite (r);
        }

        GF_EXPECT (finite);
        GF_EXPECT_MSG (maxOut < 2.0f && maxOut > 0.02f, "lower " + juce::String ((int) lower) + " max " + juce::String (maxOut));
    }
}

GF_TEST (Quality, the_fix_it_overload_rule_r11_proposes_balanced_on_the_real_parameter)
{
    // R11 was inactive until qualityMode existed. With the real APVTS: Full + 5 s of OverloadWarn -> "Quality -> Balanced";
    // applying it moves the real parameter to Balanced; any other mode proposes nothing.
    GapFillAudioProcessor proc;
    fixit::ApvtsParamAccess access (proc.getValueTreeState());
    fixit::Evidence ev;
    ev.overloadWarnSeconds = 5.0f;

    const auto proposals = fixit::evaluateRules (ev, access);
    const fixit::Proposal* r11 = nullptr;

    for (const auto& p : proposals)
        if (p.rule == fixit::RuleId::Overload)
            r11 = &p;

    GF_EXPECT (r11 != nullptr);

    if (r11 != nullptr)
    {
        GF_EXPECT (r11->changes.size() == 1 && r11->changes[0].paramId == "qualityMode");
        access.setNormalised ("qualityMode", access.toNormalised ("qualityMode", r11->changes[0].to));
        GF_EXPECT (proc.getValueTreeState().getRawParameterValue ("qualityMode")->load() == 1.0f);
    }

    for (const auto& p : fixit::evaluateRules (ev, access))
        GF_EXPECT (p.rule != fixit::RuleId::Overload);                 // already Balanced: nothing to propose
}
