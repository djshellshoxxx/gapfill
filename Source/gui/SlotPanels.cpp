#include "SlotPanels.h"

namespace gapfill
{

StackPanel::StackPanel (SlotHost& h, Slot s, juce::String panelTitle, juce::Colour panelTint)
    : host (h), slot (s), title (std::move (panelTitle)), tint (panelTint)
{
    setTitle (title);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
    host.setSlotContainer (slot, &content);
}

StackPanel::~StackPanel()
{
    host.setSlotContainer (slot, nullptr);
    viewport.setViewedComponent (nullptr, false);
}

void StackPanel::paint (juce::Graphics& g)
{
    GapFillLookAndFeel::drawPanel (g, getLocalBounds().toFloat(), title, tint);
}

void StackPanel::resized()
{
    viewport.setBounds (getLocalBounds().reduced (10).withTrimmedTop (18));
    const auto w = viewport.getMaximumVisibleWidth();
    host.setSlotBounds (slot, { 0, 0, juce::jmax (1, w), juce::jmax (1, host.getStackHeight (slot)) });
}

//==============================================================================
EffectsDrawer::EffectsDrawer (SlotHost& host)
    : body (host, Slot::EffectsSection, "Effects", colours::accentWarm)
{
    toggle.setClickingTogglesState (true);
    toggle.setTitle ("Effects section");
    toggle.setDescription ("Shows or hides the foldable effect cards");
    toggle.setTooltip ("Show or hide the effect cards. Effects are off by default and neutral until enabled.");
    toggle.onClick = [this] { setExpanded (toggle.getToggleState()); };
    addAndMakeVisible (toggle);
    addChildComponent (body);
}

void EffectsDrawer::setExpanded (bool shouldExpand)
{
    expanded = shouldExpand;
    toggle.setToggleState (expanded, juce::dontSendNotification);
    toggle.setButtonText (expanded ? "EFFECTS  -" : "EFFECTS  +");
    body.setVisible (expanded);

    if (onExpandedChanged != nullptr)
        onExpandedChanged();
}

void EffectsDrawer::resized()
{
    auto b = getLocalBounds();
    toggle.setBounds (b.removeFromTop (headerHeight).removeFromLeft (juce::jmin (140, getWidth())).reduced (0, 1));
    body.setBounds (b);
}

void EffectsDrawer::paint (juce::Graphics&) {}

//==============================================================================
TabPlan planTabs (int availableWidth, bool performancePopulated) noexcept
{
    TabPlan plan;
    const auto existing = plan.tabWidth + plan.weightsTabWidth + plan.tabWidth;

    // Existing tabs keep their size (130 / 150 / 130); the fourth must fit beside them.
    plan.performanceAsTab = performancePopulated && availableWidth >= existing + plan.performanceTabWidth;
    return plan;
}

} // namespace gapfill
