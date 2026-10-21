// F-L10 Call and response (docs/dev/features/F-L10).

#include "../support/TestRig.h"
#include "../support/VoiceRig.h"
#include "../support/AllocGuard.h"

#include "features/callresponse/Motif.h"
#include "features/callresponse/Responder.h"
#include "features/history/Describe.h"

#include <cmath>
#include <cstring>

using namespace gapfill;
using namespace gapfill::callresponse;
using namespace gftest;

namespace
{
    FillRecipe recipeOf (Algo a, int band = -1, int lengthClass = 1)
    {
        FillRecipe r;
        r.algo = (std::uint8_t) a;
        r.band = (std::int8_t) band;
        r.lengthClass = (std::uint8_t) lengthClass;
        r.lengthMs = 120;
        return r;
    }

    ResponderSettings settings (int mode, float strength = 0.6f, int calls = 3, int replies = 2)
    {
        ResponderSettings s;
        s.mode = mode; s.strength01 = strength; s.calls = calls; s.replies = replies;
        return s;
    }

    /** Starts a regular rhythmic fill every beat. */
    void startFill (Responder& r, const FillRecipe& rec, int index)
    {
        r.onNaturalFillStarted (rec, true, (double) index, (std::int64_t) index * 24000, 48000.0);
    }

    double shareOfFamilies (const Responder& r, std::initializer_list<AlgoFamily> families)
    {
        double total = 0.0, part = 0.0;

        for (int a = 0; a < numAlgos; ++a)
        {
            const double w = r.algoFactor (a);
            total += w;

            for (auto f : families)
                if (kAlgoFamilies[(size_t) a] == f)
                    part += w;
        }

        return part / total;
    }

    void respondTo (Responder& r, Algo a, int mode, float strength = 0.6f)
    {
        r.configure (settings (mode, strength), false);

        for (int i = 0; i < 3; ++i)
            startFill (r, recipeOf (a), i);
    }
}

//==============================================================================
GF_TEST (CallResponse, describe_family_mode_ties_pitch_band_and_regularity)
{
    CallEntry e[4];

    for (int i = 0; i < 4; ++i)
    {
        e[i].algo = (std::uint8_t) (i < 3 ? Algo::Stutter : Algo::Granular);
        e[i].band = 1;
        e[i].lengthClass = (std::uint8_t) (i % 2 == 0 ? 0 : 1);
        e[i].hasBeat = true;
        e[i].startBeat = i * 1.0;
        e[i].startSeconds = i * 0.5;
    }

    auto m = describe (e, 4);
    GF_EXPECT (m.family == AlgoFamily::Rhythmic && std::abs (m.familyShare - 0.75f) < 1.0e-6f);
    GF_EXPECT (m.band == 1 && m.pitchDir == 0);
    GF_EXPECT (m.regularity > 0.99f);                                  // perfectly regular
    GF_EXPECT (m.confidence > 0.85f);

    // Ties: two rhythmic, two textural -> lowest enum value (Rhythmic).
    e[2].algo = (std::uint8_t) Algo::Freeze;
    GF_EXPECT (describe (e, 4).family == AlgoFamily::Rhythmic);

    // Pitch direction: more Rise than Fall -> +1.
    CallEntry p[3];
    p[0].algo = p[1].algo = (std::uint8_t) Algo::PitchRise;
    p[2].algo = (std::uint8_t) Algo::PitchFall;
    GF_EXPECT (describe (p, 3).pitchDir == 1);

    // Regularity: irregular intervals lower it; beats and seconds both work, fewer than 3 entries -> 0.5.
    CallEntry q[3];
    double beats[3] = { 0.0, 0.2, 3.0 };

    for (int i = 0; i < 3; ++i)
    {
        q[i].algo = (std::uint8_t) Algo::Stutter;
        q[i].hasBeat = true;
        q[i].startBeat = beats[i];
        q[i].startSeconds = beats[i] * 0.5;
    }

    const auto irregular = describe (q, 3);
    GF_EXPECT (irregular.regularity < 0.2f);
    q[1].hasBeat = false;                                               // one entry without transport: seconds are used
    GF_EXPECT (std::abs (describe (q, 3).regularity - irregular.regularity) < 1.0e-4f);       // same shape in seconds
    GF_EXPECT (std::abs (describe (q, 2).regularity - 0.5f) < 1.0e-6f);
}

GF_TEST (CallResponse, off_is_neutral_and_inactive)
{
    Responder r;
    r.configure (settings (0), false);

    for (int i = 0; i < 6; ++i)
        startFill (r, recipeOf (Algo::Stutter), i);

    GF_EXPECT (! r.active());

    for (int a = 0; a < numAlgos; ++a)
        GF_EXPECT (r.algoFactor (a) == 1.0f);

    GF_EXPECT (r.lengthScale() == 1.0f && r.bandFactor (1) == 1.0f);
    GF_EXPECT (unpackStatus (r.statusWord()).off);
}

