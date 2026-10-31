#include "OverlaySlot.h"

#include "../../PluginProcessor.h"
#include "../../gui/SlotHost.h"
#include "../i18n/Strings.h"

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
    timeline.setVisible (proc.getExplainState().timelineExpanded.load());
    timelineToggle.setToggleState (timeline.isVisible(), juce::dontSendNotification);
    timelineToggle.setButtonText (timeline.isVisible() ? "HIDE TIMELINE" : "TIMELINE");
    addChildComponent (timeline);
    updateText();
    i18n::languageChanges().addChangeListener (this);

    proc.getVisualisationFifo().setReadObserver (this,
        [] (void* context, const VizFrame* drained, int count)
        {
            auto& processor = static_cast<VisualiserOverlayComponent*> (context)->proc;
            auto& frames = processor.getExplainState().timelineModel.frames;
            frames.setPositionAnchor (processor.getSourceHistoryWritePosition()
                                      - processor.getMeterState().latencySamples.load());
            frames.push (drained, count);
        });
}

VisualiserOverlayComponent::~VisualiserOverlayComponent()
{
    i18n::languageChanges().removeChangeListener (this);
    proc.getVisualisationFifo().clearReadObserver (this);
}

void VisualiserOverlayComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateText();
    resized();
}

void VisualiserOverlayComponent::updateText()
{
    timelineToggle.setButtonText (timeline.isVisible() ? i18n::text ("HIDE TIMELINE") : i18n::text ("TIMELINE"));
    timelineToggle.setTitle (i18n::text ("Gap timeline"));
    timelineToggle.setTooltip (i18n::text ("Expand the gap timeline: events, gaps, fills and skipped gaps on one time axis"));
}

void VisualiserOverlayComponent::setTimelineExpanded (bool shouldExpand)
{
    const bool changed = timeline.isVisible() != shouldExpand;
    proc.getExplainState().timelineExpanded.store (shouldExpand);
    timelineToggle.setToggleState (shouldExpand, juce::dontSendNotification);
    timeline.setVisible (shouldExpand);
    updateText();
    resized();
    if (changed && onExpansionChanged != nullptr)
        onExpansionChanged();
}

void VisualiserOverlayComponent::setExpansionBounds (juce::Rectangle<int> available)
{
    if (! timeline.isVisible() || available.isEmpty())
        return;

    const auto compact = getBounds();
    const auto height = juce::jmin (compact.getHeight() * 3, available.getBottom() - compact.getY());
    if (height > compact.getHeight())
        setBounds (compact.withHeight (height));
    toFront (false);
}

void VisualiserOverlayComponent::resized()
{
    constexpr int right = 8 + 196;                     // the view's own "4.0 s | lookahead" readout
    const auto toggleWidth = juce::jmax (timelineToggle.getToggleState() ? 104 : 84,
                                       timelineToggle.getBestWidthForHeight (18));
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
