#include "Gui.h"

#include "../../Parameters.h"
#include "../../PluginProcessor.h"
#include "../reasons/Gui.h"
#include "../reasons/Reasons.h"
#include "../i18n/Strings.h"

namespace gapfill::timeline
{

namespace
{
    constexpr int kAxisHeight = 28; // separate bar-number and seconds rows

    juce::Colour familyColour (int algo)
    {
        static const juce::Colour palette[] = { colours::primary, colours::secondary, colours::warning, colours::success };
        return palette[(size_t) (juce::jmax (0, algo) % 4)];
    }
}

TimelineView::TimelineView (GapFillAudioProcessor& p) : proc (p)
{
    setTitle ("Gap timeline");
    setDescription ("Events, gaps, fills and skipped gaps on a common time axis. Event list provides a text alternative.");
    setOpaque (false);

    auto& explain = proc.getExplainState();
    windowEvents.reserve (TimelineModel::capacity);
    displayFrames.reserve (maxDisplayColumns);
    if (explain.timelineModel.epoch() == 0)
        explain.timelineModel.adoptEpoch (0, (std::uint32_t) juce::jlimit (8000.0, (double) UINT32_MAX, explain.sampleRate.load()));
    if (explain.timelineModel.frames.capacity() == 0)
        explain.timelineModel.frames.configure (explain.timelineModel.displaySampleRate(), 16.0);
    explain.timelineModel.frames.setPositionAnchor (liveEndPos());

    pauseButton.setClickingTogglesState (true);
    pauseButton.setTooltip ("Freeze the display. Events keep being recorded.");
    pauseButton.onClick = [this] { setPaused (pauseButton.getToggleState()); };

    for (auto* b : { &zoom4, &zoom8, &zoom16 })
    {
        b->setRadioGroupId (771);
        b->setClickingTogglesState (true);
        b->setTooltip ("Time shown across the timeline");
    }

    const auto zoomSeconds = explain.timelineZoomSeconds.load();
    (zoomSeconds == 4 ? zoom4 : zoomSeconds == 16 ? zoom16 : zoom8).setToggleState (true, juce::dontSendNotification);

    zoom4.onClick  = [this] { proc.getExplainState().timelineZoomSeconds.store (4);  snapshotReady = false; refresh(); };
    zoom8.onClick  = [this] { proc.getExplainState().timelineZoomSeconds.store (8);  snapshotReady = false; refresh(); };
    zoom16.onClick = [this] { proc.getExplainState().timelineZoomSeconds.store (16); snapshotReady = false; refresh(); };
    closeButton.onClick = [this] { if (onCloseRequested != nullptr) onCloseRequested(); };
    closeButton.setTooltip ("Close the detailed timeline");

    listButton.setClickingTogglesState (true);
    listButton.setTooltip ("Replace the timeline canvas with recent events as an accessible text list");
    listButton.onClick = [this] { setListOnly (listButton.getToggleState()); };
    for (auto* button : { &pauseButton, &zoom4, &zoom8, &zoom16, &listButton, &closeButton })
        button->setTitle (button->getButtonText());

    for (auto* c : std::initializer_list<juce::Component*> { &pauseButton, &zoom4, &zoom8, &zoom16, &listButton, &closeButton })
        addAndMakeVisible (c);

    list.setModel (this);
    list.setRowHeight (18);
    list.setWantsKeyboardFocus (proc.isKeyboardNavigationEnabled());
    list.setTitle ("Recent gap events");
    list.setDescription ("The last 20 timeline events as text, newest first.");
    list.setColour (juce::ListBox::backgroundColourId, colours::background.withAlpha (0.6f));
    list.setColour (juce::ListBox::outlineColourId, colours::outline);
    list.setOutlineThickness (1);
    addAndMakeVisible (list);
    setListOnly (explain.timelineList.load());
}

TimelineView::~TimelineView()
{
    stopTimer();
    list.setModel (nullptr);
    proc.getExplainState().timelineModel.setPaused (false, 0);
}

void TimelineView::visibilityChanged()
{
    if (isVisible())
    {
        refresh();
        startTimerHz (proc.isReducedMotionEnabled() ? 4 : 30);
    }
    else
    {
        stopTimer();
    }
}

void TimelineView::resized()
{
    auto b = getLocalBounds().reduced (6);
    auto bar = b.removeFromTop (controlsHeight);
    closeButton.setBounds (bar.removeFromRight (56).reduced (2));
    bar.removeFromRight (4);
    listButton.setBounds (bar.removeFromRight (88).reduced (2));
    bar.removeFromRight (4);
    zoom16.setBounds (bar.removeFromRight (40).reduced (2));
    zoom8.setBounds (bar.removeFromRight (40).reduced (2));
    zoom4.setBounds (bar.removeFromRight (40).reduced (2));
    bar.removeFromRight (4);
    pauseButton.setBounds (bar.removeFromRight (80).reduced (2));
    list.setBounds (listOnly ? b : juce::Rectangle<int>());
}

//==============================================================================
std::int64_t TimelineView::liveEndPos() const
{
    return proc.getTimelinePosition();
}

std::int64_t TimelineView::windowSamples() const
{
    return (std::int64_t) (proc.getExplainState().timelineZoomSeconds.load()
                          * proc.getExplainState().timelineModel.displaySampleRate());
}

void TimelineView::timerCallback()
{
    const auto interval = proc.isReducedMotionEnabled() ? 250 : 33;
    if (getTimerInterval() != interval)
        startTimer (interval);
    refresh();
}

void TimelineView::refresh()
{
    auto& model = proc.getExplainState().timelineModel;
    const auto rate = model.displaySampleRate();
    if (model.frames.sampleRate() != rate)
    {
        model.frames.configure (rate, 16.0);
        model.frames.setPositionAnchor (liveEndPos());
        snapshotReady = false;
    }
    setWantsKeyboardFocus (proc.isKeyboardNavigationEnabled());
    list.setWantsKeyboardFocus (proc.isKeyboardNavigationEnabled());
    const bool generationChanged = displayedEpoch != model.epoch();
    if (generationChanged)
    {
        displayedEpoch = model.epoch();
        snapshotReady = false;
        inspectorShown = false;
        inspectorText.clear();
        resized();
    }
    const auto end = model.viewEnd (liveEndPos());
    const auto languageGeneration = i18n::languageGeneration();
    const bool languageChanged = languageGeneration != displayedLanguageGeneration;
    const bool retainSnapshot = model.isPaused() && snapshotReady && end == shownEnd;
    if (retainSnapshot && ! languageChanged)
        return; // Retain the visible snapshot while ingestion continues.

    if (! retainSnapshot)
    {
        shownEnd = end;
        model.window (shownEnd, windowSamples(), windowEvents);
        collectFrames();
        numListItems = model.latest (listItems.data(), listRows);
        numOutcomeItems = model.latestOutcomes (outcomeItems.data(), outcomeRows, shownEnd);
    }
    displayedLanguageGeneration = languageGeneration;
    if (languageChanged || generationChanged)
    {
        pauseButton.setToggleState (model.isPaused(), juce::dontSendNotification);
        pauseButton.setButtonText (model.isPaused() ? i18n::text ("LIVE") : i18n::text ("FREEZE"));
        pauseButton.setTitle (pauseButton.getButtonText());
        pauseButton.setTooltip (model.isPaused() ? i18n::text ("Resume the live display")
                                                : i18n::text ("Freeze the display. Events keep being recorded."));
        listButton.setButtonText (i18n::text ("EVENT LIST"));
        closeButton.setButtonText (i18n::text ("CLOSE"));
        setTitle (i18n::text ("Gap timeline"));
        updateListAccessibility();
    }
    if (inspectorShown)
        updateInspector (inspectorX);
    list.updateContent();
    list.repaint();
    int filled = 0, skipped = 0;
    for (const auto& event : windowEvents)
        if (event.type == EventType::GapEval)
            event.reason == 0 ? ++filled : ++skipped;
    const auto summary = i18n::format ("Last {0} seconds: {1} gaps, {2} filled, {3} skipped. F freezes, arrows scrub, I inspects, L opens Event list.",
                                      { juce::String (proc.getExplainState().timelineZoomSeconds.load()), juce::String (filled + skipped),
                                        juce::String (filled), juce::String (skipped) });
    if (summary != getDescription())
    {
        setDescription (summary);
        if (hasKeyboardFocus (true))
            juce::AccessibilityHandler::postAnnouncement (summary, juce::AccessibilityHandler::AnnouncementPriority::low);
    }
    snapshotReady = true;
    repaint();
}

int TimelineView::getNumListRows() const   { return listOnly ? numOutcomeItems : numListItems; }
int TimelineView::getNumRows()             { return getNumListRows(); }

void TimelineView::updateListAccessibility()
{
    list.setTitle (listOnly ? i18n::text ("Recent gap outcomes") : i18n::text ("Recent gap events"));
    list.setDescription (listOnly ? i18n::text ("The last 16 gap outcomes at or before the inspected time, newest first.")
                                  : i18n::text ("The last 20 timeline events as text, newest first."));
}

juce::StringArray TimelineView::getListText() const
{
    juce::StringArray result;
    for (int i = 0; i < getNumListRows(); ++i)
        result.add (listRowText (i));
    return result;
}

juce::String TimelineView::listRowText (int row) const
{
    if (row < 0 || row >= getNumListRows())
        return {};
    const auto& event = listOnly ? outcomeItems[(size_t) row] : listItems[(size_t) row];
    const auto rate = proc.getExplainState().timelineModel.displaySampleRate();
    auto description = describe (event);
    if (listOnly && event.type == EventType::GapEval && event.reason == 0 && event.algo < numAlgos)
        description = i18n::format ("Filled: {0}, {1} ms", { i18n::text (algoName ((Algo) event.algo)), juce::String (event.lenMs) });
    return i18n::format ("{0} s  {1}", { juce::String ((double) (event.pos - shownEnd) / rate, 1), description });
}

juce::String TimelineView::getNameForRow (int row) { return listRowText (row); }

TimelineView::RetainedMemory TimelineView::retainedMemoryForTests() const
{
    RetainedMemory memory;
    const auto& model = proc.getExplainState().timelineModel;
    memory.model = sizeof (TimelineModel) + model.frames.memoryBytes();
    memory.frameSnapshot = displayFrames.capacity() * sizeof (FrameHistory::Column);
    memory.eventSnapshot = windowEvents.capacity() * sizeof (TimelineEvent);
    memory.view = sizeof (*this); // includes the 20 structured list records and container objects
    const auto charge = [&] (const juce::String& value)
    {
        // Pinned JUCE stores COW text. Overcharge each reference independently,
        // including UTF conversion/spare storage and the allocation header.
        if (value.isNotEmpty()) memory.textUpperBound += 4 * (value.getNumBytesAsUTF8() + 1) + 128;
    };
    charge (inspectorText);
    const auto componentText = [&] (auto&& self, const juce::Component& component) -> void
    {
        charge (component.getTitle());
        charge (component.getDescription());
        charge (component.getName());
        charge (component.getHelpText());
        if (auto* button = dynamic_cast<const juce::Button*> (&component)) charge (button->getButtonText());
        if (auto* label = dynamic_cast<const juce::Label*> (&component)) charge (label->getText());
        for (auto* child : component.getChildren()) self (self, *child);
    };
    componentText (componentText, *this);
    return memory;
}

void TimelineView::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= getNumListRows())
        return;

    if (selected)
    {
        g.setColour (colours::accent.withAlpha (0.25f));
        g.fillRect (0, 0, width, height);
    }

    g.setColour (colours::text);
    g.setFont (fonts::ui (11.0f));
    g.drawText (listRowText (row), 6, 0, width - 8, height, juce::Justification::centredLeft, true);
}

