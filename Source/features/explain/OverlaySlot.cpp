#include "OverlaySlot.h"

#include "../../PluginProcessor.h"
#include "../../gui/SlotHost.h"

namespace gapfill::explain
{

VisualiserOverlayComponent::VisualiserOverlayComponent (GapFillAudioProcessor& p)
    : proc (p), timeline (p)
{
    setInterceptsMouseClicks (false, true);

    legend.setCollapsed (proc.getExplainState().legendCollapsed.load());
    legend.onCollapsedChanged = [this] (bool collapsed)
    {
        proc.getExplainState().legendCollapsed.store (collapsed);
        resized();
    };
    addAndMakeVisible (legend);

    timelineToggle.setClickingTogglesState (true);
    timelineToggle.setTooltip ("Expand the gap timeline: events, gaps, fills and skipped gaps on one time axis");
    timelineToggle.setTitle ("Gap timeline");
    timelineToggle.onClick = [this] { setTimelineExpanded (timelineToggle.getToggleState()); };
    addAndMakeVisible (timelineToggle);

    timeline.onCloseRequested = [this] { setTimelineExpanded (false); };
    timeline.setVisible (false);                       // collapsed by default
    addChildComponent (timeline);
}

void VisualiserOverlayComponent::setTimelineExpanded (bool shouldExpand)
{
    timelineToggle.setToggleState (shouldExpand, juce::dontSendNotification);
    timelineToggle.setButtonText (shouldExpand ? "HIDE TIMELINE" : "TIMELINE");
    timeline.setVisible (shouldExpand);
    resized();
}

void VisualiserOverlayComponent::resized()
{
    constexpr int right = 8 + 196;                     // the view's own "4.0 s | lookahead" readout
    const auto toggleWidth = timelineToggle.getToggleState() ? 104 : 84;
    const auto toggleX = juce::jmax (8, getWidth() - right - toggleWidth);

    timelineToggle.setBounds (toggleX, 8, toggleWidth, 18);

    const auto available = juce::jmax (40, toggleX - 8 - 8);
    legend.setBounds (8, 8, juce::jmin (legend.getPreferredWidth(), available), 18);

    timeline.setBounds (getLocalBounds().withTrimmedTop (stripHeight + 8).reduced (4, 0));
}

bool VisualiserOverlayComponent::hitTest (int x, int y)
{
    // Pass everything else through to the gap view underneath (hover tooltips for skipped gaps).
    if (timeline.isVisible() && timeline.getBounds().contains (x, y))
        return true;

    return legend.getBounds().contains (x, y) || timelineToggle.getBounds().contains (x, y);
}

//==============================================================================
namespace
{
    const SlotRegistrar overlay ({ Slot::VisualiserOverlay, 100,
        [] (GapFillAudioProcessor& proc) -> std::unique_ptr<juce::Component>
        {
            return std::make_unique<VisualiserOverlayComponent> (proc);
        } });
}

} // namespace gapfill::explain
