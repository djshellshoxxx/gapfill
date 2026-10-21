// C-7 Musical Phrase Awareness (docs/dev/04-SPEC-CREATIVE.md C-7, doc 12 F-L08 offset).

#include "../support/TestRig.h"

#include "features/phrase/Phrase.h"

#include <cmath>
#include <cstring>

using namespace gapfill;
using namespace gftest;

namespace
{
    Effective::TransportInfo transport (int num = 4, int den = 4, bool valid = true)
    {
        return { valid, 0.0, num, den, 120.0 };
    }

    EngineSettings phraseSettings (int bars, float bias, int offset = 0)
    {
        EngineSettings s;
        s.perf.phrase.bars = bars;
        s.perf.phrase.bias01 = bias;
        s.perf.phrase.offsetBars = offset;
        return s;
    }

    float multiplierAt (int bars, float bias, int offset, double ppq, int num = 4, int den = 4)
    {
        auto s = phraseSettings (bars, bias, offset);
        phrase::apply (s, ppq, transport (num, den));
        return s.perf.probabilityMul;
    }

    std::vector<std::int64_t> startsFor (int block, std::initializer_list<std::pair<const char*, float>> params)
    {
        GapFillAudioProcessor proc;
        setParams (proc, params);
        TestPlayHead ph;
        ph.bpm = goldenBpm;
        ph.tsNum = 4; ph.tsDen = 4;
        RenderOptions o;
        o.playHead = &ph;
        renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, 8), block, goldenSampleRate, o);
        proc.drainFillEvents();

        std::vector<std::int64_t> starts;

        for (int i = proc.getGapOutcomeLog().size() - 1; i >= 0; --i)
            if (proc.getGapOutcomeLog().get (i).type == FillEventType::Started)
                starts.push_back (proc.getGapOutcomeLog().get (i).pos);

        return starts;
    }
}

GF_TEST (Phrase, off_or_bias0_is_identity)
{
    for (auto [bars, bias] : { std::pair { 0, 1.0f }, std::pair { 4, 0.0f } })
    {
        auto s = phraseSettings (bars, bias);
        EngineSettings before;
        std::memcpy (&before, &s, sizeof (s));
        phrase::apply (s, 15.9, transport());
        GF_EXPECT (std::memcmp (&before, &s, sizeof (s)) == 0);
    }

    GapFillAudioProcessor a, b;
    setParams (a, { { "fill", 80.0f } });
    setParams (b, { { "fill", 80.0f }, { "phraseBars", 2.0f }, { "phraseBias", 0.0f } });
    GF_EXPECT (compareBuffers (renderGoldenScenarioA (a).out, renderGoldenScenarioA (b).out, 0.0).maxAbs == 0.0);
}

GF_TEST (Phrase, multiplier_ramp_values)
{
    // 4-bar phrase in 4/4 = 16 beats; the last bar starts at beat 12.
    GF_EXPECT (multiplierAt (4, 1.0f, 0, 0.0) == 1.0f);
    GF_EXPECT (multiplierAt (4, 1.0f, 0, 6.0) == 1.0f);
    GF_EXPECT (multiplierAt (4, 1.0f, 0, 11.99) == 1.0f);          // outside the final bar: exactly 1
    GF_EXPECT_NEAR (multiplierAt (4, 1.0f, 0, 14.0), 1.0 + 1.5 * 0.5, 1.0e-5);   // middle of the last bar: r = 0.5
    GF_EXPECT_NEAR (multiplierAt (4, 1.0f, 0, 15.9999), 2.5, 1.0e-3);          // end of the phrase: x2.5
    GF_EXPECT_NEAR (multiplierAt (4, 0.5f, 0, 15.9999), 1.75, 1.0e-3);
    GF_EXPECT_NEAR (multiplierAt (4, 1.0f, 0, 16.0), 1.0, 1.0e-6);              // new phrase starts

    // Ramp helper.
    GF_EXPECT_NEAR (phrase::rampAt (0.75, 4), 0.0, 1.0e-9);
    GF_EXPECT_NEAR (phrase::rampAt (1.0, 4), 1.0, 1.0e-9);
    GF_EXPECT_NEAR (phrase::rampAt (0.5, 2), 0.0, 1.0e-9);        // 2-bar phrase: final bar starts at 0.5
    GF_EXPECT_NEAR (phrase::multiplierFor (2.0f, 1.0f), 2.5, 1.0e-6);   // bias clamps
}

GF_TEST (Phrase, invalid_ppq_or_time_signature_is_neutral)
{
    GF_EXPECT (multiplierAt (4, 1.0f, 0, std::nan ("")) == 1.0f);
    GF_EXPECT (multiplierAt (4, 1.0f, 0, 15.9, 0, 0) == 1.0f);
    GF_EXPECT (multiplierAt (4, 1.0f, 0, 15.9, 4, 3) == 1.0f);

    auto s = phraseSettings (4, 1.0f);
    phrase::apply (s, 15.9, transport (4, 4, false));
    GF_EXPECT (s.perf.probabilityMul == 1.0f);
}

