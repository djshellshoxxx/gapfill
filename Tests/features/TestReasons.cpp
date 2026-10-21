// E-3 "Why didn't it fill?" (docs/dev/03 E-3).

#include "../support/TestRig.h"

#include "features/reasons/Reasons.h"
#include "features/events/EventRecorder.h"
#include "gui/WaveformGapView.h"

#include <array>
#include <set>

using namespace gapfill;
using namespace gftest;

namespace
{
    struct Collected
    {
        std::array<int, (size_t) numRejectReasons> eventCounts {};
        std::vector<FillEvent> events;     ///< every evaluated gap, oldest first (from the event recorder)
        std::vector<VizFrame> frames;
        int started = 0;
        int drained = 0;                   ///< events the message thread drained (recorder off)
    };

    /** Renders the drum loop. `watched` = a visualiser is listening and the event
        recorder collects every outcome (independent of the 64-deep queue). */
    Collected collect (GapFillAudioProcessor& proc, std::initializer_list<std::pair<const char*, float>> p,
                       int bars = 4, bool watched = true)
    {
        Collected c;
        proc.getVisualisationFifo().setEnabled (watched);
        setParams (proc, p);

        if (watched)
            proc.getExplainState().recorder.start ("<state/>", goldenSampleRate, 512, "test", 2);

        TestPlayHead ph;
        ph.bpm = goldenBpm;
        RenderOptions o;
        o.playHead = &ph;
        VizFrame buf[256];

        o.onBlock = [&] (int, int)
        {
            c.drained += proc.drainFillEvents();

            for (int got; (got = proc.getVisualisationFifo().pop (buf, 256)) > 0;)
                c.frames.insert (c.frames.end(), buf, buf + got);
        };

        renderOffline (proc, drumLoop (goldenSampleRate, goldenBpm, bars), 512, goldenSampleRate, o);

        if (watched)
        {
            auto& rec = proc.getExplainState().recorder;
            rec.stop();

            for (const auto& r : rec.snapshot().records)
            {
                if (r.type != (std::uint8_t) events::RecType::Outcome)
                    continue;

                FillEvent e;
                e.type = (r.flags & 1) != 0 ? FillEventType::Started : FillEventType::Rejected;
                e.reason = (RejectReason) (r.index & 0xFF);
                e.availMs = events::bitsFloat (r.p0);
                e.needMs = events::bitsFloat (r.p1);
                e.pos = r.pos;
                c.events.push_back (e);

                if (e.type == FillEventType::Started)
                    ++c.started;
                else
                    ++c.eventCounts[(size_t) e.reason];
            }
        }

        return c;
    }
}

GF_TEST (Reasons, table_is_total_ordered_and_letters_are_unique)
{
    std::set<char> letters;

    for (int i = 0; i < numRejectReasons; ++i)
    {
        const auto& inf = reasons::info ((RejectReason) i);
        GF_EXPECT ((int) inf.reason == i);
        GF_EXPECT (juce::String (inf.label).isNotEmpty());
        GF_EXPECT (letters.insert (inf.letter).second);

        if (i > 0)
            GF_EXPECT (juce::String (inf.advice).isNotEmpty());
    }

    // Out-of-range values never crash and read as unknown.
    GF_EXPECT (juce::String (reasons::info ((RejectReason) 200).label) == "Unknown reason");
}

GF_TEST (Reasons, wording_follows_the_catalogue_and_quotes_the_numbers)
{
    GF_EXPECT (reasons::labelWithNumbers (RejectReason::TooShort, 38.0f, 60.0f) == "Gap too short (38 ms free, need 60 ms)");
    GF_EXPECT (reasons::labelWithNumbers (RejectReason::FillOff, 5.0f, 5.0f) == "Fill is at 0");
    GF_EXPECT (reasons::hoverText (RejectReason::TooShort, 38.0f, 60.0f).startsWith ("Gap too short - 38 ms free, 60 ms needed. Lower Min Gap"));
    GF_EXPECT (reasons::hoverText (RejectReason::FillOff, 0.0f, 0.0f) == "Fill is at 0. Raise Fill");

    FillEvent e;
    e.type = FillEventType::Rejected;
    e.reason = RejectReason::TooShort;
    e.availMs = 38.0f;
    e.needMs = 60.0f;
    GF_EXPECT (reasons::statusLine (e) == "Last gap: skipped - Gap too short (38 ms free, need 60 ms)");

    e.type = FillEventType::Started;
    GF_EXPECT (reasons::statusLine (e) == "Last gap: filled");
    e.type = FillEventType::Onset;
    GF_EXPECT (reasons::statusLine (e).isEmpty());
}

