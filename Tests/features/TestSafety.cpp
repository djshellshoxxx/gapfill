// F-L25 live safety suite (docs/dev/12 F-L25): wet safety limiter and last-good-scene.

#include "../support/LiveRig.h"

#include "features/panic/WetSafety.h"

#include <cmath>

using namespace gapfill;
using namespace gapfill::panic;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;

    double dbOf (double ratio) { return 20.0 * std::log10 (ratio); }
}

GF_TEST (Safety, wetsafety_is_a_registered_performance_parameter_off_by_default)
{
    GapFillAudioProcessor proc;
    auto* p = proc.getValueTreeState().getParameter ("wetSafety");
    GF_EXPECT (p != nullptr && p->getDefaultValue() < 0.5f);
    GF_EXPECT (performanceParameterIDs().contains ("wetSafety"));
    GF_EXPECT (! randomisableParameterIDs().contains ("wetSafety"));
    GF_EXPECT (allParameterIDs().indexOf ("wetSafety") > allParameterIDs().indexOf ("capture"));       // appended
}

GF_TEST (Safety, limiter_bounds_every_input_up_to_plus_24_dbfs_and_is_odd_symmetric)
{
    for (double db = -120.0; db <= 24.0; db += 0.25)
    {
        const auto x = (float) std::pow (10.0, db / 20.0);
        const auto y = wetSafetySample (x);
        GF_EXPECT_MSG (std::abs (y) < kWetSafetyCeiling + 1.0e-6f, "db " + juce::String (db));
        GF_EXPECT (std::isfinite (y));
        GF_EXPECT (wetSafetySample (-x) == -y);
    }

    float previous = 0.0f;

    for (float x = 0.0f; x < 16.0f; x += 0.01f)             // monotonic: a limiter never inverts the waveform
    {
        const auto y = wetSafetySample (x);
        GF_EXPECT (y >= previous - 1.0e-7f);
        previous = y;
    }

    GF_EXPECT (wetSafetySample (0.0f) == 0.0f);
}

GF_TEST (Safety, limiter_is_transparent_below_minus_8_dbfs_and_deviates_0_21_db_at_minus_6)
{
    for (double db = -120.0; db <= -8.0; db += 0.5)
    {
        const auto x = (float) std::pow (10.0, db / 20.0);
        GF_EXPECT_MSG (std::abs (dbOf (wetSafetySample (x) / x)) < 0.1, "db " + juce::String (db));
    }

    const auto atMinus6 = (float) std::pow (10.0, -6.0 / 20.0);
    GF_EXPECT_NEAR (dbOf (wetSafetySample (atMinus6) / atMinus6), -0.206, 0.01);
}

GF_TEST (Safety, limiter_is_stateless_and_allocation_free)
{
    float l[64], r[64];

    for (int i = 0; i < 64; ++i)
        l[i] = r[i] = 3.0f * std::sin (0.3f * (float) i);

    float l2[64], r2[64];
    std::copy (l, l + 64, l2);
    std::copy (r, r + 64, r2);

    wetSafetyProcess (l, r, 64);

    for (int i = 0; i < 64; ++i)
        wetSafetyProcess (l2 + i, r2 + i, 1);                // one sample at a time: identical => no memory

    for (int i = 0; i < 64; ++i)
        GF_EXPECT (l[i] == l2[i] && r[i] == r2[i]);
}