GF_TEST (Phrase, loop_and_seek_are_reproducible)
{
    for (double ppq : { 12.5, 13.0, 15.0, 3.0 })
        for (int k = 1; k <= 5; ++k)
            GF_EXPECT_NEAR (multiplierAt (4, 0.8f, 0, ppq), multiplierAt (4, 0.8f, 0, ppq + 16.0 * k), 1.0e-5);

    // 7/4 phrase of 2 bars = 14 beats.
    GF_EXPECT_NEAR (multiplierAt (2, 1.0f, 0, 12.0, 7, 4), multiplierAt (2, 1.0f, 0, 12.0 + 28.0, 7, 4), 1.0e-5);
    GF_EXPECT (multiplierAt (2, 1.0f, 0, 12.0, 7, 4) > 1.0f);
}

GF_TEST (Phrase, offset_zero_identical_to_the_original_formula)
{
    for (double ppq = 0.0; ppq < 70.0; ppq += 0.9)
    {
        const double original = ppq / 16.0 - std::floor (ppq / 16.0);
        PhraseSettings p; p.bars = 4; p.bias01 = 1.0f; p.offsetBars = 0;
        GF_EXPECT (phrase::positionAt (p, ppq, transport()) == original);
    }
}

GF_TEST (Phrase, offset_shifts_the_ramp_by_bars)
{
    for (int offset = 1; offset <= 3; ++offset)
        for (double ppq : { 12.0, 14.5, 16.0, 20.0, 27.5 })
            GF_EXPECT_NEAR (multiplierAt (4, 1.0f, offset, ppq + 4.0 * offset), multiplierAt (4, 1.0f, 0, ppq), 1.0e-5);

    // Offset 1 moves the final bar from beats 12-16 to beats 16-20.
    GF_EXPECT (multiplierAt (4, 1.0f, 1, 14.0) == 1.0f);
    GF_EXPECT (multiplierAt (4, 1.0f, 1, 18.0) > 1.0f);
}

GF_TEST (Phrase, offset_is_taken_modulo_the_phrase_length)
{
    for (double ppq : { 3.0, 13.0, 15.5, 19.0 })
    {
        GF_EXPECT_NEAR (multiplierAt (4, 1.0f, 5, ppq), multiplierAt (4, 1.0f, 1, ppq), 1.0e-6);
        GF_EXPECT_NEAR (multiplierAt (2, 1.0f, 15, ppq), multiplierAt (2, 1.0f, 1, ppq), 1.0e-6);
        GF_EXPECT_NEAR (multiplierAt (8, 1.0f, 8, ppq), multiplierAt (8, 1.0f, 0, ppq), 1.0e-6);   // 8 mod 8 = 0
    }
}

GF_TEST (Phrase, never_fills_an_ineligible_gap)
{
    GapFillAudioProcessor off;
    setParams (off, { { "fill", 0.0f }, { "phraseBars", 1.0f }, { "phraseBias", 100.0f } });
    GF_EXPECT (renderGoldenScenarioA (off).fills == 0);

    // Every started fill still honours Minimum Gap.
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 50.0f }, { "minGap", 200.0f }, { "phraseBars", 1.0f }, { "phraseBias", 100.0f } });
    const auto r = renderGoldenScenarioA (proc);
    proc.drainFillEvents();

    for (int i = 0; i < proc.getGapOutcomeLog().size(); ++i)
    {
        const auto& e = proc.getGapOutcomeLog().get (i);

        if (e.type == FillEventType::Started)
            GF_EXPECT (e.recipe.lengthMs >= 199);
    }

    GF_EXPECT (r.fills >= 0);
}

GF_TEST (Phrase, block_size_invariant_decisions)
{
    std::vector<std::int64_t> reference;

    for (int block : { 32, 64, 512, 4096, 8192 })
    {
        const auto starts = startsFor (block, { { "fill", 60.0f }, { "phraseBars", 1.0f }, { "phraseBias", 100.0f } });
        GF_EXPECT_MSG (! starts.empty(), "no fills at block " + juce::String (block));

        if (reference.empty())
            reference = starts;
        else
            GF_EXPECT_MSG (starts == reference, "starts differ at block " + juce::String (block));
    }
}

GF_TEST (Phrase, raising_the_probability_adds_fills_in_the_last_bar)
{
    const auto plain = startsFor (512, { { "fill", 25.0f } });
    const auto biased = startsFor (512, { { "fill", 25.0f }, { "phraseBars", 1.0f }, { "phraseBias", 100.0f } });
    GF_EXPECT (biased.size() >= plain.size());
    GF_EXPECT (biased != plain);
}

GF_TEST (Phrase, state_round_trip_keeps_the_offset)
{
    GapFillAudioProcessor a;
    setParams (a, { { "phraseBars", 3.0f }, { "phraseBias", 40.0f }, { "phraseOffset", 7.0f } });
    juce::MemoryBlock block;
    a.getStateInformation (block);

    GapFillAudioProcessor b;
    b.setStateInformation (block.getData(), (int) block.getSize());
    GF_EXPECT (pi (b.getValueTreeState().getRawParameterValue ("phraseBars")) == 3);
    GF_EXPECT (pi (b.getValueTreeState().getRawParameterValue ("phraseOffset")) == 7);
    GF_EXPECT_NEAR (pv (b.getValueTreeState().getRawParameterValue ("phraseBias")), 40.0, 0.01);
}
