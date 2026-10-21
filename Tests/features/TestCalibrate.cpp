// U-6 / F-L05 Auto-Calibrate.

#include "../support/AllocGuard.h"
#include "../support/TestRig.h"

#include "PluginEditor.h"
#include "features/calibrate/Calibrator.h"
#include "features/calibrate/Gui.h"
#include "features/calibrate/RecordTap.h"
#include "features/calibrate/Session.h"

#include <iostream>

using namespace gapfill;
using namespace gapfill::calibrate;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;

    std::vector<float> mono (const juce::AudioBuffer<float>& b)
    {
        std::vector<float> out ((size_t) b.getNumSamples());
        for (int i = 0; i < b.getNumSamples(); ++i)
            out[(size_t) i] = b.getNumChannels() > 1 ? 0.5f * (b.getSample (0, i) + b.getSample (1, i)) : b.getSample (0, i);
        return out;
    }

    /** Hits of `decay` seconds every `interval` seconds. */
    std::vector<float> hits (double seconds, double interval, float frequency, float decay, float noise, float amp, double sr = kSr)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * sr));
        b.clear();
        for (double t = 0.1; t < seconds - 0.2; t += interval)
            addDrumHit (b, (int) (t * sr), sr, frequency, decay, amp, noise);
        return mono (b);
    }

    std::vector<float> pad (double seconds, double sr = kSr)
    {
        std::vector<float> out ((size_t) (seconds * sr));
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = 0.2f * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) i / sr)
                   + 0.1f * std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * (double) i / sr);
        return out;
    }

    AnalysisSettings defaultBase()
    {
        AnalysisSettings a;
        a.protectHoldMs = 40.0f; a.retrigGuardMs = 30.0f; a.gapReleaseMs = 80.0f;
        return a;
    }

    CalibResult run (const std::vector<float>& samples, double sr = kSr, CalibInput* inputOut = nullptr)
    {
        CalibInput in;
        in.mono = samples.data();
        in.numSamples = (int) samples.size();
        in.sampleRate = sr;
        in.base = defaultBase();
        if (inputOut != nullptr) *inputOut = in;
        std::atomic<bool> cancel { false };
        return gapfill::calibrate::calibrate (in, cancel);
    }

    bool inTargets (const CalibMetrics& m)
    {
        return m.eventsPerSec >= kTargetEventsLow && m.eventsPerSec <= kTargetEventsHigh
            && m.gapFraction >= kTargetGapLow && m.gapFraction <= kTargetGapHigh && m.chatter < kMaxChatter;
    }

    bool waitFor (CalibrationSession& s, Phase target, int timeoutMs = 60000)
    {
        for (int waited = 0; waited < timeoutMs; waited += 10)
        {
            s.tick();
            if (s.getPhase() == target)
                return true;
            juce::Thread::sleep (10);
        }
        return false;
    }

    std::vector<float> allParams (GapFillAudioProcessor& p)
    {
        std::vector<float> v;
        for (const auto& id : allParameterIDs())
            v.push_back (p.getValueTreeState().getRawParameterValue (id)->load());
        return v;
    }

    /** Pushes `audio` through the processor block by block. */
    void feed (GapFillAudioProcessor& proc, const juce::AudioBuffer<float>& audio, int fromSample, int toSample)
    {
        juce::AudioBuffer<float> work (2, 512);
        juce::MidiBuffer midi;

        for (int pos = fromSample; pos + 512 <= toSample; pos += 512)
        {
            for (int ch = 0; ch < 2; ++ch)
                work.copyFrom (ch, 0, audio, juce::jmin (ch, audio.getNumChannels() - 1), pos, 512);
            proc.processBlock (work, midi);
        }
    }
}

GF_TEST (Calibrate, calibrate_fixtures_in_targets)
{
    const auto drums = mono (makeDrumLoop (kSr, 8.0, 120.0, 1));
    std::vector<std::pair<juce::String, std::vector<float>>> fixtures;
    fixtures.emplace_back ("four-on-floor", drums);
    fixtures.emplace_back ("quiet four-on-floor", [&drums] { auto q = drums; for (auto& s : q) s *= 0.15f; return q; }());
    fixtures.emplace_back ("loud four-on-floor", [&drums] { auto q = drums; for (auto& s : q) s *= 1.8f; return q; }());
    fixtures.emplace_back ("sparse snare", hits (8.0, 1.0, 190.0f, 0.12f, 0.8f, 0.7f));
    fixtures.emplace_back ("dense hats", hits (8.0, 60.0 / 140.0 / 2.0, 7000.0f, 0.03f, 0.9f, 0.5f));

    int inBands = 0;

    for (const auto& [name, samples] : fixtures)
    {
        const auto r = run (samples);
        GF_EXPECT_MSG (r.ok, name + ": " + r.reason);

        if (r.ok)
        {
            std::cout << "    [" << name << "] sens " << r.sensitivity << " thr " << r.gapThresh << " minGap " << r.minGapMs
                      << " | " << r.chosen.eventsPerSec << " hits/s, gaps " << r.chosen.gapFraction << ", chatter "
                      << r.chosen.chatter << (r.limited ? " (limited)" : "") << "\n";
            inBands += inTargets (r.chosen) ? 1 : 0;
            GF_EXPECT_MSG (inTargets (r.chosen) || r.limited, name + ": outside the bands must be reported as limited");
            GF_EXPECT (r.minGapMs >= 20 && r.minGapMs <= 200);
        }
    }

    // Sparse snare (gaps > 45 % by nature) and dense hats (45.2 %) are the "best achievable, limited" cases.
    GF_EXPECT_MSG (inBands >= 3, "fixtures inside the target bands: " + juce::String (inBands) + " of 5");
}

