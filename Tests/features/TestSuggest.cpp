// F-M22 smart preset suggestions.

#include "../support/AllocGuard.h"
#include "../support/TestRig.h"

#include "PluginEditor.h"
#include "features/presetbrowser/PresetBrowser.h"
#include "features/suggest/Gui.h"
#include "features/suggest/Profile.h"
#include "features/suggest/Suggest.h"

#include <iostream>

using namespace gapfill;
using namespace gapfill::suggest;
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

    std::vector<float> hits (double seconds, double interval, float frequency, float decay, float noise, float amp, double sr = kSr)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * sr));
        b.clear();
        for (double t = 0.1; t < seconds - 0.2; t += interval)
            addDrumHit (b, (int) (t * sr), sr, frequency, decay, amp, noise);
        return mono (b);
    }

    /** Runs the real AnalysisEngine + accumulator over `samples` in blocks of `block`. */
    InputProfile profileOf (const std::vector<float>& samples, double sr, int block, float seconds, bool* gotSignal = nullptr)
    {
        AnalysisEngine engine;
        engine.prepare (sr, 8192);
        AnalysisSettings settings;
        settings.protectHoldMs = 40.0f; settings.retrigGuardMs = 30.0f; settings.gapReleaseMs = 80.0f;

        ProfileAccumulator acc;
        acc.prepare (sr);
        acc.startListening (seconds);

        for (int pos = 0; pos < (int) samples.size(); pos += block)
        {
            const auto n = std::min (block, (int) samples.size() - pos);
            const auto before = engine.getAnalysisPosition();
            engine.processBlock (samples.data() + pos, nullptr, n, settings);
            acc.accumulate (engine, before, engine.getAnalysisPosition());
            engine.discardEventsBefore (engine.getAnalysisPosition());
        }

        InputProfile p;
        const bool ok = acc.takeProfile (p);
        if (gotSignal != nullptr) *gotSignal = ok;
        return p;
    }

    juce::String topFamily (const InputProfile& p)
    {
        Suggestion s[3];
        if (rankPresets (p, s) == 0)
            return {};
        const auto name = juce::String (getFactoryPresets()[(size_t) s[0].presetIndex].name);
        for (const auto& row : familyTable())
            if (name == row.preset)
                return row.family;
        return {};
    }
}

GF_TEST (Suggest, table_resolves_all_names_and_every_factory_preset_has_a_row)
{
    const auto& all = getFactoryPresets();

    for (const auto& row : familyTable())
    {
        bool found = false;
        for (const auto& p : all)
            found = found || juce::String (p.name) == row.preset;
        GF_EXPECT_MSG (found, row.preset);
    }

    const auto missing = presetsWithoutRow();
    for (const auto i : missing)
        GF_EXPECT_MSG (false, juce::String ("factory preset without a family row: ") + all[(size_t) i].name);
}

GF_TEST (Suggest, rank_deterministic_skips_init_and_ties_break_by_index)
{
    InputProfile p;
    p.eventsPerSec = 5.0f; p.gapFraction = 0.3f; p.levelDb = -14.0f; p.lowShare = 0.4f; p.highShare = 0.25f; p.valid = true;

    Suggestion a[3], b[3];
    GF_EXPECT (rankPresets (p, a) == 3 && rankPresets (p, b) == 3);

    for (int i = 0; i < 3; ++i)
    {
        GF_EXPECT (a[i].presetIndex == b[i].presetIndex && a[i].distance == b[i].distance);
        GF_EXPECT (a[i].presetIndex > 0);                              // never Init
    }

    GF_EXPECT (juce::String (getFactoryPresets()[(size_t) a[0].presetIndex].name) == "Drum Gap Glitch");   // the exact row
    GF_EXPECT (a[0].distance < 0.01f && a[0].confidence == Confidence::Good);
    GF_EXPECT (a[0].distance <= a[1].distance && a[1].distance <= a[2].distance);

    p.valid = false;
    GF_EXPECT (rankPresets (p, a) == 0);
}

GF_TEST (Suggest, confidence_thresholds_and_reason_strings)
{
    InputProfile far;
    far.eventsPerSec = 40.0f; far.gapFraction = 1.0f; far.levelDb = 0.0f; far.lowShare = 1.0f; far.highShare = 0.0f; far.valid = true;
    Suggestion s[3];
    GF_EXPECT (rankPresets (far, s) == 3 && s[0].confidence == Confidence::Weak);

    for (const auto r : { Reason::DenseHits, Reason::SparseHits, Reason::ShortGaps, Reason::LongGaps, Reason::Loud,
                          Reason::Quiet, Reason::BassHeavy, Reason::TrebleHeavy })
        GF_EXPECT (describeReason (far, r).isNotEmpty());

    GF_EXPECT (confidenceWord (Confidence::Good) == "Good match" && confidenceWord (Confidence::Weak) == "Weak match");
    GF_EXPECT (measuredSummary (far).contains ("hits/s"));
}