//==============================================================================
void TimelineView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (colours::background.withAlpha (0.96f));
    g.fillRoundedRectangle (b, 7.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (b.reduced (0.5f), 7.0f, 1.0f);

    if (! listOnly)
        paintLanes (g, plotBounds().toFloat());

    if (inspectorShown && inspectorText.isNotEmpty() && ! listOnly)
    {
        g.setColour (colours::text);
        g.setFont (fonts::ui (10.0f));
        g.drawText (inspectorText, getLocalBounds().reduced (8, 4).removeFromBottom (16),
                    juce::Justification::centredLeft, true);
    }

    g.setColour (colours::textDim);
    g.setFont (fonts::label (10.0f));
    g.drawText (i18n::text ("TIMELINE"), getLocalBounds().reduced (12, 6).removeFromTop (controlsHeight), juce::Justification::centredLeft, false);
}

void TimelineView::paintLanes (juce::Graphics& g, juce::Rectangle<float> plot)
{
    const auto sr = proc.getExplainState().timelineModel.displaySampleRate();
    const auto window = windowSamples();
    const auto startPos = shownEnd - window;
    auto axis = plot.removeFromBottom ((float) kAxisHeight);
    axis.removeFromTop ((float) kAxisHeight * 0.5f);
    const auto* detMode = proc.getValueTreeState().getRawParameterValue ("detMode");
    const bool showBands = detMode != nullptr && (int) detMode->load() == (int) DetMode::Spectral && getWidth() >= 1100;
    const auto bandLanes = showBands ? plot.removeFromBottom (juce::jmin (54.0f, plot.getHeight() * 0.25f))
                                    : juce::Rectangle<float>();

    const auto laneEvents = plot.removeFromTop (plot.getHeight() * 0.22f);
    const auto laneGaps = plot.removeFromTop (plot.getHeight() * 0.36f);
    const auto laneProtection = plot.removeFromTop (plot.getHeight() * 0.35f);
    const auto laneFills = plot;

    auto xOf = [&] (std::int64_t pos) { return plot.getX() + (float) ((double) (pos - startPos) / (double) window) * plot.getWidth(); };
    auto msToPx = [&] (double ms) { return (float) (ms * 0.001 * sr / (double) window) * plot.getWidth(); };

    for (auto lane : { laneEvents, laneGaps, laneProtection, laneFills })
    {
        g.setColour (colours::panelRaised.withAlpha (0.55f));
        g.fillRoundedRectangle (lane.reduced (0.0f, 1.0f), 3.0f);
    }

    g.setFont (fonts::label (9.0f));
    g.setColour (colours::textMuted);
    g.drawText (i18n::text ("AUDIO / EVENTS"), laneEvents.reduced (6.0f, 0.0f), juce::Justification::topLeft, false);
    g.drawText (i18n::text ("GAPS"), laneGaps.reduced (6.0f, 0.0f), juce::Justification::topLeft, false);
    g.drawText (i18n::text ("FILLS"), laneFills.reduced (6.0f, 0.0f), juce::Justification::topLeft, false);
    g.drawText (i18n::text ("PROTECTION"), laneProtection.reduced (6.0f, 0.0f), juce::Justification::topLeft, false);
    paintFrameLanes (g, laneEvents, laneGaps, laneProtection, bandLanes);

    // Confidence threshold: the engine's own number (below it is "quiet but not a gap").
    {
        const auto y = laneGaps.getBottom() - kEnterGapConf * laneGaps.getHeight() * 0.92f - 2.0f;
        const float dashes[] = { 4.0f, 3.0f };
        g.setColour (colours::accent.withAlpha (0.6f));
        g.drawDashedLine ({ laneGaps.getX(), y, laneGaps.getRight(), y }, dashes, 2, 1.0f);
        g.setFont (fonts::label (8.5f));
        g.drawText (i18n::text ("gap threshold"), juce::Rectangle<float> (laneGaps.getRight() - 90.0f, y - 11.0f, 88.0f, 10.0f),
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

    if (confidenceStarted && displayFrames.empty())
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
            // The engine records a planned length. Aggregate FillEnd events do
            // not identify a voice, so pairing the next one would invent a
            // shortened duration when several fills overlap.
            float w = msToPx (e.lenMs);

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
                g.drawText (i18n::text (algoName ((Algo) e.algo)).toUpperCase(), r.reduced (3.0f, 0.0f),
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
            g.drawText (e.type == EventType::SceneApplied ? i18n::text ("SCENE") : e.type == EventType::Panic ? i18n::text ("PANIC") : i18n::text ("RECOVER"),
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
        g.drawText (s == 0 ? i18n::text ("now") : i18n::format ("-{0}s", { juce::String (s) }),
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
        g.drawText (i18n::text ("view incomplete"), juce::Rectangle<float> (plot.getX() + 20.0f, axis.getY(), 110.0f, 12.0f),
                    juce::Justification::centredLeft, false);
    }
}

} // namespace gapfill::timeline