GF_TEST (Safety, processor_bounds_a_hot_wet_bus_when_on_and_is_unchanged_below_the_knee_when_off)
{
    auto render = [] (float gain, bool safety)
    {
        GapFillAudioProcessor proc;
        setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "dry", 0.0f }, { "wet", 100.0f },
                           { "wetSafety", safety ? 1.0f : 0.0f } });
        auto input = drumLoop (kSr, 120.0, 3);
        input.applyGain (gain);
        TestPlayHead ph;
        return renderLive (proc, input, 512, kSr, ph, {});
    };

    const auto hot = render (15.8f, true);                    // +24 dBFS source
    float peak = 0.0f;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < hot.out.getNumSamples(); ++i)
        {
            peak = std::max (peak, std::abs (hot.out.getSample (ch, i)));
            GF_EXPECT (std::isfinite (hot.out.getSample (ch, i)));
        }

    GF_EXPECT_MSG (peak <= kWetSafetyCeiling + 1.0e-4f, "peak " + juce::String (peak));
    GF_EXPECT (peak > 0.3f);                                  // it did produce a wet signal

    // quiet material: on vs off differ by < 0.1 dB everywhere
    const auto on = render (0.05f, true);
    const auto off = render (0.05f, false);

    for (int i = 0; i < on.out.getNumSamples(); ++i)
    {
        const auto a = on.out.getSample (0, i), b = off.out.getSample (0, i);

        if (std::abs (b) > 1.0e-4f && std::abs (b) < 0.35f)          // below -9 dBFS the limiter is transparent
            GF_EXPECT_MSG (std::abs (dbOf (a / b)) < 0.1, "sample " + juce::String (i) + " a=" + juce::String (a) + " b=" + juce::String (b));
    }
}

GF_TEST (Safety, last_good_scene_recall_restores_the_committed_scene_after_edits)
{
    GapFillAudioProcessor proc;
    auto& live = proc.getLiveControl();
    scenes::initTables();
    scenes::Scene s;
    s.name = "Good";
    s.scope = scenes::ScopeCreative;
    s.params.push_back ({ (std::uint16_t) scenes::paramIndexOf ("fill"), 0.8f });
    s.params.push_back ({ (std::uint16_t) scenes::paramIndexOf ("chaos"), 0.1f });
    live.scenes().getBank().store (0, s);
    live.scenes().getBank().options.quant = scenes::Quant::Immediate;

    prepareProcessor (proc, kSr, 256, false);
    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    work.clear();
    live.scenes().gotoScene (0);
    proc.processBlock (work, midi);
    proc.flushLatencyReportForTests();
    GF_EXPECT (live.scenes().getLastGoodIndex() == 0);

    setParams (proc, { { "fill", 5.0f }, { "chaos", 99.0f } });         // the performance goes wrong

    commands::Binding b;
    b.cmd = CommandType::SceneGoto;
    b.arg = -1;                                                        // "last good scene"
    b.type = commands::MsgType::Note;
    b.number = 71;
    live.bindings().add (b);

    midi.addEvent (juce::MidiMessage::noteOn (1, 71, (juce::uint8) 100), 0);
    proc.processBlock (work, midi);
    midi.clear();
    proc.processBlock (work, midi);                                    // executes the queued command
    proc.processBlock (work, midi);
    proc.flushLatencyReportForTests();                                 // UiRequest -> overlay published
    proc.processBlock (work, midi);                                    // overlay consumed, applied
    proc.flushLatencyReportForTests();                                 // committed

    GF_EXPECT_NEAR (pv (proc.getValueTreeState().getRawParameterValue ("fill")), 80.0, 0.01);
    GF_EXPECT_NEAR (pv (proc.getValueTreeState().getRawParameterValue ("chaos")), 10.0, 0.01);
    GF_EXPECT (! live.panic().isPanicked());                           // Recover is never implied by last-good (and vice versa)
}

GF_TEST (Safety, recover_does_not_imply_last_good_scene)
{
    GapFillAudioProcessor proc;
    auto& live = proc.getLiveControl();
    prepareProcessor (proc, kSr, 256, false);
    juce::AudioBuffer<float> work (2, 256);
    juce::MidiBuffer midi;
    work.clear();

    live.panic().requestPanic();
    proc.processBlock (work, midi);
    live.panic().requestRecover();
    proc.processBlock (work, midi);
    proc.flushLatencyReportForTests();
    GF_EXPECT (live.scenes().getCommitCount() == 0 && live.scenes().getPendingIndex() == -1);
}