GF_TEST (CallResponse, state_machine_collect_respond_collect_and_responses_not_in_next_call)
{
    Responder r;
    r.configure (settings (1, 0.6f, 3, 2), false);

    for (int i = 0; i < 2; ++i)
        startFill (r, recipeOf (Algo::Stutter), i);

    GF_EXPECT (r.status().state == ResponderState::Collecting && r.status().k == 2 && r.status().n == 3);
    GF_EXPECT (! r.active());

    startFill (r, recipeOf (Algo::Scramble), 2);                        // third rhythmic fill: clear motif
    GF_EXPECT (r.status().state == ResponderState::Responding && r.active());
    GF_EXPECT (r.status().k == 1 && r.status().n == 2);

    startFill (r, recipeOf (Algo::Granular), 3);                        // first response
    GF_EXPECT (r.active() && r.status().k == 2);
    startFill (r, recipeOf (Algo::Filter), 4);                          // second response: cycle ends
    GF_EXPECT (r.status().state == ResponderState::Collecting && r.status().k == 0 && ! r.active());

    // The two response fills were not added to the next call: three fresh fills are needed.
    startFill (r, recipeOf (Algo::Delay), 5);
    startFill (r, recipeOf (Algo::Stutter), 6);
    GF_EXPECT (r.status().state == ResponderState::Collecting && r.status().k == 2);
}

GF_TEST (CallResponse, unclear_motif_gives_no_response_and_window_slides)
{
    int unclear = 0, cycles = 0;
    juce::Random rnd (3);

    for (int c = 0; c < 200; ++c)
    {
        Responder r;
        r.configure (settings (1), false);
        // Three different families at random times (irregular spacing).
        const Algo picks[3] = { Algo::Stutter, Algo::Granular, Algo::Crush };
        double t = 0.0;

        for (int i = 0; i < 3; ++i)
        {
            t += 0.2 + 2.5 * rnd.nextDouble();
            r.onNaturalFillStarted (recipeOf (picks[i]), true, t, (std::int64_t) (t * 24000.0), 48000.0);
        }

        ++cycles;
        unclear += r.status().state == ResponderState::Unclear && ! r.active();
    }

    GF_EXPECT_MSG (unclear * 100 >= cycles * 95, "unclear " + juce::String (unclear));

    // Unclear keeps collecting with a sliding window: after three scattered fills, two more rhythmic fills
    // at regular beats make the last three a clear motif.
    Responder r;
    r.configure (settings (1), false);
    r.onNaturalFillStarted (recipeOf (Algo::Granular), true, 0.0, 0, 48000.0);
    r.onNaturalFillStarted (recipeOf (Algo::Crush), true, 0.3, 7200, 48000.0);
    r.onNaturalFillStarted (recipeOf (Algo::Granular), true, 4.0, 96000, 48000.0);
    GF_EXPECT (r.status().state == ResponderState::Unclear);

    startFill (r, recipeOf (Algo::Stutter), 5);
    startFill (r, recipeOf (Algo::Stutter), 6);
    startFill (r, recipeOf (Algo::Stutter), 7);
    GF_EXPECT (r.status().state == ResponderState::Responding && r.motif().family == AlgoFamily::Rhythmic);
}

GF_TEST (CallResponse, complement_table_is_complete_for_all_family_pairs)
{
    for (int f = 0; f < 4; ++f)
    {
        int primary = 0, secondary = 0;

        for (int g = 0; g < 4; ++g)
        {
            const auto m = complementMultiplier ((AlgoFamily) f, (AlgoFamily) g);
            primary += m == 2.0f;
            secondary += m == 1.0f;
            GF_EXPECT (m >= 0.0f);

            if (f == g)
                GF_EXPECT (m == 0.0f);                                    // the motif family itself is reduced, not boosted
        }

        GF_EXPECT (primary == 1 && secondary == 1);
    }
}

GF_TEST (CallResponse, complement_shifts_family_shares_and_echo_boosts_the_motif)
{
    const auto baseRhythmic = 3.0 / 15.0;
    const auto baseOther = 9.0 / 15.0;

    Responder comp, echo;
    respondTo (comp, Algo::Stutter, 1, 0.6f);
    GF_EXPECT (comp.active());
    const auto rhythmic = shareOfFamilies (comp, { AlgoFamily::Rhythmic });
    GF_EXPECT_MSG (rhythmic < 0.6 * baseRhythmic, "rhythmic share " + juce::String (rhythmic));
    GF_EXPECT_MSG (shareOfFamilies (comp, { AlgoFamily::Textural, AlgoFamily::PitchTape }) > 1.2 * baseOther,
                   "other share " + juce::String (shareOfFamilies (comp, { AlgoFamily::Textural, AlgoFamily::PitchTape })));

    respondTo (echo, Algo::Stutter, 2, 0.6f);
    GF_EXPECT (shareOfFamilies (echo, { AlgoFamily::Rhythmic }) >= 1.5 * baseRhythmic);
}