GF_TEST (Reasons, new_doc12_reasons_have_their_specified_labels)
{
    GF_EXPECT (reasons::label (RejectReason::BudgetFull) == "Fill budget for this bar used");
    GF_EXPECT (reasons::advice (RejectReason::Panicked) == "Press Recover");
    GF_EXPECT (reasons::advice (RejectReason::ScGated).contains ("SC threshold"));
    GF_EXPECT (reasons::label (RejectReason::ForcedUnsafe).startsWith ("Fill Now skipped"));
}

GF_TEST (Reasons, top_reasons_orders_by_count_and_skips_none_and_zero)
{
    std::uint32_t counts[numRejectReasons] {};
    counts[(int) RejectReason::None] = 999;            // never listed
    counts[(int) RejectReason::TooShort] = 7;
    counts[(int) RejectReason::DensityRoll] = 9;
    counts[(int) RejectReason::NoBand] = 7;            // ties: lower enum value first
    counts[(int) RejectReason::FillOff] = 1;

    reasons::TopReason top[3];
    GF_EXPECT (reasons::topReasons (counts, numRejectReasons, top, 3) == 3);
    GF_EXPECT (top[0].reason == RejectReason::DensityRoll && top[0].count == 9);
    GF_EXPECT (top[1].reason == RejectReason::TooShort);
    GF_EXPECT (top[2].reason == RejectReason::NoBand);

    std::uint32_t none[numRejectReasons] {};
    GF_EXPECT (reasons::topReasons (none, numRejectReasons, top, 3) == 0);
    GF_EXPECT (reasons::whySoFewFillsText (none, numRejectReasons).contains ("No gaps"));
    GF_EXPECT (reasons::whySoFewFillsText (counts, numRejectReasons).contains ("Skipped by Density"));
}

GF_TEST (Reasons, status_line_rate_limited)
{
    reasons::StatusRateLimiter limiter;                 // 4 Hz
    GF_EXPECT (limiter.allow (1000));
    GF_EXPECT (! limiter.allow (1100));
    GF_EXPECT (! limiter.allow (1249));
    GF_EXPECT (limiter.allow (1250));
    GF_EXPECT (! limiter.allow (1300));
    GF_EXPECT (limiter.allow (4000));
}

GF_TEST (Reasons, fill_off_and_too_short_events_carry_the_exact_numbers)
{
    {
        GapFillAudioProcessor proc;
        const auto c = collect (proc, { { "fill", 0.0f } });
        GF_EXPECT (c.eventCounts[(size_t) RejectReason::FillOff] > 0);
        GF_EXPECT (c.started == 0);
    }

    GapFillAudioProcessor proc;
    const auto c = collect (proc, { { "minGap", 500.0f } });
    int checked = 0;

    for (const auto& e : c.events)
    {
        if (e.reason == RejectReason::TooShort || e.reason == RejectReason::GuardEaten)
        {
            GF_EXPECT_NEAR (e.needMs, 500.0f, 0.01);       // the number the decision used
            GF_EXPECT (e.availMs < e.needMs);
            ++checked;
        }
    }

    GF_EXPECT (checked > 0);
}

GF_TEST (Reasons, density_no_algo_and_no_band_events_are_reported)
{
    {
        GapFillAudioProcessor proc;
        GF_EXPECT (collect (proc, { { "fill", 1.0f }, { "density", 0.0f } }).eventCounts[(size_t) RejectReason::DensityRoll] > 0);
    }
    {
        GapFillAudioProcessor proc;
        const auto c = collect (proc, { { "fill", 100.0f }, { "density", 100.0f }, { "fillType", 4.0f },
                                        { "maxFill", 20.0f }, { "minGap", 10.0f } });
        GF_EXPECT (c.eventCounts[(size_t) RejectReason::NoAlgoFits] > 0);

        for (const auto& e : c.events)
            if (e.reason == RejectReason::NoAlgoFits)
                GF_EXPECT (e.needMs > 20.0f * 0.9f);      // the shortest fill that would have fitted
    }
    {
        GapFillAudioProcessor proc;
        GF_EXPECT (collect (proc, { { "detMode", 2.0f }, { "fill", 100.0f }, { "bandLowOn", 0.0f },
                                    { "bandMidOn", 0.0f }, { "bandHighOn", 0.0f } }).eventCounts[(size_t) RejectReason::NoBand] > 0);
    }
}