GF_TEST (Suggest, profile_deterministic_and_block_size_invariant)
{
    const auto samples = mono (makeDrumLoop (kSr, 8.0, 120.0, 1));
    const auto reference = profileOf (samples, kSr, 512, 6.0f);
    GF_EXPECT (reference.valid && reference.eventsPerSec > 0.5f);

    const auto again = profileOf (samples, kSr, 512, 6.0f);
    GF_EXPECT (again.eventsPerSec == reference.eventsPerSec && again.gapFraction == reference.gapFraction && again.levelDb == reference.levelDb);

    for (const int block : { 32, 64, 4096, 8192 })
    {
        const auto p = profileOf (samples, kSr, block, 6.0f);
        GF_EXPECT_MSG (p.valid && p.eventsPerSec == reference.eventsPerSec && p.gapFraction == reference.gapFraction
                         && p.levelDb == reference.levelDb && p.lowShare == reference.lowShare && p.highShare == reference.highShare,
                       "block " + juce::String (block));
    }
}

GF_TEST (Suggest, profile_rate_invariant_within_tolerance)
{
    float reference = 0.0f;

    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const auto samples = mono (makeDrumLoop (sr, 8.0, 120.0, 1));
        const auto p = profileOf (samples, sr, 512, 6.0f);
        GF_EXPECT (p.valid);

        if (reference == 0.0f)
            reference = p.eventsPerSec;

        GF_EXPECT_MSG (std::abs (p.eventsPerSec - reference) <= 0.1f * reference + 0.2f,
                       "rate " + juce::String (sr) + ": " + juce::String (p.eventsPerSec) + " vs " + juce::String (reference));
    }
}

GF_TEST (Suggest, signal_gate_and_cancel)
{
    bool ok = true;
    const auto silent = profileOf (std::vector<float> ((size_t) (kSr * 8), 0.0f), kSr, 512, 5.0f, &ok);
    GF_EXPECT (! ok && ! silent.valid);

    ProfileAccumulator acc;
    acc.prepare (kSr);
    acc.startListening (10.0f);
    GF_EXPECT (acc.getState() == ProfileAccumulator::State::Requested);
    acc.cancel();
    GF_EXPECT (acc.getState() == ProfileAccumulator::State::Idle && acc.getProgress() == 0.0f);

    AnalysisEngine engine;
    engine.prepare (kSr, 512);
    acc.accumulate (engine, 0, 512);                     // idle: nothing happens
    InputProfile p;
    GF_EXPECT (! acc.takeProfile (p));

    // A sample-rate change abandons a run.
    acc.startListening (10.0f);
    acc.prepare (44100.0);
    GF_EXPECT (acc.getState() == ProfileAccumulator::State::Idle);
}

GF_TEST (Suggest, no_audio_thread_allocation_and_fixed_size_accumulator)
{
    static_assert (sizeof (ProfileAccumulator) <= 256, "no audio is retained: only a small fixed accumulator");

    AnalysisEngine engine;
    engine.prepare (kSr, 512);
    ProfileAccumulator acc;
    acc.prepare (kSr);
    acc.startListening (4.0f);

    const auto samples = mono (makeDrumLoop (kSr, 5.0, 120.0, 1));
    AnalysisSettings settings;
    long allocations = 0;

    {
        AllocGuard guard;
        for (int pos = 0; pos + 512 <= (int) samples.size(); pos += 512)
        {
            const auto before = engine.getAnalysisPosition();
            engine.processBlock (samples.data() + pos, nullptr, 512, settings);
            acc.accumulate (engine, before, engine.getAnalysisPosition());
            engine.discardEventsBefore (engine.getAnalysisPosition());
        }
        allocations = guard.count();
    }

    GF_EXPECT (allocations == 0);
    GF_EXPECT (acc.getState() == ProfileAccumulator::State::Done);
}

