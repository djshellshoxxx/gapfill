// F-M03 Gap timeline - data model and engine event feed (docs/dev/12 F-M03).

#include "../support/TestRig.h"

#include "features/reasons/Reasons.h"
#include "features/timeline/Model.h"

#include <set>
#include <tuple>

using namespace gapfill;
using namespace gapfill::timeline;
using namespace gftest;

namespace
{
    using Key = std::tuple<int, std::int64_t, int>;      // type, pos, reason

    /** Timeline events the processor's model collected while rendering. */
    std::vector<Key> renderTimeline (int blockSize, bool playing = true,
                                     std::initializer_list<std::pair<const char*, float>> p = { { "fill", 100.0f }, { "density", 100.0f } },
                                     TimelineModel* modelOut = nullptr)
    {
        GapFillAudioProcessor proc;
        proc.getVisualisationFifo().setEnabled (true);
        setParams (proc, p);
        TestPlayHead ph;
        ph.bpm = goldenBpm;
        ph.playing = playing;
        RenderOptions o;
        o.playHead = &ph;
        o.onBlock = [&] (int, int) { proc.drainFillEvents(); };
        renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, 4), blockSize, goldenSampleRate, o);
        proc.drainFillEvents();

        const auto& m = proc.getExplainState().timelineModel;
        std::vector<Key> keys;

        for (int i = 0; i < m.size(); ++i)
            keys.emplace_back ((int) m.at (i).type, m.at (i).pos, (int) m.at (i).reason);

        if (modelOut != nullptr)
            *modelOut = m;

        return keys;
    }
}

GF_TEST (Timeline, record_is_32_bytes_and_every_type_has_text)
{
    static_assert (sizeof (TimelineEvent) == 32);

    for (int t = 0; t <= (int) EventType::Recover; ++t)
        GF_EXPECT (juce::String (typeName ((EventType) t)).isNotEmpty());
}

GF_TEST (Timeline, reason_enum_to_text_total)
{
    // Every reason the engine can return has text on the timeline, and that text is the E-3 wording.
    for (int r = 0; r < numRejectReasons; ++r)
    {
        TimelineEvent e;
        e.type = EventType::GapEval;
        e.reason = (std::uint8_t) r;
        e.availMs = 30;
        e.needMs = 60;
        const auto text = describe (e);
        GF_EXPECT (text.isNotEmpty());

        if (r > 0)
            GF_EXPECT_MSG (text.contains (reasons::label ((RejectReason) r)), text);
    }

    TimelineEvent beyond;
    beyond.type = EventType::GapEval;
    beyond.reason = 250;
    GF_EXPECT (describe (beyond).contains ("Unknown"));       // out-of-range stays total
}

GF_TEST (Timeline, record_roundtrip_from_engine_events)
{
    TimelineEvent out[2];

    FillEvent started;
    started.type = FillEventType::Started;
    started.pos = 123456;
    started.conf8 = 200;
    started.generation = 9;
    started.availMs = 180.0f;
    started.recipe.algo = 2;
    started.recipe.lengthMs = 120;
    GF_EXPECT (fromEngineEvent (started, out) == 2);
    GF_EXPECT (out[0].type == EventType::GapEval && out[0].reason == 0 && out[0].availMs == 180 && out[0].pos == 123456);
    GF_EXPECT (out[1].type == EventType::FillStart && out[1].algo == 2 && out[1].lenMs == 120 && out[1].generation == 9);

    FillEvent rejected;
    rejected.type = FillEventType::Rejected;
    rejected.reason = RejectReason::TooShort;
    rejected.availMs = 38.4f;
    rejected.needMs = 60.0f;
    rejected.pos = 5;
    GF_EXPECT (fromEngineEvent (rejected, out) == 1);
    GF_EXPECT (out[0].type == EventType::GapEval && out[0].reason == (std::uint8_t) RejectReason::TooShort);
    GF_EXPECT (out[0].availMs == 38 && out[0].needMs == 60);

    FillEvent onset;
    onset.type = FillEventType::Onset;
    onset.protHoldMs = 90;
    onset.lookaheadMs = 40;
    GF_EXPECT (fromEngineEvent (onset, out) == 1 && out[0].type == EventType::Onset && out[0].protHoldMs == 90 && out[0].lookaheadMs == 40);

    FillEvent none;
    GF_EXPECT (fromEngineEvent (none, out) == 0);

    // Saturating conversion: absurd millisecond values never wrap.
    rejected.availMs = 1.0e9f;
    rejected.needMs = -5.0f;
    fromEngineEvent (rejected, out);
    GF_EXPECT (out[0].availMs == 65535 && out[0].needMs == 0);
}