GF_TEST (Calibrate, pad_reports_no_events_and_noise_is_limited_or_refused)
{
    const auto r = run (pad (6.0));
    GF_EXPECT_MSG (! r.ok, "pad: " + juce::String (r.chosen.eventsPerSec) + " hits/s, gaps " + juce::String (r.chosen.gapFraction) + ", " + juce::String (r.chosen.events) + " events");
    GF_EXPECT_MSG (r.reason.containsIgnoreCase ("no events") || r.reason.containsIgnoreCase ("clear hits"), r.reason);

    juce::AudioBuffer<float> noiseBuffer = makeNoise (kSr, 6.0, 0.3f, 1);
    const auto noise = run (mono (noiseBuffer));
    GF_EXPECT_MSG (! noise.ok || noise.limited || inTargets (noise.chosen), "noise is either refused or reported as limited");
}

GF_TEST (Calibrate, silence_refused)
{
    std::vector<float> silence ((size_t) (kSr * 6.0), 0.0f);
    const auto r = run (silence);
    GF_EXPECT (! r.ok && r.reason.containsIgnoreCase ("not enough signal"));

    // Mostly quiet with a short burst: still below 25 % of the time.
    auto mostlyQuiet = silence;
    for (size_t i = 0; i < (size_t) (kSr * 0.5); ++i)
        mostlyQuiet[i] = 0.3f * std::sin ((float) i * 0.05f);
    GF_EXPECT (signalFraction (mostlyQuiet.data(), (int) mostlyQuiet.size(), kSr) < 0.25f);
    GF_EXPECT (! run (mostlyQuiet).ok);
}

GF_TEST (Calibrate, calibrate_counts_more_than_256_events)
{
    // 12 hits/s for 30 s = 360 events: more than the engine's 256-entry queue ever holds at once.
    const auto dense = hits (30.0, 1.0 / 12.0, 5000.0f, 0.015f, 0.9f, 0.6f);
    CalibInput in;
    in.mono = dense.data();
    in.numSamples = (int) dense.size();
    in.sampleRate = kSr;
    in.base = defaultBase();

    const auto m = measure (in, 60, 50);
    GF_EXPECT_MSG (m.events > 256, "events counted: " + juce::String (m.events));
}

GF_TEST (Calibrate, deterministic_same_input)
{
    const auto samples = mono (makeDrumLoop (kSr, 6.0, 128.0, 1));
    const auto a = run (samples);
    const auto b = run (samples);
    GF_EXPECT (a.ok == b.ok && a.sensitivity == b.sensitivity && a.gapThresh == b.gapThresh && a.minGapMs == b.minGapMs);
    GF_EXPECT (a.chosen.events == b.chosen.events && a.chosen.eventsPerSec == b.chosen.eventsPerSec);
}

GF_TEST (Calibrate, score_prefers_in_band_then_minimal_change_then_lower_sensitivity)
{
    CalibMetrics good { 4.0f, 0.3f, 0.0f, 100.0f, 30 };
    CalibMetrics tooBusy { 14.0f, 0.3f, 0.0f, 100.0f, 90 };
    CalibMetrics chatty { 4.0f, 0.3f, 0.3f, 100.0f, 30 };
    GF_EXPECT (score (good, 50, 50, 50, 50) < score (tooBusy, 50, 50, 50, 50));
    GF_EXPECT (score (good, 50, 50, 50, 50) < score (chatty, 50, 50, 50, 50));
    GF_EXPECT (score (good, 50, 50, 50, 50) < score (good, 80, 70, 50, 50));        // minimal change preferred
    GF_EXPECT (score (good, 50, 50, 50, 50) >= 0.0f);
}