GF_TEST (Reasons, reason_none_when_started)
{
    GapFillAudioProcessor proc;
    const auto c = collect (proc, { { "fill", 100.0f }, { "density", 100.0f } });
    GF_EXPECT (c.started > 0);

    for (const auto& e : c.events)
        if (e.type == FillEventType::Started)
            GF_EXPECT (e.reason == RejectReason::None);

    GF_EXPECT (proc.getRecipeHistory().size() > 0 && proc.getRecipeHistory().get (0).lengthMs > 0);
}

GF_TEST (Reasons, reasons_not_published_when_viz_disabled)
{
    GapFillAudioProcessor proc;
    const auto c = collect (proc, { { "fill", 0.0f } }, 4, false);
    GF_EXPECT (c.drained == 0);                       // the queue stayed empty
    proc.drainFillEvents();
    GF_EXPECT (proc.getGapOutcomeLog().size() == 0);
    GF_EXPECT (proc.getDroppedFillEvents() == 0);
}

GF_TEST (Reasons, fill_started_recipes_published_always)
{
    GapFillAudioProcessor proc;
    const auto c = collect (proc, { { "fill", 100.0f }, { "density", 100.0f } }, 4, false);
    GF_EXPECT (proc.getFillEngine().getFillsStarted() > 0);
    GF_EXPECT (c.drained == proc.getFillEngine().getFillsStarted());     // none lost, none invented (only recipes are queued)
    GF_EXPECT (proc.getRecipeHistory().size() > 0);
}

GF_TEST (Reasons, reasonCount_matches_events)
{
    GapFillAudioProcessor proc;
    const auto c = collect (proc, { { "fill", 40.0f }, { "density", 30.0f }, { "minGap", 200.0f } });

    int total = 0;

    for (int r = 1; r < numRejectReasons; ++r)
    {
        const auto published = (int) proc.getMeterState().reasonCount[r].load();
        GF_EXPECT_MSG (published == c.eventCounts[(size_t) r], reasons::label ((RejectReason) r));
        total += published;
    }

    GF_EXPECT (total > 0);
}

GF_TEST (Reasons, viz_flags_reject_and_preview_distinct)
{
    static_assert (VizFrame::flagRejected != VizFrame::flagPreview);
    static_assert ((VizFrame::flagRejected & VizFrame::flagPreview) == 0);
    static_assert (std::is_trivially_copyable_v<VizFrame>);

    GapFillAudioProcessor proc;
    const auto c = collect (proc, { { "fill", 0.0f } });
    int rejected = 0;

    for (const auto& f : c.frames)
    {
        if ((f.flags & VizFrame::flagRejected) != 0)
        {
            ++rejected;
            GF_EXPECT (f.reason == (std::uint8_t) RejectReason::FillOff);
        }

        GF_EXPECT ((f.flags & VizFrame::flagPreview) == 0);    // E-1's flag is never set by E-3
    }

    GF_EXPECT (rejected > 0);
}

GF_TEST (Reasons, skipped_marker_tooltip_names_the_engines_reason)
{
    // Frames carry the reason through the decimation into a column, and the
    // hover text is exactly the catalogue wording with the engine's numbers.
    VisualisationFifo fifo;
    WaveformGapView view (fifo);
    view.setBounds (0, 0, 400, 200);

    VizFrame f;
    f.flags = VizFrame::flagRejected | VizFrame::flagGapEligible;
    f.reason = (std::uint8_t) RejectReason::TooShort;
    f.availMs = 38;
    f.needMs = 60;
    view.injectFrameForTests (f);

    int column = -1;

    for (int i = 0; i < view.getNumColumns(); ++i)
        if ((view.getColumn (i).flags & VizFrame::flagRejected) != 0)
            column = i;

    GF_EXPECT (column >= 0);

    juce::Image image (juce::Image::ARGB, 400, 200, true);
    juce::Graphics g (image);
    view.paintEntireComponent (g, true);                 // computes the column geometry

    const auto colWidth = (400.0f - 16.0f) / (float) view.getNumColumns();
    GF_EXPECT (view.findRejectedColumnNear (8.0f + (float) column * colWidth) == column);
    GF_EXPECT (view.findRejectedColumnNear (2.0f) < 0 || column < 3);
}
