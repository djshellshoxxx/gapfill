#include "Gui.h"

#include "../../Parameters.h"
#include "../../PluginProcessor.h"
#include "../reasons/Gui.h"
#include "../reasons/Reasons.h"

namespace gapfill::timeline
{

namespace
{
    constexpr int kControlsHeight = 28;
    constexpr int kListWidth = 250;
    constexpr int kAxisHeight = 14;

    juce::Colour familyColour (int algo)
    {
        static const juce::Colour palette[] = { colours::primary, colours::secondary, colours::warning, colours::success };
        return palette[(size_t) (juce::jmax (0, algo) % 4)];
    }
}

TimelineView::TimelineView (GapFillAudioProcessor& p) : proc (p)
{
    setTitle ("Gap timeline");
    setDescription ("Events, gaps, fills and skipped gaps on a common time axis. The list on the right repeats "
                    "the last 20 events as text.");
    setOpaque (false);

    auto& explain = proc.getExplainState();

    pauseButton.setClickingTogglesState (true);
    pauseButton.setTooltip ("Freeze the display. Events keep being recorded.");
    pauseButton.onClick = [this]
    {
        proc.getExplainState().timelineModel.setPaused (pauseButton.getToggleState(), liveEndPos());
        pauseButton.setButtonText (pauseButton.getToggleState() ? "RESUME" : "PAUSE");
        refresh();
    };

    for (auto* b : { &zoom4, &zoom8, &zoom16 })
    {
        b->setRadioGroupId (771);
        b->setClickingTogglesState (true);
        b->setTooltip ("Time shown across the timeline");
    }

    const auto zoomSeconds = explain.timelineZoomSeconds.load();
    (zoomSeconds == 4 ? zoom4 : zoomSeconds == 16 ? zoom16 : zoom8).setToggleState (true, juce::dontSendNotification);

    zoom4.onClick  = [this] { proc.getExplainState().timelineZoomSeconds.store (4);  repaint(); };
    zoom8.onClick  = [this] { proc.getExplainState().timelineZoomSeconds.store (8);  repaint(); };
    zoom16.onClick = [this] { proc.getExplainState().timelineZoomSeconds.store (16); repaint(); };
    closeButton.onClick = [this] { if (onCloseRequested != nullptr) onCloseRequested(); };

    for (auto* c : std::initializer_list<juce::Component*> { &pauseButton, &zoom4, &zoom8, &zoom16, &closeButton })
        addAndMakeVisible (c);

    list.setModel (this);
    list.setRowHeight (18);
    list.setWantsKeyboardFocus (true);
    list.setTitle ("Recent gap events");
    list.setDescription ("The last 20 timeline events as text, newest first.");
    list.setColour (juce::ListBox::backgroundColourId, colours::background.withAlpha (0.6f));
    list.setColour (juce::ListBox::outlineColourId, colours::outline);
    list.setOutlineThickness (1);
    addAndMakeVisible (list);
}

TimelineView::~TimelineView()
{
    stopTimer();
    list.setModel (nullptr);
}

void TimelineView::visibilityChanged()
{
    if (isVisible())
    {
        refresh();
        startTimerHz (proc.isReducedMotionEnabled() ? 4 : 20);
    }
    else
    {
        stopTimer();
    }
}

void TimelineView::resized()
{
    auto b = getLocalBounds().reduced (6);
    auto bar = b.removeFromTop (kControlsHeight);
    closeButton.setBounds (bar.removeFromRight (70).reduced (2));
    bar.removeFromRight (8);
    zoom16.setBounds (bar.removeFromRight (52).reduced (2));
    zoom8.setBounds (bar.removeFromRight (52).reduced (2));
    zoom4.setBounds (bar.removeFromRight (52).reduced (2));
    bar.removeFromRight (8);
    pauseButton.setBounds (bar.removeFromRight (84).reduced (2));

    if (b.getWidth() > 520)
        list.setBounds (b.removeFromRight (kListWidth));
    else
        list.setBounds ({});
}

//==============================================================================
std::int64_t TimelineView::liveEndPos() const
{
    return proc.getSourceHistoryWritePosition() - (std::int64_t) proc.getMeterState().latencySamples.load();
}

std::int64_t TimelineView::windowSamples() const
{
    return (std::int64_t) (proc.getExplainState().timelineZoomSeconds.load() * juce::jmax (8000.0, proc.getExplainState().sampleRate.load()));
}

void TimelineView::timerCallback()
{
    refresh();
}

void TimelineView::refresh()
{
    auto& model = proc.getExplainState().timelineModel;
    shownEnd = model.viewEnd (liveEndPos());
    model.window (shownEnd, windowSamples(), windowEvents);
    numListItems = model.latest (listItems.data(), listRows);
    list.updateContent();
    list.repaint();
    repaint();
}

int TimelineView::getNumListRows() const   { return numListItems; }
int TimelineView::getNumRows()             { return numListItems; }

juce::StringArray TimelineView::getListText() const
{
    juce::StringArray out;
    const auto sr = juce::jmax (8000.0, proc.getExplainState().sampleRate.load());

    for (int i = 0; i < numListItems; ++i)
    {
        const auto& e = listItems[(size_t) i];
        out.add (juce::String ((double) (e.pos - shownEnd) / sr, 1) + " s  " + describe (e));
    }

    return out;
}

void TimelineView::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= numListItems)
        return;

    if (selected)
    {
        g.setColour (colours::accent.withAlpha (0.25f));
        g.fillRect (0, 0, width, height);
    }

    g.setColour (colours::text);
    g.setFont (fonts::ui (11.0f));
    g.drawText (getListText()[row], 6, 0, width - 8, height, juce::Justification::centredLeft, true);
}