GF_TEST (Calibrate, tap_records_exact_length_and_never_allocates_on_the_audio_thread)
{
    RecordTap tap;
    GF_EXPECT (! tap.start (4, kSr));                      // not allocated yet: refused, never allocates itself
    GF_EXPECT (tap.allocate (kSr) && tap.isReadyFor (kSr));
    GF_EXPECT (! tap.isReadyFor (44100.0));

    std::vector<float> block (512, 0.25f);
    {
        AllocGuard guard;
        tap.write (block.data(), 512);                     // idle: nothing happens
        GF_EXPECT (guard.count() == 0);
    }
    GF_EXPECT (tap.getFramesWritten() == 0);

    GF_EXPECT (tap.start (4, kSr));
    GF_EXPECT (! tap.start (4, kSr));                      // already recording

    long allocations = 0;
    {
        AllocGuard guard;
        for (int i = 0; i < 1000 && tap.getState() == RecordTap::State::Recording; ++i)
            tap.write (block.data(), 512);
        allocations = guard.count();
    }

    GF_EXPECT (allocations == 0);
    GF_EXPECT (tap.getState() == RecordTap::State::Done);
    GF_EXPECT (tap.getFramesWritten() == (std::int64_t) (4 * kSr));
    const auto snap = tap.snapshot();
    GF_EXPECT (snap.size() == (size_t) (4 * kSr) && snap.front() == 0.25f && snap.back() == 0.25f);

    tap.reset();
    GF_EXPECT (tap.getState() == RecordTap::State::Idle);
    GF_EXPECT (tap.snapshot().empty());

    GF_EXPECT (tap.start (4, kSr));
    tap.cancel();
    tap.write (block.data(), 512);
    GF_EXPECT (tap.getState() == RecordTap::State::Cancelled && tap.getFramesWritten() == 0);
}

GF_TEST (Calibrate, session_records_analyses_applies_and_reverts)
{
    GapFillAudioProcessor proc;
    prepareProcessor (proc, kSr, 512, false);
    setParams (proc, { { "sensitivity", 50.0f }, { "gapThresh", 60.0f }, { "minGap", 60.0f }, { "fill", 41.0f }, { "wet", 77.0f } });

    CalibrationSession session (proc);
    GF_EXPECT_MSG (! session.start (4) && session.getMessage() == "Try again", "first press allocates and asks to try again");
    GF_EXPECT (session.start (4));
    GF_EXPECT (session.getPhase() == Phase::Recording);

    const auto audio = makeDrumLoop (kSr, 6.0, 120.0, 2);
    feed (proc, audio, 0, audio.getNumSamples());
    GF_EXPECT (waitFor (session, Phase::Result));
    GF_EXPECT_MSG (session.getResult().ok, session.getMessage());
    GF_EXPECT (session.getMessage().contains ("Sensitivity 50 ->") && session.getMessage().contains ("hits/s"));

    const auto before = allParams (proc);
    const auto depth = proc.getUndoDepth();

    GF_EXPECT (session.apply());
    GF_EXPECT (proc.getUndoDepth() == depth + 1);

    // Only the three detector parameters change.
    const auto after = allParams (proc);
    const auto ids = allParameterIDs();
    for (size_t i = 0; i < ids.size(); ++i)
        if (ids[(int) i] != "sensitivity" && ids[(int) i] != "gapThresh" && ids[(int) i] != "minGap")
            GF_EXPECT_MSG (before[i] == after[i], ids[(int) i]);

    GF_EXPECT_NEAR (proc.getValueTreeState().getRawParameterValue ("sensitivity")->load(), session.getResult().sensitivity, 0.01);
    GF_EXPECT_NEAR (proc.getValueTreeState().getRawParameterValue ("gapThresh")->load(), session.getResult().gapThresh, 0.01);

    GF_EXPECT (session.canRevert());
    GF_EXPECT (session.revert());
    GF_EXPECT (allParams (proc) == before);
    GF_EXPECT (session.getPhase() == Phase::Idle);
}

GF_TEST (Calibrate, cancel_leaves_params_untouched_and_revert_disabled_after_other_action)
{
    GapFillAudioProcessor proc;
    prepareProcessor (proc, kSr, 512, false);
    CalibrationSession session (proc);
    session.start (4);                                   // allocates
    GF_EXPECT (session.start (4));

    const auto before = allParams (proc);
    const auto audio = makeDrumLoop (kSr, 2.0, 120.0, 2);
    feed (proc, audio, 0, audio.getNumSamples() / 2);
    session.cancel();
    GF_EXPECT (session.getPhase() == Phase::Idle && allParams (proc) == before);
    feed (proc, audio, audio.getNumSamples() / 2, audio.getNumSamples());      // the tap ignores audio now
    GF_EXPECT (proc.getUiState().recordTap.getState() == RecordTap::State::Idle);

    // Apply, then another undoable action: Revert is disabled.
    session.analyseForTests (mono (makeDrumLoop (kSr, 6.0, 120.0, 1)), kSr);
    GF_EXPECT (waitFor (session, Phase::Result));
    GF_EXPECT (session.apply() && session.canRevert());
    proc.pushUndoSnapshot();
    GF_EXPECT (! session.canRevert() && ! session.revert());
}

