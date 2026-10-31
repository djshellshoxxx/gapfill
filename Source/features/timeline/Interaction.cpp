#include "Gui.h"

#include "../../PluginProcessor.h"
#include "../i18n/Strings.h"

#include <limits>

namespace gapfill::timeline
{

juce::Rectangle<int> TimelineView::plotBounds() const
{
    auto bounds = getLocalBounds().reduced (6);
    bounds.removeFromTop (controlsHeight);
    if (inspectorShown)
        bounds.removeFromBottom (16);
    return bounds;
}

void TimelineView::setPaused (bool paused)
{
    proc.getExplainState().timelineModel.setPaused (paused, liveEndPos());
    pauseButton.setToggleState (paused, juce::dontSendNotification);
    pauseButton.setButtonText (paused ? i18n::text ("LIVE") : i18n::text ("FREEZE"));
    pauseButton.setTitle (pauseButton.getButtonText());
    pauseButton.setTooltip (paused ? i18n::text ("Resume the live display")
                                  : i18n::text ("Freeze the display. Events keep being recorded."));
    snapshotReady = false;
    refresh();
}

void TimelineView::setListOnly (bool enabled)
{
    listOnly = enabled;
    proc.getExplainState().timelineList.store (enabled);
    listButton.setToggleState (enabled, juce::dontSendNotification);
    list.setVisible (enabled);
    resized();
    repaint();
}

bool TimelineView::keyPressed (const juce::KeyPress& key)
{
    if (! proc.isKeyboardNavigationEnabled())
        return false;
    if (key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown() || key.getModifiers().isCommandDown())
        return false;

    const auto letter = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) key.getKeyCode());
    auto& model = proc.getExplainState().timelineModel;
    if (letter == 'f')
        setPaused (! model.isPaused());
    else if (letter == 'l')
        setListOnly (! listOnly);
    else if (letter == 'i')
    {
        inspectorShown = ! inspectorShown;
        if (inspectorShown)
            updateInspector ((float) plotBounds().getCentreX());
        resized();
        repaint();
    }
    else if (key == juce::KeyPress::homeKey)
        setPaused (false);
    else if (key == juce::KeyPress::escapeKey && onCloseRequested != nullptr)
        onCloseRequested();
    else if ((key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey)
              && model.isPaused())
    {
        const auto seconds = key.getModifiers().isShiftDown() ? 1.0 : 0.1;
        const auto step = (std::int64_t) std::llround (seconds * proc.getExplainState().sampleRate.load());
        model.scrubTo (shownEnd + (key.getKeyCode() == juce::KeyPress::leftKey ? -step : step), liveEndPos());
        refresh();
    }
    else
        return false;

    return true;
}

void TimelineView::updateInspector (float x)
{
    inspectorX = x;
    if (windowEvents.empty() || plotBounds().getWidth() <= 0)
    {
        inspectorText = i18n::text ("No recorded gap outcome at this position");
        return;
    }

    const auto plot = plotBounds();
    const auto fraction = juce::jlimit (0.0, 1.0, (double) (x - (float) plot.getX()) / (double) plot.getWidth());
    const auto position = shownEnd - windowSamples() + (std::int64_t) (fraction * (double) windowSamples());
    const TimelineEvent* nearest = nullptr;
    auto distance = std::numeric_limits<std::int64_t>::max();
    for (const auto& event : windowEvents)
    {
        if (event.type != EventType::GapEval && event.type != EventType::FillStart && event.type != EventType::Preview)
            continue;
        const auto delta = std::abs (event.pos - position);
        if (delta < distance)
        {
            nearest = &event;
            distance = delta;
        }
    }

    if (nearest == nullptr)
    {
        inspectorText = i18n::text ("No recorded gap outcome at this position");
        return;
    }

    const auto seconds = (double) (shownEnd - nearest->pos) / juce::jmax (8000.0, proc.getExplainState().sampleRate.load());
    inspectorText = i18n::format ("{0} s ago: {1}", { juce::String (seconds, 2), describe (*nearest) });
}

void TimelineView::mouseMove (const juce::MouseEvent& event)
{
    inspectorShown = true;
    updateInspector (event.position.x);
    repaint();
}

void TimelineView::mouseDown (const juce::MouseEvent& event)
{
    dragStartEnd = shownEnd;
    if (proc.isKeyboardNavigationEnabled())
        grabKeyboardFocus();
    mouseMove (event);
}

void TimelineView::mouseDrag (const juce::MouseEvent& event)
{
    auto& model = proc.getExplainState().timelineModel;
    if (! model.isPaused() || plotBounds().getWidth() <= 0)
        return;
    const auto delta = (std::int64_t) ((double) event.getDistanceFromDragStartX() / (double) plotBounds().getWidth()
                                    * (double) windowSamples());
    model.scrubTo (dragStartEnd - delta, liveEndPos());
    refresh();
    mouseMove (event);
}

std::unique_ptr<juce::AccessibilityHandler> TimelineView::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group);
}

} // namespace gapfill::timeline