GF_TEST (Timeline, engine_feeds_onsets_gaps_evaluations_and_fills_in_order)
{
    TimelineModel m;
    const auto keys = renderTimeline (512, true, { { "fill", 100.0f }, { "density", 100.0f } }, &m);

    int onsets = 0, opens = 0, evals = 0, starts = 0, ends = 0;
    std::int64_t last = std::numeric_limits<std::int64_t>::min() / 2;

    for (int i = 0; i < m.size(); ++i)
    {
        const auto& e = m.at (i);
        GF_EXPECT_MSG (e.pos >= last - 1, "events are in decision order: i=" + juce::String (i) + " type=" + juce::String ((int) e.type)
                       + " pos=" + juce::String (e.pos) + " last=" + juce::String (last));
        last = e.pos;
        onsets += e.type == EventType::Onset;
        opens += e.type == EventType::GapOpen;
        evals += e.type == EventType::GapEval;
        starts += e.type == EventType::FillStart;
        ends += e.type == EventType::FillEnd;
    }

    GF_EXPECT (onsets > 4 && opens > 2 && evals > 2 && starts > 0);
    GF_EXPECT (evals == opens);                        // every opened gap is evaluated exactly once
    GF_EXPECT (ends >= starts - 1 && ends <= starts);  // each fill ends (the last one may still be playing)
    GF_EXPECT (! keys.empty());
}

GF_TEST (Timeline, ring_wrap_512)
{
    TimelineModel m;

    for (int i = 0; i < 700; ++i)
    {
        TimelineEvent e;
        e.type = EventType::Onset;
        e.pos = i;
        m.push (e);
    }

    GF_EXPECT (m.size() == TimelineModel::capacity);
    GF_EXPECT (m.at (0).pos == 700 - 512 && m.at (511).pos == 699);

    TimelineEvent newest[20];
    GF_EXPECT (m.latest (newest, 20) == 20);
    GF_EXPECT (newest[0].pos == 699 && newest[19].pos == 680);

    std::vector<TimelineEvent> w;
    m.window (699, 100, w);
    GF_EXPECT (w.size() == 100 && w.front().pos == 600 && w.back().pos == 699);
}

GF_TEST (Timeline, overflow_sets_incomplete)
{
    TimelineModel m;
    GF_EXPECT (! m.isIncomplete (1000, 500));

    m.noteDropped (900);
    GF_EXPECT (m.isIncomplete (1000, 500));            // loss inside the window
    GF_EXPECT (! m.isIncomplete (2000, 500));          // it has scrolled out of view

    // The engine side: a full queue skips optional events and counts them.
    FillEventQueue q;
    FillEvent onset;
    onset.type = FillEventType::Onset;

    for (int i = 0; i < 200; ++i)
        q.pushOptional (onset);

    GF_EXPECT (q.getNumReady() == FillEventQueue::capacity - FillEventQueue::reservedForOutcomes);
    GF_EXPECT (q.getDroppedOptionalCount() == (std::uint32_t) (200 - q.getNumReady()));
    GF_EXPECT (q.getDroppedCount() == 0);              // outcomes were not lost

    FillEvent started;
    started.type = FillEventType::Started;

    for (int i = 0; i < FillEventQueue::reservedForOutcomes; ++i)
        q.push (started);

    GF_EXPECT (q.getDroppedCount() == 0);              // the reserved slots were still there for outcomes
}

GF_TEST (Timeline, pause_does_not_drop_events)
{
    TimelineModel m;
    m.setPaused (true, 1000);

    for (int i = 0; i < 50; ++i)
    {
        TimelineEvent e;
        e.type = EventType::Onset;
        e.pos = 1000 + i;
        m.push (e);
    }

    GF_EXPECT (m.size() == 50);                        // recording continued
    GF_EXPECT (m.viewEnd (5000) == 1000);              // the display is frozen
    m.setPaused (false, 0);
    GF_EXPECT (m.viewEnd (5000) == 5000);
}

GF_TEST (Timeline, stopped_transport_ok)
{
    // A host that is not playing still gets evaluated gaps and a coherent timeline; nothing throws.
    TimelineModel m;
    renderTimeline (512, false, { { "fill", 100.0f }, { "density", 100.0f } }, &m);

    for (int i = 0; i < m.size(); ++i)
        GF_EXPECT (m.at (i).type != EventType::None);
}

GF_TEST (Timeline, variable_block_sizes)
{
    const auto reference = renderTimeline (512);
    GF_EXPECT (reference.size() > 20);

    for (const int block : { 32, 64, 4096 })
    {
        const auto keys = renderTimeline (block);
        GF_EXPECT_MSG (keys == reference, "timeline differs at block size " + juce::String (block));
    }
}