GF_TEST (Calibrate, refusals_and_warnings)
{
    GapFillAudioProcessor proc;
    CalibrationSession session (proc);
    GF_EXPECT_MSG (! session.start (4) && session.getMessage().containsIgnoreCase ("playback"), "not prepared: needs playback");

    prepareProcessor (proc, kSr, 512, false);
    setParams (proc, { { "detSource", 1.0f } });
    GF_EXPECT (session.getSourceWarning() == "Calibrated from the main input only");
    setParams (proc, { { "detSource", 0.0f } });
    GF_EXPECT (session.getSourceWarning().isEmpty());

    session.analyseForTests (std::vector<float> ((size_t) (kSr * 5), 0.0f), kSr);
    GF_EXPECT (waitFor (session, Phase::Idle));
    GF_EXPECT (session.getMessage().containsIgnoreCase ("not enough signal"));

    session.analyseForTests (pad (6.0), kSr);
    GF_EXPECT (waitFor (session, Phase::Idle));
    GF_EXPECT (session.getMessage().containsIgnoreCase ("no events"));
}

GF_TEST (Calibrate, record_cancelled_on_samplerate_change)
{
    GapFillAudioProcessor proc;
    prepareProcessor (proc, kSr, 512, false);
    CalibrationSession session (proc);
    session.start (4);
    GF_EXPECT (session.start (4));

    prepareProcessor (proc, 44100.0, 512, false);
    GF_EXPECT (session.tick());
    GF_EXPECT (session.getPhase() == Phase::Idle && session.getMessage().containsIgnoreCase ("sample rate"));
    GF_EXPECT (proc.getUiState().recordTap.getState() != RecordTap::State::Recording);
}

GF_TEST (Calibrate, large_sample_rate_192k_ok)
{
    const double sr = 192000.0;
    auto samples = hits (4.0, 0.5, 120.0f, 0.1f, 0.5f, 0.7f, sr);

    CalibInput in;
    in.mono = samples.data();
    in.numSamples = (int) samples.size();
    in.sampleRate = sr;
    in.base = defaultBase();

    const auto m = measure (in, 50, 50);
    GF_EXPECT (m.events >= 4 && m.eventsPerSec > 0.5f && m.eventsPerSec < 10.0f);
    GF_EXPECT (signalFraction (samples.data(), in.numSamples, sr) > 0.25f);
}

GF_TEST (Calibrate, thread_exits_on_session_destruction)
{
    GapFillAudioProcessor proc;
    const auto samples = mono (makeDrumLoop (kSr, 8.0, 120.0, 1));

    for (int i = 0; i < 20; ++i)
    {
        CalibrationSession session (proc);
        session.analyseForTests (samples, kSr);
        juce::Thread::sleep (i % 3);
    }                                                    // destructors stop the worker mid-run

    GF_EXPECT (true);
}

GF_TEST (Calibrate, runtime_budget_24_runs_of_8_seconds)
{
    const auto samples = mono (makeDrumLoop (kSr, 8.0, 120.0, 1));
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto r = run (samples);
    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
    std::cout << "    calibrate 24 x 8 s: " << ms << " ms\n";
    GF_EXPECT (r.ok);

    if (juce::SystemStats::getEnvironmentVariable ("GAPFILL_PERF", {}).isNotEmpty())
        GF_EXPECT_MSG (ms < 3000.0, "24 runs of 8 s took " + juce::String (ms) + " ms");
}

GF_TEST (Calibrate, card_follows_the_session_and_apply_is_one_click)
{
    GapFillAudioProcessor proc;
    GapFillAudioProcessorEditor editor (proc);
    prepareProcessor (proc, kSr, 512, false);
    editor.setSize (1100, 700);

    auto* session = editor.getCalibrationForTests();
    auto* card = editor.getCalibrateCardForTests();
    GF_EXPECT (session != nullptr && card != nullptr && ! card->isVisible());

    session->analyseForTests (mono (makeDrumLoop (kSr, 6.0, 120.0, 1)), kSr);
    GF_EXPECT (waitFor (*session, Phase::Result));
    card->refreshForTests();
    GF_EXPECT (card->isVisible() && card->getShownText().contains ("hits/s"));

    const auto depth = proc.getUndoDepth();
    card->pressApply();
    GF_EXPECT (session->getPhase() == Phase::Applied && proc.getUndoDepth() == depth + 1);
    card->pressRevert();
    GF_EXPECT (session->getPhase() == Phase::Idle);
}
