// F-P02 Quantised Fill Now (docs/dev/12 F-P02).

#include "../support/LiveRig.h"

#include <cmath>

using namespace gapfill;
using namespace gapfill::live;
using namespace gftest;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kLatency = 1920;       // default Lookahead 40 ms

    juce::AudioBuffer<float> sine (double seconds, float amp = 0.5f)
    {
        juce::AudioBuffer<float> b (2, (int) (seconds * kSr));

        for (int i = 0; i < b.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                b.setSample (ch, i, amp * std::sin (6.2831853f * 220.0f * (float) i / (float) kSr));

        return b;
    }

    void click (juce::AudioBuffer<float>& b, int at)
    {
        for (int i = 0; i < 400 && at + i < b.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                b.setSample (ch, at + i, 0.95f * (i % 2 == 0 ? 1.0f : -1.0f) * std::exp (-(float) i / 60.0f));
    }

    struct Outcome { bool started = false; RejectReason reason = RejectReason::None; std::int64_t pos = -1; juce::uint8 flags = 0; };

    /** Runs the render, posts `presses` FillNow commands at the given block indices, returns forced outcomes in order. */
    std::vector<Outcome> run (const juce::AudioBuffer<float>& input, int block, std::initializer_list<int> pressBlocks,
                              std::initializer_list<std::pair<const char*, float>> params, bool transport = true,
                              FillNowQuant quant = FillNowQuant::NextBeat, bool overSustained = false, double startPpq = 0.0)
    {
        GapFillAudioProcessor proc;
        setParams (proc, params);
        PerfOptions o = proc.getLiveControl().getPerfOptions();
        o.fillNowQuant = quant;
        o.fillNowOverSustained = overSustained;
        proc.getLiveControl().setPerfOptions (o);

        TestPlayHead ph;
        ph.provideInfo = transport;
        ph.ppq = startPpq;
        std::vector<Outcome> out;

        renderLive (proc, input, block, kSr, ph, [&] (int b, int, juce::MidiBuffer&)
        {
            for (const auto p : pressBlocks)
                if (b == p)
                {
                    Command c;
                    c.type = CommandType::FillNow;
                    proc.postCommand (c);
                }

            proc.drainFillEvents();
        });

        proc.drainFillEvents();
        const auto& log = proc.getGapOutcomeLog();

        for (int i = log.size() - 1; i >= 0; --i)                   // oldest first
        {
            const auto& e = log.get (i);
            const bool forcedStart = e.type == FillEventType::Started && (e.recipe.flags & FillRecipe::flagForced) != 0;

            if (forcedStart || e.reason == RejectReason::ForcedUnsafe)
                out.push_back ({ forcedStart, e.reason, e.pos, e.recipe.flags });
        }

        return out;
    }

    const std::initializer_list<std::pair<const char*, float>> kBase { { "fill", 100.0f }, { "density", 0.0f }, { "dry", 0.0f } };
}

GF_TEST (FillNow, fires_on_the_next_beat_boundary_within_one_analysis_hop)
{
    // Pressed in block 100 of 512 samples = 1.067 s = ppq 2.13 at 120 bpm: the next beat is ppq 3 (1.5 s).
    const auto r = run (sine (4.0), 512, { 100 }, kBase);
    GF_EXPECT (r.size() == 1 && r[0].started);
    const auto boundary = (std::int64_t) (3.0 * 24000.0) - kLatency;
    GF_EXPECT_MSG (r[0].pos >= boundary && r[0].pos < boundary + 32, "pos " + juce::String ((juce::int64) r[0].pos));
    GF_EXPECT ((r[0].flags & FillRecipe::flagForced) != 0 && (r[0].flags & FillRecipe::flagPreview) == 0);
}

GF_TEST (FillNow, next_bar_and_immediate_modes)
{
    const auto bar = run (sine (6.0), 512, { 100 }, kBase, true, FillNowQuant::NextBar);
    GF_EXPECT (bar.size() == 1 && bar[0].started);
    const auto boundary = (std::int64_t) (4.0 * 24000.0) - kLatency;      // ppq 4 is the next bar line
    GF_EXPECT (bar[0].pos >= boundary && bar[0].pos < boundary + 32);

    const auto now = run (sine (4.0), 512, { 100 }, kBase, true, FillNowQuant::Immediate);
    GF_EXPECT (now.size() == 1 && now[0].started);
    GF_EXPECT (now[0].pos >= 100 * 512 - kLatency && now[0].pos < 100 * 512 - kLatency + 512 + 32);
}

GF_TEST (FillNow, no_transport_falls_back_to_immediate)
{
    const auto r = run (sine (4.0), 512, { 100 }, kBase, false);
    GF_EXPECT (r.size() == 1 && r[0].started);
    GF_EXPECT (r[0].pos < 100 * 512 - kLatency + 512 + 32);
}

GF_TEST (FillNow, ignores_the_density_roll_and_the_half_gap_guard)
{
    // Density 0 would reject most natural gaps; four presses on four consecutive beats all start.
    const auto r = run (sine (10.0), 512, { 20, 120, 220, 320 }, kBase);
    GF_EXPECT (r.size() == 4);

    for (const auto& o : r)
        GF_EXPECT (o.started);
}

GF_TEST (FillNow, is_unsafe_when_the_next_hit_is_too_close)
{
    auto input = sine (4.0, 0.2f);
    click (input, 72000 + 20 * 48 - kLatency);     // a hit 20 ms after the ppq-3 boundary (detections live on the output timeline)
    const auto r = run (input, 512, { 100 }, kBase);
    GF_EXPECT_MSG (r.size() == 1 && ! r[0].started && r[0].reason == RejectReason::ForcedUnsafe,
                   "n=" + juce::String ((int) r.size()) + (r.empty() ? juce::String() : " started=" + juce::String ((int) r[0].started) + " reason=" + juce::String ((int) r[0].reason)));
}

GF_TEST (FillNow, respects_protect_hold_unless_the_option_allows_it)
{
    auto input = sine (4.0, 0.2f);
    click (input, 72000 - 10 * 48 - kLatency);     // a hit 10 ms BEFORE the boundary
    const auto strict = run (input, 512, { 100 }, kBase);
    GF_EXPECT (strict.size() == 1 && ! strict[0].started && strict[0].reason == RejectReason::ForcedUnsafe);

    const auto allowed = run (input, 512, { 100 }, kBase, true, FillNowQuant::NextBeat, true);
    GF_EXPECT (allowed.size() == 1 && allowed[0].started);
}

GF_TEST (FillNow, the_start_position_is_independent_of_the_host_block_size)
{
    const auto input = sine (4.0);
    const auto reference = run (input, 512, { 100 }, kBase);
    GF_EXPECT (reference.size() == 1);

    // press at the same time (block index scaled to keep ~1.07 s)
    GF_EXPECT (run (input, 64, { 800 }, kBase)[0].pos == reference[0].pos);
    GF_EXPECT (run (input, 4096, { 12 }, kBase)[0].pos == reference[0].pos);
    GF_EXPECT (run (input, 32, { 1600 }, kBase)[0].pos == reference[0].pos);
}

GF_TEST (FillNow, appears_in_the_history_as_a_natural_fill_not_a_preview)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 100.0f }, { "density", 0.0f } });
    TestPlayHead ph;
    renderLive (proc, sine (4.0), 512, kSr, ph, [&] (int b, int, juce::MidiBuffer&)
    {
        if (b == 100)
        {
            Command c;
            c.type = CommandType::FillNow;
            proc.postCommand (c);
        }
    });
    proc.drainFillEvents();

    GF_EXPECT (proc.getRecipeHistory().size() == 1);
    const auto& r = proc.getRecipeHistory().get (0);
    GF_EXPECT ((r.flags & FillRecipe::flagForced) != 0 && (r.flags & FillRecipe::flagPreview) == 0);
    GF_EXPECT (isValid (r));
}