GF_TEST (Suggest, archetype_ranking)
{
    struct Archetype { juce::String name; std::vector<float> samples; juce::StringArray families; };
    std::vector<Archetype> corpus;

    corpus.push_back ({ "four on the floor 120", mono (makeDrumLoop (kSr, 10.0, 120.0, 1)), { "Glitch", "Subtle", "Rhythmic", "Utility", "Destructive" } });
    corpus.push_back ({ "sparse snare", hits (10.0, 1.0, 190.0f, 0.12f, 0.8f, 0.5f), { "Ambient", "Subtle", "Dub", "Utility" } });
    corpus.push_back ({ "dense hats", hits (10.0, 60.0 / 160.0 / 2.0, 7000.0f, 0.03f, 0.9f, 0.5f), { "Rhythmic", "Destructive", "Glitch" } });
    corpus.push_back ({ "bass and kick", hits (10.0, 0.5, 55.0f, 0.2f, 0.05f, 0.9f), { "Utility", "Subtle", "Glitch", "Dub" } });

    int top1 = 0;

    for (const auto& a : corpus)
    {
        const auto p = profileOf (a.samples, kSr, 512, 8.0f);
        Suggestion s[3];
        const auto n = rankPresets (p, s);
        std::cout << "    [" << a.name << "] " << measuredSummary (p) << " low " << p.lowShare << " high " << p.highShare << " -> ";

        for (int i = 0; i < n; ++i)
            std::cout << getFactoryPresets()[(size_t) s[i].presetIndex].name << " (" << s[i].distance << ") ";
        std::cout << "\n";

        GF_EXPECT_MSG (p.valid && n == 3, a.name);
        top1 += a.families.contains (topFamily (p)) ? 1 : 0;
    }

    GF_EXPECT_MSG (top1 >= 3, "archetypes whose top-1 family is an expected one: " + juce::String (top1) + " of 4");
}

GF_TEST (Suggest, consent_roundtrip_default_off_and_load_goes_through_select_preset)
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("gapfill-suggest-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    dir.createDirectory();

    {
        auto settingsFile = std::make_shared<settings::GlobalSettings> (dir.getChildFile ("s.xml"));
        GapFillAudioProcessor proc;
        prepareProcessor (proc, kSr, 512, false);
        SuggestSession session (proc, settingsFile);

        GF_EXPECT (! session.hasConsent());
        GF_EXPECT_MSG (! session.listen() && session.getMessage().containsIgnoreCase ("allow"), "no measurement without consent");
        GF_EXPECT (proc.getUiState().profile.getState() == ProfileAccumulator::State::Idle);

        session.setConsent (true);
        settingsFile->flush();
        GF_EXPECT (settings::GlobalSettings (dir.getChildFile ("s.xml")).getBool (kConsentKey, false));
        GF_EXPECT (session.listen (4.0f) && session.getPhase() == SuggestSession::Phase::Listening);

        // Feed audio through the real processor.
        const auto audio = makeDrumLoop (kSr, 6.0, 120.0, 2);
        juce::AudioBuffer<float> work (2, 512);
        juce::MidiBuffer midi;

        for (int pos = 0; pos + 512 <= audio.getNumSamples(); pos += 512)
        {
            for (int ch = 0; ch < 2; ++ch)
                work.copyFrom (ch, 0, audio, ch, pos, 512);
            proc.processBlock (work, midi);
        }

        GF_EXPECT (session.tick() && session.getPhase() == SuggestSession::Phase::Result);
        GF_EXPECT (session.getNumSuggestions() == 3 && session.getMessage().contains ("hits/s"));

        const auto depth = proc.getUndoDepth();
        GF_EXPECT (session.load (0));
        GF_EXPECT (proc.getUndoDepth() == depth + 1);
        GF_EXPECT (proc.getCurrentProgram() == session.getSuggestion (0).presetIndex);
        GF_EXPECT (! session.load (7));

        // Bypass abandons a run.
        setParams (proc, { { "bypass", 1.0f } });
        GF_EXPECT (! session.listen());
        setParams (proc, { { "bypass", 0.0f } });
    }

    dir.deleteRecursively();
}

GF_TEST (Suggest, browser_view_shows_consent_listen_and_results)
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("gapfill-suggest-view-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    dir.createDirectory();

    {
        auto settingsFile = std::make_shared<settings::GlobalSettings> (dir.getChildFile ("s.xml"));
        GapFillAudioProcessor proc;
        prepareProcessor (proc, kSr, 512, false);
        presets::PresetBrowser browser (proc, settingsFile, { { "t", dir.getChildFile ("none") } });
        browser.setBounds (0, 0, 540, 460);

        GF_EXPECT (browser.getSuggestView() == nullptr);
        browser.showSuggestions (true);
        auto* view = browser.getSuggestView();
        GF_EXPECT (view != nullptr);
        view->refreshForTests();
        GF_EXPECT (view->getSummary().contains ("Nothing is recorded or sent"));
        GF_EXPECT (view->getPreferredHeight() >= 40);

        browser.showSuggestions (false);
        GF_EXPECT (browser.getSuggestView() == nullptr);
    }

    dir.deleteRecursively();
}