//==============================================================================
void TimelineView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (colours::background.withAlpha (0.96f));
    g.fillRoundedRectangle (b, 7.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (b.reduced (0.5f), 7.0f, 1.0f);

    auto inner = getLocalBounds().reduced (6);
    inner.removeFromTop (kControlsHeight);

    if (inner.getWidth() > 520)
        inner.removeFromRight (kListWidth + 8);

    paintLanes (g, inner.toFloat());

    g.setColour (colours::textDim);
    g.setFont (fonts::label (10.0f));
    g.drawText ("TIMELINE", getLocalBounds().reduced (12, 6).removeFromTop (kControlsHeight), juce::Justification::centredLeft, false);
}

void TimelineView::paintLanes (juce::Graphics& g, juce::Rectangle<float> plot)
{
    const auto sr = juce::jmax (8000.0, proc.getExplainState().sampleRate.load());
    const auto window = windowSamples();
    const auto startPos = shownEnd - window;
    auto axis = plot.removeFromBottom ((float) kAxisHeight);

    const auto laneEvents = plot.removeFromTop (plot.getHeight() * 0.26f);
    const auto laneGaps = plot.removeFromTop (plot.getHeight() * 0.52f);
    const auto laneFills = plot;

    auto xOf = [&] (std::int64_t pos) { return plot.getX() + (float) ((double) (pos - startPos) / (double) window) * plot.getWidth(); };
    auto msToPx = [&] (double ms) { return (float) (ms * 0.001 * sr / (double) window) * plot.getWidth(); };

    for (auto lane : { laneEvents, laneGaps, laneFills })
    {
        g.setColour (colours::panelRaised.withAlpha (0.55f));
        g.fillRoundedRectangle (lane.reduced (0.0f, 1.0f), 3.0f);
    }

    g.setFont (fonts::label (9.0f));
    g.setColour (colours::textMuted);
    g.drawText ("EVENTS", laneEvents.reduced (6.0f, 0.0f), juce::Justification::topLeft, false);
    g.drawText ("GAPS", laneGaps.reduced (6.0f, 0.0f), juce::Justification::topLeft, false);
    g.drawText ("FILLS", laneFills.reduced (6.0f, 0.0f), juce::Justification::topLeft, false);

    // Confidence threshold: the engine's own number (below it is "quiet but not a gap").
    {
        const auto y = laneGaps.getBottom() - kEnterGapConf * laneGaps.getHeight() * 0.92f - 2.0f;
        const float dashes[] = { 4.0f, 3.0f };
        g.setColour (colours::accent.withAlpha (0.6f));
        g.drawDashedLine ({ laneGaps.getX(), y, laneGaps.getRight(), y }, dashes, 2, 1.0f);
        g.setFont (fonts::label (8.5f));
        g.drawText ("gap threshold", juce::Rectangle<float> (laneGaps.getRight() - 90.0f, y - 11.0f, 88.0f, 10.0f),
                    juce::Justification::centredRight, false);
    }

    // Pass 1: structure (gap spans end at the next event), protected zones, confidence line.
    juce::Path confidence;
    bool confidenceStarted = false;
    std::int64_t gapOpenPos = -1;

    auto closeGap = [&] (std::int64_t endPos)
    {
        if (gapOpenPos < 0)
            return;

        auto r = juce::Rectangle<float> (xOf (gapOpenPos), laneGaps.getY() + 1.0f,
                                         juce::jmax (1.5f, xOf (endPos) - xOf (gapOpenPos)), laneGaps.getHeight() - 2.0f);
        reasons::drawSwatch (g, r.getIntersection (laneGaps), reasons::Pattern::Tint, colours::accent);
        gapOpenPos = -1;
    };

    for (const auto& e : windowEvents)
    {
        const auto x = xOf (e.pos);

        if (e.type == EventType::Onset)
        {
            closeGap (e.pos);

            if (e.protHoldMs > 0)
            {
                auto zone = juce::Rectangle<float> (x, laneEvents.getY() + 1.0f, msToPx (e.protHoldMs), 4.0f);
                g.setColour (colours::accentAmber.withAlpha (0.85f));
                g.fillRect (zone.getIntersection (laneEvents));
            }

            if (e.lookaheadMs > 0)
            {
                const auto w = msToPx (e.lookaheadMs);
                g.setColour (colours::accent.withAlpha (0.7f));
                const float dashes[] = { 3.0f, 2.0f };
                g.drawDashedLine ({ x - w, laneEvents.getBottom() - 3.0f, x, laneEvents.getBottom() - 3.0f }, dashes, 2, 1.2f);
            }

            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillRect (x - 1.0f, laneEvents.getY() + 6.0f, 2.0f, laneEvents.getHeight() - 8.0f);
        }
        else if (e.type == EventType::GapOpen)
        {
            gapOpenPos = e.pos;
        }

        if (e.type == EventType::GapOpen || e.type == EventType::GapEval || e.type == EventType::Onset)
        {
            const auto y = laneGaps.getBottom() - ((float) e.confidence8 / 255.0f) * laneGaps.getHeight() * 0.92f - 2.0f;

            if (! confidenceStarted) { confidence.startNewSubPath (x, y); confidenceStarted = true; }
            else                     { confidence.lineTo (x, y); }
        }
    }

    closeGap (shownEnd);

    if (confidenceStarted)
    {
        g.setColour (colours::accent.withAlpha (0.9f));
        g.strokePath (confidence, juce::PathStrokeType (1.3f));
    }

    // Pass 2: fills and skipped gaps.
    for (std::size_t i = 0; i < windowEvents.size(); ++i)
    {
        const auto& e = windowEvents[i];
        const auto x = xOf (e.pos);

        if (e.type == EventType::FillStart)
        {
            // End at the matching FillEnd when it is in view, otherwise the planned length.
            float w = msToPx (e.lenMs);

            for (std::size_t j = i + 1; j < windowEvents.size(); ++j)
                if (windowEvents[j].type == EventType::FillEnd) { w = juce::jmax (2.0f, xOf (windowEvents[j].pos) - x); break; }

            auto r = juce::Rectangle<float> (x, laneFills.getY() + 12.0f, juce::jmax (2.0f, w), laneFills.getHeight() - 16.0f);
            const auto colour = familyColour (e.algo);
            g.setColour (colour.withAlpha (0.85f));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (colours::background.withAlpha (0.7f));

            // Pattern per family so colour is never the only cue.
            {
                juce::Graphics::ScopedSaveState s (g);
                g.reduceClipRegion (r.toNearestInt());

                if (e.algo % 4 == 1)       for (float hx = r.getX() - r.getHeight(); hx < r.getRight(); hx += 5.0f) g.drawLine (hx, r.getBottom(), hx + r.getHeight(), r.getY(), 1.0f);
                else if (e.algo % 4 == 2)  for (float dx = r.getX() + 2.0f; dx < r.getRight(); dx += 5.0f) g.fillRect (dx, r.getCentreY() - 1.0f, 2.0f, 2.0f);
                else if (e.algo % 4 == 3)  for (float vx = r.getX() + 2.0f; vx < r.getRight(); vx += 5.0f) g.fillRect (vx, r.getY(), 1.0f, r.getHeight());
            }

            if (e.algo < numAlgos && r.getWidth() > 38.0f)
            {
                g.setColour (colours::text);
                g.setFont (fonts::label (8.5f));
                g.drawText (juce::String (algoName ((Algo) e.algo)).toUpperCase(), r.reduced (3.0f, 0.0f),
                            juce::Justification::centredLeft, true);
            }
        }
        else if (e.type == EventType::GapEval && e.reason != (std::uint8_t) RejectReason::None)
        {
            juce::Path d;
            const auto cy = laneFills.getCentreY() + 4.0f;
            d.addQuadrilateral (x, cy - 6.0f, x + 5.0f, cy, x, cy + 6.0f, x - 5.0f, cy);
            g.setColour (colours::text.withAlpha (0.95f));
            g.strokePath (d, juce::PathStrokeType (1.4f));
            g.setFont (fonts::mono (8.5f));
            g.drawText (juce::String::charToString ((juce::juce_wchar) reasons::info ((RejectReason) e.reason).letter),
                        juce::Rectangle<float> (x + 6.0f, cy - 6.0f, 12.0f, 12.0f).toNearestInt(), juce::Justification::centredLeft, false);
        }
        else if (e.type == EventType::Preview)
        {
            auto r = juce::Rectangle<float> (x, laneFills.getY() + 12.0f, msToPx (e.lenMs > 0 ? e.lenMs : 120), laneFills.getHeight() - 16.0f);
            reasons::drawSwatch (g, r, reasons::Pattern::Hatch, colours::accentWarm);
        }
        else if (e.type == EventType::SceneApplied || e.type == EventType::Panic || e.type == EventType::Recover)
        {
            g.setColour (e.type == EventType::Panic ? colours::clip : colours::secondary);
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            g.setFont (fonts::label (8.5f));
            g.drawText (e.type == EventType::SceneApplied ? "SCENE" : e.type == EventType::Panic ? "PANIC" : "RECOVER",
                        juce::Rectangle<float> (x + 3.0f, plot.getY(), 60.0f, 10.0f), juce::Justification::centredLeft, false);
        }
    }

    // Time axis in seconds before "now".
    g.setColour (colours::textMuted);
    g.setFont (fonts::mono (9.0f));
    const auto windowSeconds = (int) (window / sr);

    for (int s = 0; s <= windowSeconds; s += juce::jmax (1, windowSeconds / 8))
    {
        const auto px = plot.getRight() - (float) s / (float) windowSeconds * plot.getWidth();
        g.drawVerticalLine ((int) px, axis.getY(), axis.getY() + 3.0f);
        g.drawText (s == 0 ? "now" : "-" + juce::String (s) + "s",
                    juce::Rectangle<float> (px - 22.0f, axis.getY() + 2.0f, 44.0f, 11.0f), juce::Justification::centred, false);
    }

    // Overflow: the left end-cap is striped when events were lost inside the window.
    if (proc.getExplainState().timelineModel.isIncomplete (shownEnd, window))
    {
        auto cap = juce::Rectangle<float> (plot.getX(), plot.getY(), 16.0f, plot.getHeight() + 2.0f);
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (cap.toNearestInt());
        g.setColour (colours::warning.withAlpha (0.7f));

        for (float y = cap.getY() - 16.0f; y < cap.getBottom(); y += 6.0f)
            g.drawLine (cap.getX(), y + 16.0f, cap.getRight(), y, 2.0f);

        g.setColour (colours::warning);
        g.setFont (fonts::label (9.0f));
        g.drawText ("view incomplete", juce::Rectangle<float> (plot.getX() + 20.0f, axis.getY(), 110.0f, 12.0f),
                    juce::Justification::centredLeft, false);
    }
}

} // namespace gapfill::timeline