GF_TEST (CallResponse, pitch_direction_contrast_band_and_length_bounds)
{
    Responder r;
    r.configure (settings (1, 1.0f), false);

    for (int i = 0; i < 3; ++i)
        startFill (r, recipeOf (Algo::PitchRise, 2, 0), i);

    GF_EXPECT (r.active() && r.motif().pitchDir == 1);
    GF_EXPECT (r.algoFactor ((int) Algo::PitchFall) > r.algoFactor ((int) Algo::TapeStop));        // contrast on top of the family multiplier
    GF_EXPECT (r.bandFactor (0) > 1.0f && r.bandFactor (2) == 1.0f);                                  // Complement: other bands favoured
    GF_EXPECT (r.lengthScale() > 1.0f && r.lengthScale() <= 1.3f + 1.0e-6f);                          // short call -> longer reply

    Responder e;
    e.configure (settings (2, 1.0f), false);

    for (int i = 0; i < 3; ++i)
        startFill (e, recipeOf (Algo::Stutter, 2, 3), i);

    GF_EXPECT (e.bandFactor (2) > 1.0f && e.bandFactor (0) == 1.0f);
    GF_EXPECT (e.lengthScale() > 1.0f && e.lengthScale() < 1.3f);

    for (int a = 0; a < numAlgos; ++a)
        GF_EXPECT (r.algoFactor (a) > 0.0f && std::isfinite (r.algoFactor (a)) && e.algoFactor (a) > 0.0f);          // zero weights stay zero
}

GF_TEST (CallResponse, higher_precedence_features_pause_and_resets_clear)
{
    Responder r;
    r.configure (settings (1), false);
    startFill (r, recipeOf (Algo::Stutter), 0);
    startFill (r, recipeOf (Algo::Stutter), 1);

    r.configure (settings (1), true);                                    // Fill Lock / Like / forced type
    startFill (r, recipeOf (Algo::Stutter), 2);                          // frozen: does not count
    GF_EXPECT (r.status().k == 2 && ! r.active());

    r.configure (settings (1), false);
    startFill (r, recipeOf (Algo::Stutter), 3);
    GF_EXPECT (r.active());

    r.configure (settings (1), true);
    GF_EXPECT (! r.active() && r.algoFactor ((int) Algo::Stutter) == 1.0f);

    r.configure (settings (1), false);
    r.reset();                                                           // transport jump / stop / bypass / sample-rate change
    GF_EXPECT (! r.active() && r.status().k == 0);

    // Changing the cycle lengths restarts it.
    startFill (r, recipeOf (Algo::Stutter), 0);
    r.configure (settings (1, 0.6f, 4, 2), false);
    GF_EXPECT (r.status().k == 0);
}

GF_TEST (CallResponse, status_packing_roundtrip)
{
    for (int state = 0; state < 3; ++state)
        for (int k = 0; k <= 4; ++k)
        {
            ResponderStatus s;
            s.state = (ResponderState) state; s.k = k; s.n = 4; s.confidencePercent = 17 * k; s.family = AlgoFamily::PitchTape; s.off = false;
            const auto back = unpackStatus (packStatus (s));
            GF_EXPECT (back.state == s.state && back.k == s.k && back.n == s.n);
            GF_EXPECT (back.confidencePercent == s.confidencePercent && back.family == s.family && ! back.off);
        }
}

GF_TEST (CallResponse, off_is_bit_identical_and_on_marks_response_fills)
{
    auto render = [] (std::initializer_list<std::pair<const char*, float>> p, std::vector<FillRecipe>* recipes = nullptr)
    {
        GapFillAudioProcessor proc;
        setParams (proc, p);
        TestPlayHead ph;
        ph.bpm = goldenBpm;
        RenderOptions o;
        o.playHead = &ph;
        auto out = renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, goldenBars), 512, goldenSampleRate, o).out;

        if (recipes != nullptr)
        {
            proc.drainFillEvents();

            for (int i = 0; i < proc.getRecipeHistory().size(); ++i)
                recipes->push_back (proc.getRecipeHistory().get (i));
        }

        return out;
    };

    const auto a = render ({ { "fill", 100.0f }, { "density", 100.0f } });
    const auto b = render ({ { "fill", 100.0f }, { "density", 100.0f }, { "fl10_mode", 0.0f }, { "fl10_strength", 100.0f },
                             { "fl10_calls", 4.0f }, { "fl10_replies", 4.0f } });
    GF_EXPECT (buffersIdentical (a, b));

    std::vector<FillRecipe> recipes;
    render ({ { "fill", 100.0f }, { "density", 100.0f }, { "fl10_mode", 1.0f }, { "fl10_calls", 2.0f }, { "fl10_replies", 1.0f },
              { "seed", 3.0f } }, &recipes);

    int responses = 0;

    for (const auto& r : recipes)
    {
        GF_EXPECT (isValid (r));
        responses += (r.flags & FillRecipe::flagResponse) != 0;

        if ((r.flags & FillRecipe::flagResponse) != 0)
            GF_EXPECT (history::describeRecipe (r, 120.0).contains ("Response"));
    }

    GF_EXPECT_MSG (responses >= 1, "no response fill was produced");
}

GF_TEST (CallResponse, audio_thread_does_not_allocate_with_call_and_response_on)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "fl10_mode", 2.0f } });
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