GF_TEST (FillNow, panic_cancels_a_pending_request_and_a_panicked_engine_refuses)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 100.0f }, { "density", 0.0f } });
    TestPlayHead ph;
    renderLive (proc, sine (4.0), 512, kSr, ph, [&] (int b, int, juce::MidiBuffer&)
    {
        if (b == 100)
        {
            Command c;
            c.type = CommandType::FillNow;
            proc.postCommand (c);
        }

        if (b == 101)
            proc.getLiveControl().panic().requestPanic();
    });
    proc.drainFillEvents();
    GF_EXPECT (proc.getFillEngine().getFillsStarted() == 0);
}

GF_TEST (FillNow, options_round_trip_through_the_perf_subtree)
{
    GapFillAudioProcessor a;
    PerfOptions o = a.getLiveControl().getPerfOptions();
    o.fillNowQuant = FillNowQuant::NextBar;
    o.fillNowOverSustained = true;
    a.getLiveControl().setPerfOptions (o);

    juce::MemoryBlock blob;
    a.getStateInformation (blob);
    GapFillAudioProcessor b;
    b.setStateInformation (blob.getData(), (int) blob.getSize());
    GF_EXPECT (b.getLiveControl().getPerfOptions().fillNowQuant == FillNowQuant::NextBar);
    GF_EXPECT (b.getLiveControl().getPerfOptions().fillNowOverSustained);
}

GF_TEST (FillNow, a_midi_note_bound_to_fill_now_triggers_it)
{
    GapFillAudioProcessor proc;
    setParams (proc, { { "fill", 100.0f }, { "density", 0.0f } });
    commands::Binding bnd;
    bnd.cmd = CommandType::FillNow;
    bnd.type = commands::MsgType::Note;
    bnd.number = 60;
    proc.getLiveControl().bindings().add (bnd);

    TestPlayHead ph;
    renderLive (proc, sine (4.0), 512, kSr, ph, [&] (int b, int, juce::MidiBuffer& m)
    {
        if (b == 100)
            m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    });
    proc.drainFillEvents();
    GF_EXPECT (proc.getFillEngine().getFillsStarted() == 1);
}
