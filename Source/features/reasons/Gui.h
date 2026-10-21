#pragma once

// E-3 legend strip (docs/dev/03 E-3 "UI"): Event, Gap-eligible, Fill, Protected,
// Lookahead, Preview, Skipped. Every entry carries a colour AND a pattern/shape
// plus text, so nothing is encoded by colour alone. Collapsible; the state lives
// in ExplainState (persisted in the <Health> subtree).

#include "../../gui/LookAndFeel.h"

#include <array>
#include <functional>

namespace gapfill::reasons
{

enum class Pattern { VerticalBar, Tint, SolidBar, TopTick, DashedEdge, Hatch, HollowDiamond };

struct LegendEntry
{
    const char* label;
    const char* description;      ///< accessibility text
    Pattern pattern;
};

/** The seven states, in display order. */
const std::array<LegendEntry, 7>& legendEntries() noexcept;

/** Draws one swatch (colour + pattern) into `r`. Shared with the timeline so both
    views use the same visual language. */
void drawSwatch (juce::Graphics&, juce::Rectangle<float> r, Pattern, juce::Colour);

juce::Colour swatchColour (Pattern) noexcept;

class LegendStrip : public juce::Component
{
public:
    LegendStrip();

    void setCollapsed (bool shouldCollapse);
    bool isCollapsed() const noexcept   { return collapsed; }

    /** Width this strip would like at the current state. */
    int getPreferredWidth() const;

    std::function<void (bool collapsed)> onCollapsedChanged;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void toggle();

    bool collapsed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LegendStrip)
};

} // namespace gapfill::reasons
