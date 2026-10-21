#pragma once

// The single component WP3 registers in Slot::VisualiserOverlay. The slot host
// splits a slot's rectangle between ALL components registered for it, so the
// legend strip (E-3), the timeline toggle and the expanded timeline (F-M03) live
// in one composite instead of competing for width.

#include "../reasons/Gui.h"
#include "../timeline/Gui.h"

namespace gapfill::explain
{

class VisualiserOverlayComponent : public juce::Component
{
public:
    explicit VisualiserOverlayComponent (GapFillAudioProcessor&);

    void resized() override;
    bool hitTest (int x, int y) override;

    bool isTimelineExpanded() const noexcept   { return timeline.isVisible(); }
    void setTimelineExpanded (bool shouldExpand);

    reasons::LegendStrip& getLegend() noexcept          { return legend; }
    timeline::TimelineView& getTimeline() noexcept      { return timeline; }

    static constexpr int stripHeight = 22;

private:
    GapFillAudioProcessor& proc;
    reasons::LegendStrip legend;
    juce::TextButton timelineToggle { "TIMELINE" };
    timeline::TimelineView timeline;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisualiserOverlayComponent)
};

} // namespace gapfill::explain
